// LocalSend receive-core tests: discovery JSON, session lifecycle, token
// validation, filename sanitization, and collision naming. The core is
// Arduino-free, so everything runs on the host.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "LocalsendHttp.h"
#include "LocalsendService.h"
#include "test_utils.h"

namespace {

uint32_t fakeRng() {
  static uint32_t state = 0x9E3779B9;
  state = state * 1664525u + 1013904223u;
  return state;
}

LocalsendIncomingFile file(const char* id, const char* name, uint64_t size = 100) {
  return LocalsendIncomingFile{id, name, size};
}

LocalsendService makeService() {
  LocalsendService svc;
  svc.begin("PapyriX", "0123456789abcdef", 53317, &fakeRng);
  return svc;
}

}  // namespace

struct DecodeResult {
  ChunkFeed status;
  std::string body;
};

DecodeResult decodeChunks(const std::string& wire, uint64_t expected, size_t split) {
  LocalsendChunkDecoder decoder(expected);
  DecodeResult result{ChunkFeed::NeedInput, {}};
  uint8_t output[32];
  size_t offset = 0;
  while (offset < wire.size()) {
    const size_t count = split < wire.size() - offset ? split : wire.size() - offset;
    size_t consumed = 0;
    while (consumed < count) {
      size_t used = 0;
      size_t produced = 0;
      result.status = decoder.feed(reinterpret_cast<const uint8_t*>(wire.data() + offset + consumed), count - consumed,
                                   &used, output, sizeof(output), &produced);
      result.body.append(reinterpret_cast<const char*>(output), produced);
      consumed += used;
      if (result.status != ChunkFeed::NeedInput) return result;
      if (used == 0 && produced == 0) return result;
    }
    offset += count;
  }
  return result;
}

int main() {
  const char* kSenderIp = "192.168.1.100";

  TestUtils::TestRunner runner("LocalsendTest");

  {
    // The announce document carries every field a sender needs to find and
    // to talk to the device.
    LocalsendService svc = makeService();
    char buf[512];
    const size_t n = svc.buildAnnounce(buf, sizeof(buf), true);
    runner.expectTrue(n > 0, "announce: fits");
    runner.expectTrue(strstr(buf, "\"alias\":\"PapyriX\"") != nullptr, "announce: alias");
    runner.expectTrue(strstr(buf, "\"version\":\"2.0\"") != nullptr, "announce: version");
    runner.expectTrue(strstr(buf, "\"port\":53317") != nullptr, "announce: port");
    runner.expectTrue(strstr(buf, "\"protocol\":\"http\"") != nullptr, "announce: http");
    runner.expectTrue(strstr(buf, "\"announce\":true") != nullptr, "announce: flag set");
    runner.expectTrue(strstr(buf, "\"download\":false") != nullptr, "announce: no download api");

    const size_t r = svc.buildAnnounce(buf, sizeof(buf), false);
    runner.expectTrue(r > 0 && strstr(buf, "\"announce\":false") != nullptr, "announce: reply flag clear");

    runner.expectTrue(svc.buildAnnounce(buf, 16, true) == 0, "announce: small buffer reports zero");
  }

  {
    // The info document is what /info and /register answer with. It must not
    // carry the port-less announce flag.
    LocalsendService svc = makeService();
    char buf[512];
    const size_t n = svc.buildInfo(buf, sizeof(buf));
    runner.expectTrue(n > 0, "info: fits");
    runner.expectTrue(strstr(buf, "\"fingerprint\":\"0123456789abcdef\"") != nullptr, "info: fingerprint");
    runner.expectTrue(strstr(buf, "announce") == nullptr, "info: no announce field");
  }

  {
    // A normal session: prepare accepts, the response pairs each id with a
    // token, upload validates all three parameters, and cancel frees the
    // slot for the next session.
    LocalsendService svc = makeService();
    const LocalsendIncomingFile files[] = {file("id1", "book.epub"), file("id2", "notes.txt")};
    runner.expectTrue(svc.prepareUpload(files, 2, 1000, kSenderIp) == LocalsendPrepareStatus::Ok, "session: accepted");
    runner.expectTrue(svc.sessionActive(1000), "session: active");

    char resp[1024];
    const size_t n = svc.buildPrepareResponse(resp, sizeof(resp));
    runner.expectTrue(n > 0, "session: response fits");
    runner.expectTrue(strstr(resp, "\"sessionId\":\"") != nullptr, "session: id in response");
    runner.expectTrue(strstr(resp, "\"id1\":\"") != nullptr, "session: first id");
    runner.expectTrue(strstr(resp, "\"id2\":\"") != nullptr, "session: second id");

    // Extract the session id and the first token from the response to drive
    // validateUpload the way the app does.
    char sid[17] = {0};
    char tok[17] = {0};
    sscanf(resp, "{\"sessionId\":\"%16[^\"]\"", sid);
    const char* p = strstr(resp, "\"id1\":\"");
    runner.expectTrue(p != nullptr, "session: token locatable");
    sscanf(p + 7, "%16[^\"]", tok);

    runner.expectTrue(svc.validateUpload(sid, "id1", tok, 1001, kSenderIp) != nullptr, "upload: valid triple accepted");
    runner.expectTrue(svc.validateUpload("0000000000000000", "id1", tok, 1001, kSenderIp) == nullptr,
                      "upload: wrong session rejected");
    runner.expectTrue(svc.validateUpload(sid, "id1", "0000000000000000", 1001, kSenderIp) == nullptr,
                      "upload: wrong token rejected");
    runner.expectTrue(svc.validateUpload(sid, "idX", tok, 1001, kSenderIp) == nullptr, "upload: unknown id rejected");

    runner.expectTrue(svc.cancel(sid, kSenderIp), "cancel: own session");
    runner.expectTrue(!svc.sessionActive(1002), "cancel: session gone");
    runner.expectTrue(svc.prepareUpload(files, 2, 1003, kSenderIp) == LocalsendPrepareStatus::Ok,
                      "cancel: next session accepted");
  }

  {
    // Concurrency and lifecycle: a second prepare while one is active gets
    // Busy; an idle session expires and frees the slot; empty and unusable
    // requests map to their statuses.
    LocalsendService svc = makeService();
    const LocalsendIncomingFile one[] = {file("a", "a.txt")};
    runner.expectTrue(svc.prepareUpload(one, 1, 0, kSenderIp) == LocalsendPrepareStatus::Ok, "life: first ok");
    runner.expectTrue(svc.prepareUpload(one, 1, 1000, kSenderIp) == LocalsendPrepareStatus::Busy,
                      "life: busy while active");
    runner.expectTrue(svc.sessionActive(60000), "life: exact timeout boundary stays alive");
    runner.expectTrue(!svc.sessionActive(60001), "life: boundary plus 1ms expires");
    runner.expectTrue(svc.prepareUpload(one, 1, 61001, kSenderIp) == LocalsendPrepareStatus::Ok,
                      "life: expired session replaced");

    runner.expectTrue(svc.prepareUpload(nullptr, 0, 62000, kSenderIp) == LocalsendPrepareStatus::NoFiles,
                      "life: empty request 204");

    const LocalsendIncomingFile bad[] = {file("b", "../../..")};
    runner.expectTrue(svc.prepareUpload(bad, 1, 122002, kSenderIp) == LocalsendPrepareStatus::Invalid,
                      "life: unusable names 400");

    // A session that expired mid-transfer rejects further uploads.
    LocalsendService svc2 = makeService();
    svc2.prepareUpload(one, 1, 0, kSenderIp);
    char resp[512];
    svc2.buildPrepareResponse(resp, sizeof(resp));
    char sid[17] = {0};
    sscanf(resp, "{\"sessionId\":\"%16[^\"]\"", sid);
    const char* p = strstr(resp, "\"a\":\"");
    char tok[17] = {0};
    sscanf(p + 5, "%16[^\"]", tok);
    runner.expectTrue(svc2.validateUpload(sid, "a", tok, 61002, kSenderIp) == nullptr,
                      "life: expired session rejects upload");
  }

  {
    // Sanitization is the CVE-2025-27142 boundary: path components,
    // traversal, control characters, and FAT-unsafe characters must never
    // reach the filesystem.
    char out[160];
    runner.expectTrue(
        LocalsendService::sanitizeFileName("../../etc/passwd", out, sizeof(out)) && strcmp(out, "passwd") == 0,
        "sanitize: traversal drops to base name");
    runner.expectTrue(
        LocalsendService::sanitizeFileName("..\\..\\win.ini", out, sizeof(out)) && strcmp(out, "win.ini") == 0,
        "sanitize: backslash path drops to base name");
    runner.expectTrue(!LocalsendService::sanitizeFileName("../../..", out, sizeof(out)),
                      "sanitize: pure traversal rejected");
    runner.expectTrue(!LocalsendService::sanitizeFileName("", out, sizeof(out)), "sanitize: empty rejected");
    runner.expectTrue(!LocalsendService::sanitizeFileName("   ...  ", out, sizeof(out)),
                      "sanitize: dots and spaces rejected");
    runner.expectTrue(
        LocalsendService::sanitizeFileName("my book?.epub", out, sizeof(out)) && strcmp(out, "my book.epub") == 0,
        "sanitize: FAT-unsafe char removed");
    runner.expectTrue(LocalsendService::sanitizeFileName("a\tb\n.txt", out, sizeof(out)) && strcmp(out, "ab.txt") == 0,
                      "sanitize: control chars removed");
    {
      // The final extension can occur after the 159-byte copy limit.
      std::string longName(160, 'x');
      longName += ".epub";
      runner.expectTrue(LocalsendService::sanitizeFileName(longName.c_str(), out, sizeof(out)) && strlen(out) == 128 &&
                            strcmp(out + 123, ".epub") == 0,
                        "sanitize: long name keeps extension past the copy window");
    }
    {
      // The name is free of trailing dots and spaces before the extension search.
      std::string trailSpace(150, 'x');
      trailSpace += ".epub ";
      runner.expectTrue(LocalsendService::sanitizeFileName(trailSpace.c_str(), out, sizeof(out)) &&
                            strlen(out) == 128 && strcmp(out + 123, ".epub") == 0,
                        "sanitize: trailing space not part of extension");
      std::string trailDot(150, 'x');
      trailDot += ".epub.";
      runner.expectTrue(LocalsendService::sanitizeFileName(trailDot.c_str(), out, sizeof(out)) && strlen(out) == 128 &&
                            strcmp(out + 123, ".epub") == 0,
                        "sanitize: trailing dot does not shadow the extension");
    }
    {
      // A name that contains only continuation bytes becomes empty after truncation.
      std::string cont(130, '\x80');
      runner.expectTrue(!LocalsendService::sanitizeFileName(cont.c_str(), out, sizeof(out)),
                        "sanitize: truncation to empty rejected");
    }
    runner.expectTrue(LocalsendService::sanitizeFileName("normal-name_1.epub", out, sizeof(out)) &&
                          strcmp(out, "normal-name_1.epub") == 0,
                      "sanitize: plain name untouched");

    // Truncation keeps the extension when one fits.
    std::string longName(150, 'x');
    longName += ".epub";
    runner.expectTrue(LocalsendService::sanitizeFileName(longName.c_str(), out, sizeof(out)),
                      "sanitize: long name accepted");
    runner.expectTrue(strlen(out) == 128, "sanitize: long name capped at 128");
    runner.expectTrue(strcmp(out + 123, ".epub") == 0, "sanitize: extension preserved");
  }

  {
    // Collision naming walks "name (N).ext" and leaves index 0 untouched.
    char out[160];
    runner.expectTrue(
        LocalsendService::makeCollisionName("book.epub", out, sizeof(out), 0) && strcmp(out, "book.epub") == 0,
        "collision: index 0 verbatim");
    runner.expectTrue(
        LocalsendService::makeCollisionName("book.epub", out, sizeof(out), 1) && strcmp(out, "book (1).epub") == 0,
        "collision: index 1 before extension");
    runner.expectTrue(
        LocalsendService::makeCollisionName("noext", out, sizeof(out), 2) && strcmp(out, "noext (2)") == 0,
        "collision: no extension");
    runner.expectTrue(
        LocalsendService::makeCollisionName(".hidden", out, sizeof(out), 1) && strcmp(out, ".hidden (1)") == 0,
        "collision: leading dot is not an extension");
    runner.expectTrue(!LocalsendService::makeCollisionName("book.epub", out, 8, 1), "collision: overflow rejected");
  }

  {
    // The prepare response escapes a hostile id instead of emitting broken
    // JSON.
    LocalsendService svc = makeService();
    const LocalsendIncomingFile hostile[] = {file("a\"b\\c", "x.txt")};
    runner.expectTrue(svc.prepareUpload(hostile, 1, 0, kSenderIp) == LocalsendPrepareStatus::Ok, "escape: session ok");
    char resp[512];
    const size_t n = svc.buildPrepareResponse(resp, sizeof(resp));
    runner.expectTrue(n > 0, "escape: response fits");
    runner.expectTrue(strstr(resp, "a\\\"b\\\\c") != nullptr, "escape: quote and backslash escaped");
    runner.expectTrue(strstr(resp, "a\"b\\c\"") == nullptr, "escape: no raw id leaks");
  }

  {
    // Control bytes in an id break the escaped response's buffer worst
    // case, so prepareUpload rejects them.
    LocalsendService svc = makeService();
    const LocalsendIncomingFile badId[] = {file("a\tb", "x.txt")};
    runner.expectTrue(svc.prepareUpload(badId, 1, 0, kSenderIp) == LocalsendPrepareStatus::Invalid,
                      "id: control byte rejected");
    const LocalsendIncomingFile goodId[] = {file("a b\"c\\d", "x.txt")};
    runner.expectTrue(svc.prepareUpload(goodId, 1, 1, kSenderIp) == LocalsendPrepareStatus::Ok,
                      "id: spaces, quotes, backslashes accepted");
  }

  {
    // A long first upload must not expire the session for the next file:
    // markReceived refreshes the idle timer.
    LocalsendService svc = makeService();
    const LocalsendIncomingFile two[] = {file("f1", "a.bin"), file("f2", "b.bin")};
    svc.prepareUpload(two, 2, 0, kSenderIp);
    char resp[512];
    svc.buildPrepareResponse(resp, sizeof(resp));
    char sid[17] = {0};
    sscanf(resp, "{\"sessionId\":\"%16[^\"]\"}", sid);
    char tok1[17] = {0};
    sscanf(strstr(resp, "\"f1\":\"") + 6, "%16[^\"]", tok1);
    char tok2[17] = {0};
    sscanf(strstr(resp, "\"f2\":\"") + 6, "%16[^\"]", tok2);
    // The first upload starts at t=0 and finishes at t=90s (within the 120s
    // connection budget but past the 60s session timeout).
    runner.expectTrue(svc.validateUpload(sid, "f1", tok1, 0, kSenderIp) != nullptr,
                      "long transfer: first upload starts");
    svc.markReceived("f1", 90000);
    runner.expectTrue(svc.validateUpload(sid, "f2", tok2, 90001, kSenderIp) != nullptr,
                      "long transfer: session refreshed on completion");
  }

  {
    // Route matching is exact: shared prefixes do not dispatch.
    runner.expectTrue(matchLocalsendRoute("POST", "/api/localsend/v2/register") == LocalsendRoute::Register,
                      "route: register");
    runner.expectTrue(matchLocalsendRoute("POST", "/api/localsend/v2/registerX") == LocalsendRoute::Unknown,
                      "route: register prefix rejected");
    runner.expectTrue(matchLocalsendRoute("POST", "/api/localsend/v2/prepare-upload") == LocalsendRoute::PrepareUpload,
                      "route: prepare-upload");
    runner.expectTrue(matchLocalsendRoute("POST", "/api/localsend/v2/prepare-uploadX") == LocalsendRoute::Unknown,
                      "route: prepare-upload prefix rejected");
    runner.expectTrue(
        matchLocalsendRoute("POST", "/api/localsend/v2/upload?sessionId=a&fileId=b&token=c") == LocalsendRoute::Upload,
        "route: upload with query");
    runner.expectTrue(matchLocalsendRoute("POST", "/api/localsend/v2/upload") == LocalsendRoute::Unknown,
                      "route: upload without query rejected");
    runner.expectTrue(matchLocalsendRoute("POST", "/api/localsend/v2/cancel?sessionId=a") == LocalsendRoute::Cancel,
                      "route: cancel with query");
    runner.expectTrue(matchLocalsendRoute("GET", "/api/localsend/v2/info") == LocalsendRoute::Info, "route: info");
    runner.expectTrue(matchLocalsendRoute("GET", "/api/localsend/v2/infofoo") == LocalsendRoute::Unknown,
                      "route: info prefix rejected");
    runner.expectTrue(matchLocalsendRoute("POST", "/api/localsend/v2/info") == LocalsendRoute::Unknown,
                      "route: info requires GET");
    runner.expectTrue(matchLocalsendRoute("GET", "/other/path") == LocalsendRoute::Unknown,
                      "route: foreign prefix rejected");
  }

  {
    // The app reuses the request-line buffer for header lines before
    // dispatch: the parsed method and target must stay valid. A parser that
    // returns pointers into the line buffer fails this.
    char line[320];
    strcpy(line, "POST /api/localsend/v2/prepare-upload HTTP/1.1");
    char method[8];
    char target[MAX_TARGET_LEN + 1];
    runner.expectTrue(localsendParseRequestLine(line, method, sizeof(method), target, sizeof(target)),
                      "head: request line parses");

    LocalsendHeaderParser headers;
    strcpy(line, "Content-Length: 17");
    runner.expectTrue(headers.feed(line) == HeaderFeed::More, "head: header line feeds");
    strcpy(line, "");
    runner.expectTrue(headers.feed(line) == HeaderFeed::Done, "head: blank line ends headers");

    runner.expectTrue(matchLocalsendRoute(method, target) == LocalsendRoute::PrepareUpload,
                      "head: route survives header reads");
    runner.expectTrue(strcmp(method, "POST") == 0, "head: method survives header reads");
    runner.expectTrue(strcmp(target, "/api/localsend/v2/prepare-upload") == 0, "head: target survives header reads");
    runner.expectTrue(headers.contentLength == 17 && !headers.chunked, "head: headers parse");

    runner.expectTrue(!localsendParseRequestLine("no-spaces-here", method, sizeof(method), target, sizeof(target)),
                      "head: single token rejected");
    runner.expectTrue(
        !localsendParseRequestLine(" /leading-space HTTP/1.1", method, sizeof(method), target, sizeof(target)),
        "head: empty method rejected");
    runner.expectTrue(
        !localsendParseRequestLine("POST /missing-version", method, sizeof(method), target, sizeof(target)),
        "head: missing version rejected");
    runner.expectTrue(!localsendParseRequestLine("POST /x ", method, sizeof(method), target, sizeof(target)),
                      "head: empty version token rejected");
    runner.expectTrue(!localsendParseRequestLine("POST  /x HTTP/1.1", method, sizeof(method), target, sizeof(target)),
                      "head: empty target rejected");
    runner.expectTrue(
        !localsendParseRequestLine("OPTIONSLONG /x HTTP/1.1", method, sizeof(method), target, sizeof(target)),
        "head: overlong method rejected");
    runner.expectTrue(!localsendParseRequestLine("GET /x HTTP/1.1", method, sizeof(method), target, 2),
                      "head: overlong target rejected");
  }

  {
    // The header parser reports Done only on the blank terminator line. A
    // full cap without it is Overflow, which the app answers 400.
    LocalsendHeaderParser hp;
    runner.expectTrue(hp.feed("Content-Length: 123") == HeaderFeed::More, "headers: field");
    runner.expectTrue(hp.contentLength == 123, "headers: content-length parsed");
    runner.expectTrue(hp.feed("") == HeaderFeed::Done, "headers: blank line completes");

    LocalsendHeaderParser hp2;
    HeaderFeed last = HeaderFeed::More;
    for (int i = 0; i < LocalsendHeaderParser::MAX_LINES + 1; i++) last = hp2.feed("X-F: y");
    runner.expectTrue(last == HeaderFeed::Overflow, "headers: cap without terminator overflows");

    LocalsendHeaderParser hp3;
    runner.expectTrue(hp3.feed("Transfer-Encoding: chunked") == HeaderFeed::More && hp3.chunked,
                      "headers: chunked detected");
    LocalsendHeaderParser hp4;
    runner.expectTrue(hp4.feed("Transfer-Encoding: Chunked") == HeaderFeed::More && hp4.chunked,
                      "headers: coding is case-insensitive");
  }
  {
    const std::string wire =
        "4 \t; name = \"a;b\\\"c\" \t; flag; token=value\r\nWiki\r\n5\r\npedia\r\n0\r\nX-Checksum: ok\r\n\r\n";
    for (size_t split = 1; split <= wire.size(); split++) {
      const DecodeResult result = decodeChunks(wire, 9, split);
      runner.expectTrue(result.status == ChunkFeed::Complete && result.body == "Wikipedia",
                        "chunk: framing survives every input block size");
    }
    const DecodeResult empty = decodeChunks("0\r\n\r\n", 0, 1);
    runner.expectTrue(empty.status == ChunkFeed::Complete && empty.body.empty(), "chunk: empty file completes");

    const std::string payload(64, 'x');
    std::string inPlace = "40\r\n" + payload + "\r\n3\r\nend\r\n0\r\nX-Note:\tok\r\n\r\n";
    LocalsendChunkDecoder decoder(payload.size() + 3);
    size_t used = 0;
    size_t produced = 0;
    auto* data = reinterpret_cast<uint8_t*>(inPlace.data());
    const ChunkFeed inPlaceStatus = decoder.feed(data, inPlace.size(), &used, data, inPlace.size(), &produced);
    runner.expectTrue(inPlaceStatus == ChunkFeed::Complete && used == inPlace.size() &&
                          inPlace.substr(0, produced) == payload + "end",
                      "chunk: in-place decoding preserves overlapping payload and accepts trailer whitespace");

    std::string binaryWire = "3\r\n";
    binaryWire.append("a\0b", 3);
    binaryWire += "\r\n0\r\n\r\n";
    const DecodeResult binary = decodeChunks(binaryWire, 3, 2);
    runner.expectTrue(binary.status == ChunkFeed::Complete && binary.body == std::string("a\0b", 3),
                      "chunk: binary payload preserves NUL bytes");

    const std::string largePayload(5000, 'z');
    const std::string largeWire = "1388\r\n" + largePayload + "\r\n0\r\n\r\n";
    const DecodeResult large = decodeChunks(largeWire, largePayload.size(), 4096);
    runner.expectTrue(large.status == ChunkFeed::Complete && large.body == largePayload,
                      "chunk: chunk larger than network buffer decodes incrementally");

    LocalsendChunkDecoder capped(4);
    const char cappedInput[] = "4\r\nDATA\r\n0\r\n\r\n";
    uint8_t cappedOutput[2];
    size_t cappedUsed = 0;
    size_t cappedLen = 0;
    const ChunkFeed first = capped.feed(reinterpret_cast<const uint8_t*>(cappedInput), sizeof(cappedInput) - 1,
                                        &cappedUsed, cappedOutput, sizeof(cappedOutput), &cappedLen);
    size_t nextUsed = 0;
    size_t nextLen = 0;
    const ChunkFeed second =
        capped.feed(reinterpret_cast<const uint8_t*>(cappedInput) + cappedUsed, sizeof(cappedInput) - 1 - cappedUsed,
                    &nextUsed, cappedOutput, sizeof(cappedOutput), &nextLen);
    runner.expectTrue(first == ChunkFeed::NeedInput && cappedLen == 2 && second == ChunkFeed::Complete &&
                          nextLen == 2 && memcmp(cappedOutput, "TA", 2) == 0,
                      "chunk: output capacity resumes on next feed");

    const char* malformedExtensions[] = {"1;=x\r\n", "1;name=\r\n", "1;name=\"unterminated\r\n", "1;bad@name=x\r\n",
                                         "1;name=\"bad\\\r\n"};
    for (const char* malformed : malformedExtensions)
      runner.expectTrue(decodeChunks(malformed, 1, 8).status == ChunkFeed::Invalid,
                        "chunk: malformed extension rejected");

    runner.expectTrue(decodeChunks("Z\r\n", 1, 8).status == ChunkFeed::Invalid, "chunk: invalid size rejected");
    runner.expectTrue(decodeChunks("1\na\r\n0\r\n\r\n", 1, 8).status == ChunkFeed::Invalid,
                      "chunk: LF-only size line rejected");
    runner.expectTrue(decodeChunks("1\r\naX\n", 1, 8).status == ChunkFeed::Invalid,
                      "chunk: invalid data terminator rejected");
    runner.expectTrue(decodeChunks("10000000000000000\r\n", 1, 32).status == ChunkFeed::Invalid,
                      "chunk: overflowing size rejected");
    std::string tooLongMetadata(128, 'a');
    tooLongMetadata += "\r\n";
    runner.expectTrue(decodeChunks(tooLongMetadata, 1, 32).status == ChunkFeed::Invalid,
                      "chunk: metadata line length is bounded");
    runner.expectTrue(decodeChunks("3\r\nab", 3, 2).status == ChunkFeed::NeedInput,
                      "chunk: truncated payload never completes");
    runner.expectTrue(decodeChunks("1", 1, 1).status == ChunkFeed::NeedInput,
                      "chunk: truncated size line never completes");
    runner.expectTrue(decodeChunks("1\r\na\r\n", 1, 32).status == ChunkFeed::NeedInput,
                      "chunk: missing terminal chunk never completes");
    runner.expectTrue(decodeChunks("0\r\nX: y\r\n", 0, 4).status == ChunkFeed::NeedInput,
                      "chunk: missing trailer terminator never completes");
    const DecodeResult overrun = decodeChunks("3\r\nabc\r\n0\r\n\r\n", 2, 16);
    runner.expectTrue(overrun.status == ChunkFeed::Invalid && overrun.body.empty(),
                      "chunk: excess chunk rejected before payload emission");
    runner.expectTrue(decodeChunks("2\r\nab\r\n0\r\n\r\n", 3, 4).status == ChunkFeed::Invalid,
                      "chunk: short decoded body rejected at terminator");
    std::string tooManyTrailers = "0\r\n";
    for (int i = 0; i < 33; i++) tooManyTrailers += "X: y\r\n";
    tooManyTrailers += "\r\n";
    runner.expectTrue(decodeChunks(tooManyTrailers, 0, 32).status == ChunkFeed::Invalid,
                      "chunk: trailer line count is bounded");
  }

  {
    LocalsendHeaderParser cl;
    runner.expectTrue(cl.feed("Content-Length: 17") == HeaderFeed::More && cl.contentLength == 17 && !cl.invalid,
                      "headers: Content-Length framing remains supported");
    runner.expectTrue(cl.feed("") == HeaderFeed::Done, "headers: Content-Length terminates");

    LocalsendHeaderParser ambiguous;
    ambiguous.feed("Content-Length: 17");
    ambiguous.feed("Transfer-Encoding: chunked");
    runner.expectTrue(ambiguous.invalid, "headers: ambiguous Content-Length and Transfer-Encoding rejected");
    LocalsendHeaderParser malformed;
    malformed.feed("Content-Length: 18446744073709551616");
    runner.expectTrue(malformed.invalid, "headers: overflowing Content-Length rejected");
    LocalsendHeaderParser reverseAmbiguous;
    reverseAmbiguous.feed("Transfer-Encoding: chunked");
    reverseAmbiguous.feed("Content-Length: 17");
    runner.expectTrue(reverseAmbiguous.invalid, "headers: reverse framing ambiguity rejected");
    LocalsendHeaderParser duplicateLength;
    duplicateLength.feed("Content-Length: 17");
    duplicateLength.feed("Content-Length: 17");
    runner.expectTrue(duplicateLength.invalid, "headers: duplicate Content-Length rejected");
    LocalsendHeaderParser unsupported;
    unsupported.feed("Transfer-Encoding: gzip, chunked");
    runner.expectTrue(unsupported.invalid, "headers: unsupported transfer coding rejected");
  }

  {
    // bodyDest: accumulate mode appends at the written offset; a full buffer
    // fails instead of writing out of bounds.
    uint8_t buf[16];
    uint8_t* dst = nullptr;
    size_t room = 0;
    runner.expectTrue(bodyDest(buf, sizeof(buf), 0, &dst, &room) && dst == buf && room == 16,
                      "bodyDest: empty buffer starts at base");
    runner.expectTrue(bodyDest(buf, sizeof(buf), 9, &dst, &room) && dst == buf + 9 && room == 7,
                      "bodyDest: offset resumes mid-buffer");
    runner.expectTrue(!bodyDest(buf, sizeof(buf), 16, &dst, &room), "bodyDest: full buffer rejected");
  }

  {
    // Reason phrases: a numeric status must never ship with a wrong phrase.
    runner.expectTrue(strcmp(reasonPhrase(200), "OK") == 0, "reason: 200");
    runner.expectTrue(strcmp(reasonPhrase(404), "Not Found") == 0, "reason: 404");
    runner.expectTrue(strcmp(reasonPhrase(409), "Conflict") == 0, "reason: 409");
    runner.expectTrue(strcmp(reasonPhrase(599), "OK") == 0, "reason: unknown falls back");
  }

  {
    // Senders form-encode ids in the upload query; raw bytes do not match.
    char out[80];
    runner.expectTrue(queryParam("/api/localsend/v2/upload?sessionId=a&fileId=some%20file%20id&token=b", "fileId", out,
                                 sizeof(out)) &&
                          strcmp(out, "some file id") == 0,
                      "query: percent-decoded id");
    runner.expectTrue(queryParam("/x?token=ab%2Fcd", "token", out, sizeof(out)) && strcmp(out, "ab/cd") == 0,
                      "query: encoded slash");
    runner.expectTrue(queryParam("/x?token=a%2Bb+c", "token", out, sizeof(out)) && strcmp(out, "a+b c") == 0,
                      "query: form-urlencoding (+ is space, %2B is plus)");
    runner.expectTrue(!queryParam("/x?token=ab%", "token", out, sizeof(out)), "query: trailing percent rejected");
    runner.expectTrue(!queryParam("/x?token=ab%2", "token", out, sizeof(out)), "query: short escape rejected");
    runner.expectTrue(!queryParam("/x?token=ab%zz", "token", out, sizeof(out)), "query: bad hex rejected");
    runner.expectTrue(!queryParam("/x?token=a%00b", "token", out, sizeof(out)), "query: NUL escape rejected");
    runner.expectTrue(!queryParam("/x?a=1", "missing", out, sizeof(out)), "query: absent key");
    runner.expectTrue(!queryParam("/x?a=&b=1", "a", out, 0), "query: zero capacity rejected");
    runner.expectTrue(!queryParam(nullptr, "a", out, sizeof(out)), "query: null target rejected");
    runner.expectTrue(!queryParam("/x?a=1", nullptr, out, sizeof(out)), "query: null key rejected");
    runner.expectTrue(!queryParam("/x?a=1", "a", nullptr, sizeof(out)), "query: null output rejected");
  }

  {
    // The session ends when every accepted file arrives: the next prepare
    // does not hit 409. A partial session stays open.
    LocalsendService svc = makeService();
    const LocalsendIncomingFile two[] = {file("f1", "a.bin"), file("f2", "b.bin")};
    svc.prepareUpload(two, 2, 0, kSenderIp);
    svc.markReceived("f1", 1000);
    const LocalsendIncomingFile one[] = {file("g1", "c.bin")};
    runner.expectTrue(svc.prepareUpload(one, 1, 1001, kSenderIp) == LocalsendPrepareStatus::Busy,
                      "completion: partial session stays busy");
    svc.markReceived("f2", 2000);
    runner.expectTrue(svc.prepareUpload(one, 1, 2001, kSenderIp) == LocalsendPrepareStatus::Ok,
                      "completion: last file frees the session");
  }

  {
    // Duplicate effective ids (the app prefers the inner id over the unique
    // map key) marks both files received at once. Later ones are skipped.
    LocalsendService svc = makeService();
    const LocalsendIncomingFile dups[] = {file("dup", "a.bin"), file("dup", "b.bin"), file("solo", "c.bin")};
    runner.expectTrue(svc.prepareUpload(dups, 3, 0, kSenderIp) == LocalsendPrepareStatus::Ok, "dup: request accepted");
    char resp[1024];
    svc.buildPrepareResponse(resp, sizeof(resp));
    const char* first = strstr(resp, "\"dup\"");
    runner.expectTrue(first != nullptr && strstr(first + 5, "\"dup\"") == nullptr, "dup: id appears once");
    runner.expectTrue(strstr(resp, "\"solo\"") != nullptr, "dup: unique file kept");
    svc.markReceived("dup", 100);
    char sid[17] = {0};
    sscanf(resp, "{\"sessionId\":\"%16[^\"]\"}", sid);
    char tokSolo[17] = {0};
    sscanf(strstr(resp, "\"solo\":\"") + 8, "%16[^\"]", tokSolo);
    runner.expectTrue(svc.validateUpload(sid, "solo", tokSolo, 101, kSenderIp) != nullptr,
                      "dup: solo file still uploadable");
  }

  {
    // Oversize declarations are rejected at prepare, not after a token was
    // issued. Boundary: exactly the cap is fine, cap+1 is skipped.
    LocalsendService svc = makeService();
    const LocalsendIncomingFile atCap = {"at", "at.bin", LocalsendService::MAX_FILE_BYTES};
    const LocalsendIncomingFile overCap = {"over", "over.bin", LocalsendService::MAX_FILE_BYTES + 1};
    const LocalsendIncomingFile files[] = {atCap, overCap};
    runner.expectTrue(svc.prepareUpload(files, 2, 0, kSenderIp) == LocalsendPrepareStatus::Ok, "cap: request accepted");
    char resp[1024];
    svc.buildPrepareResponse(resp, sizeof(resp));
    runner.expectTrue(strstr(resp, "\"at\"") != nullptr, "cap: exact cap accepted");
    runner.expectTrue(strstr(resp, "\"over\"") == nullptr, "cap: cap+1 skipped");

    LocalsendService svc2 = makeService();
    runner.expectTrue(svc2.prepareUpload(&overCap, 1, 0, kSenderIp) == LocalsendPrepareStatus::Invalid,
                      "cap: only oversize files is invalid");
  }

  {
    // The session caps at MAX_FILES: the 17th id gets no token.
    LocalsendService svc = makeService();
    char ids[LocalsendService::MAX_FILES + 1][8];
    LocalsendIncomingFile many[LocalsendService::MAX_FILES + 1];
    for (int i = 0; i <= LocalsendService::MAX_FILES; i++) {
      snprintf(ids[i], sizeof(ids[i]), "f%d", i);
      many[i] = file(ids[i], "n.bin");
    }
    runner.expectTrue(
        svc.prepareUpload(many, LocalsendService::MAX_FILES + 1, 0, kSenderIp) == LocalsendPrepareStatus::Ok,
        "files cap: request accepted");
    char resp[3072];
    svc.buildPrepareResponse(resp, sizeof(resp));
    runner.expectTrue(strstr(resp, "\"f15\"") != nullptr, "files cap: 16th accepted");
    runner.expectTrue(strstr(resp, "\"f16\"") == nullptr, "files cap: 17th skipped");
  }

  {
    // The session binds to the prepare sender's address: upload and cancel
    // from another address fail (LocalSend v2: 403 for an invalid token or
    // IP; the link is plain HTTP).
    const char* kOtherIp = "192.168.1.101";
    LocalsendService svc = makeService();
    const LocalsendIncomingFile one[] = {file("f1", "a.bin")};
    svc.prepareUpload(one, 1, 0, kSenderIp);
    char resp[512];
    svc.buildPrepareResponse(resp, sizeof(resp));
    char sid[17] = {0};
    sscanf(resp, "{\"sessionId\":\"%16[^\"]\"}", sid);
    char tok[17] = {0};
    sscanf(strstr(resp, "\"f1\":\"") + 6, "%16[^\"]", tok);
    runner.expectTrue(svc.validateUpload(sid, "f1", tok, 1, kOtherIp) == nullptr, "ip: other host rejected");
    runner.expectTrue(svc.validateUpload(sid, "f1", tok, 2, kSenderIp) != nullptr, "ip: prepare host accepted");
    runner.expectTrue(!svc.cancel(sid, kOtherIp), "ip: cancel from other host rejected");
    runner.expectTrue(svc.cancel(sid, kSenderIp), "ip: cancel from prepare host accepted");

    // A 32-bit view merges distinct IPv6 peers; full strings keep them
    // distinct. An empty conversion result never binds a session.
    const char* kV6A = "fe80::1";
    const char* kV6B = "fe80::2";
    LocalsendService svc6 = makeService();
    svc6.prepareUpload(one, 1, 0, kV6A);
    char resp6[512];
    svc6.buildPrepareResponse(resp6, sizeof(resp6));
    char sid6[17] = {0};
    sscanf(resp6, "{\"sessionId\":\"%16[^\"]\"}", sid6);
    char tok6[17] = {0};
    sscanf(strstr(resp6, "\"f1\":\"") + 6, "%16[^\"]", tok6);
    runner.expectTrue(svc6.validateUpload(sid6, "f1", tok6, 1, kV6B) == nullptr, "ip: IPv6 other host rejected");
    runner.expectTrue(svc6.validateUpload(sid6, "f1", tok6, 2, kV6A) != nullptr, "ip: IPv6 prepare host accepted");
    runner.expectTrue(svc6.validateUpload(sid6, "f1", tok6, 3, "") == nullptr, "ip: empty client rejected");

    LocalsendService svc0 = makeService();
    runner.expectTrue(svc0.prepareUpload(one, 1, 0, "") == LocalsendPrepareStatus::Invalid,
                      "ip: empty sender rejected");
    runner.expectTrue(svc0.prepareUpload(one, 1, 0, nullptr) == LocalsendPrepareStatus::Invalid,
                      "ip: null sender rejected");
  }

  {
    // A received file no longer validates: a retry writes no duplicate,
    // while a pending file in the same session still uploads.
    LocalsendService svc = makeService();
    const LocalsendIncomingFile two[] = {file("f1", "a.bin"), file("f2", "b.bin")};
    svc.prepareUpload(two, 2, 0, kSenderIp);
    char resp[512];
    svc.buildPrepareResponse(resp, sizeof(resp));
    char sid[17] = {0};
    sscanf(resp, "{\"sessionId\":\"%16[^\"]\"}", sid);
    char tok1[17] = {0};
    sscanf(strstr(resp, "\"f1\":\"") + 6, "%16[^\"]", tok1);
    char tok2[17] = {0};
    sscanf(strstr(resp, "\"f2\":\"") + 6, "%16[^\"]", tok2);
    svc.markReceived("f1", 100);
    runner.expectTrue(svc.validateUpload(sid, "f1", tok1, 101, kSenderIp) == nullptr,
                      "received: completed file rejected");
    runner.expectTrue(svc.validateUpload(sid, "f2", tok2, 102, kSenderIp) != nullptr,
                      "received: pending file still validates");
  }

  {
    // Truncation never splits a UTF-8 codepoint. 125 ASCII bytes + euro
    // sign (bytes 125..127) + ".x" = 130 bytes: the extension cut lands at
    // 126, inside the euro sign, so the base ends at 125.
    std::string name(125, 'a');
    name += "\xE2\x82\xAC";
    name += ".x";
    char out[160];
    runner.expectTrue(LocalsendService::sanitizeFileName(name.c_str(), out, sizeof(out)),
                      "utf8: long multibyte name sanitized");
    runner.expectTrue(strlen(out) == 127, "utf8: base 125 + extension 2");
    runner.expectTrue(strcmp(out + 125, ".x") == 0, "utf8: extension kept");
    runner.expectTrue(out[124] == 'a', "utf8: base ends on ASCII");

    // No-extension branch: 127 ASCII bytes + euro sign = 130 bytes. The cut
    // at 128 lands inside the euro sign, so it backs off to 127.
    std::string bare(127, 'b');
    bare += "\xE2\x82\xAC";
    runner.expectTrue(LocalsendService::sanitizeFileName(bare.c_str(), out, sizeof(out)),
                      "utf8: no-extension name sanitized");
    runner.expectTrue(strlen(out) == 127, "utf8: cut backed off the codepoint");
    runner.expectTrue(out[126] == 'b', "utf8: ends on ASCII");
  }

  {
    // The worst-case upload target (percent-encoded 64-byte file id) is
    // parsed and decoded in full.
    std::string id(64, ' ');
    std::string target = "/api/localsend/v2/upload?sessionId=0123456789abcdef&fileId=";
    for (char c : id) {
      char enc[4];
      snprintf(enc, sizeof(enc), "%%%02X", static_cast<unsigned char>(c));
      target += enc;
    }
    target += "&token=0123456789abcdef";
    runner.expectTrue(target.size() == MAX_TARGET_LEN, "target: constant matches worst case");
    runner.expectTrue(matchLocalsendRoute("POST", target.c_str()) == LocalsendRoute::Upload,
                      "target: worst case routes to upload");
    char decoded[65];
    runner.expectTrue(
        queryParam(target.c_str(), "fileId", decoded, sizeof(decoded)) && strcmp(decoded, id.c_str()) == 0,
        "target: worst-case id decodes");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
