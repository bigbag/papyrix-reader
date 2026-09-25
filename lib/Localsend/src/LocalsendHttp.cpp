#include "LocalsendHttp.h"

#include <strings.h>

#include <cstring>

namespace {
constexpr char API_PREFIX[] = "/api/localsend/v2/";
constexpr size_t API_PREFIX_LEN = sizeof(API_PREFIX) - 1;
}  // namespace
const char* reasonPhrase(int code) {
  switch (code) {
    case 200:
      return "OK";
    case 204:
      return "No Content";
    case 400:
      return "Bad Request";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 409:
      return "Conflict";
    case 500:
      return "Internal Server Error";
    default:
      return "OK";
  }
}

namespace {
int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool isOWS(char c) { return c == ' ' || c == '\t'; }

bool parseContentLength(const char* value, uint64_t* result) {
  while (isOWS(*value)) value++;
  if (*value < '0' || *value > '9') return false;
  uint64_t n = 0;
  do {
    const uint8_t digit = static_cast<uint8_t>(*value - '0');
    if (n > (UINT64_MAX - digit) / 10) return false;
    n = n * 10 + digit;
    value++;
  } while (*value >= '0' && *value <= '9');
  while (isOWS(*value)) value++;
  if (*value != 0) return false;
  *result = n;
  return true;
}

bool isTokenChar(unsigned char c) {
  return c != 0 && ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    strchr("!#$%&'*+-.^_`|~", c) != nullptr);
}

bool parseChunkExtensions(const char* line, size_t* index) {
  size_t i = *index;
  while (true) {
    const size_t beforeWhitespace = i;
    while (isOWS(line[i])) i++;
    if (line[i] != ';') {
      if (i != beforeWhitespace || line[i] != 0) return false;
      *index = i;
      return true;
    }
    i++;
    while (isOWS(line[i])) i++;
    const size_t name = i;
    while (isTokenChar(static_cast<unsigned char>(line[i]))) i++;
    if (i == name) return false;
    size_t equals = i;
    while (isOWS(line[equals])) equals++;
    if (line[equals] == '=') {
      i = equals + 1;
      while (isOWS(line[i])) i++;
      if (line[i] == '"') {
        i++;
        bool closed = false;
        while (line[i]) {
          const unsigned char c = static_cast<unsigned char>(line[i++]);
          if (c == '"') {
            closed = true;
            break;
          }
          if (c == '\\') {
            const unsigned char escaped = static_cast<unsigned char>(line[i++]);
            if (!escaped || (escaped != '\t' && (escaped < 32 || escaped == 127))) return false;
          } else if (!(c == '\t' || c == ' ' || c == '!' || (c >= '#' && c <= '[') || (c >= ']' && c <= '~') ||
                       c >= 128)) {
            return false;
          }
        }
        if (!closed) return false;
      } else {
        const size_t value = i;
        while (isTokenChar(static_cast<unsigned char>(line[i]))) i++;
        if (i == value) return false;
      }
    }
  }
}

bool validTrailer(const char* line) {
  const char* colon = strchr(line, ':');
  if (!colon || colon == line) return false;
  for (const char* p = line; p < colon; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (!(c >= '0' && c <= '9') && !(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') &&
        strchr("!#$%&'*+-.^_`|~", c) == nullptr)
      return false;
  }
  for (const char* p = colon + 1; *p; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if ((c < 32 && c != '\t') || c == 127) return false;
  }
  return true;
}
}  // namespace

ChunkFeed LocalsendChunkDecoder::feed(const uint8_t* input, size_t inputLen, size_t* inputUsed, uint8_t* output,
                                      size_t outputCap, size_t* outputLen) {
  if (!inputUsed || !outputLen || (!input && inputLen) || (!output && outputCap)) return ChunkFeed::Invalid;
  *inputUsed = 0;
  *outputLen = 0;
  if (state_ == State::Failed) return ChunkFeed::Invalid;
  if (state_ == State::Done) return ChunkFeed::Complete;

  auto fail = [&]() {
    state_ = State::Failed;
    return ChunkFeed::Invalid;
  };
  while (*inputUsed < inputLen) {
    if (state_ == State::Data) {
      const size_t room = outputCap - *outputLen;
      if (room == 0) return ChunkFeed::NeedInput;
      const size_t take =
          chunkRemaining_ < inputLen - *inputUsed ? static_cast<size_t>(chunkRemaining_) : inputLen - *inputUsed;
      const size_t copy = take < room ? take : room;
      memmove(output + *outputLen, input + *inputUsed, copy);
      *inputUsed += copy;
      *outputLen += copy;
      chunkRemaining_ -= copy;
      decodedBytes_ += copy;
      if (chunkRemaining_ == 0) state_ = State::DataCR;
      continue;
    }

    const char c = static_cast<char>(input[(*inputUsed)++]);
    if (state_ == State::DataCR) {
      if (c != '\r') return fail();
      state_ = State::DataLF;
      continue;
    }
    if (state_ == State::DataLF) {
      if (c != '\n') return fail();
      state_ = State::Line;
      continue;
    }
    if (state_ == State::Line || state_ == State::Trailers) {
      if (sawCR_) {
        if (c != '\n') return fail();
        sawCR_ = false;
        line_[lineLen_] = 0;
        if (state_ == State::Trailers) {
          if (lineLen_ == 0) {
            if (decodedBytes_ != expectedBytes_) return fail();
            state_ = State::Done;
            return ChunkFeed::Complete;
          }
          if (++trailerLines_ > 32 || !validTrailer(line_)) return fail();
        } else {
          size_t i = 0;
          uint64_t size = 0;
          while (hexDigit(line_[i]) >= 0) {
            const int digit = hexDigit(line_[i++]);
            if (size > (UINT64_MAX - static_cast<unsigned>(digit)) / 16) return fail();
            size = size * 16 + static_cast<unsigned>(digit);
          }
          if (i == 0 || !parseChunkExtensions(line_, &i)) return fail();
          lineLen_ = 0;
          if (size == 0) {
            state_ = State::Trailers;
          } else {
            if (decodedBytes_ > expectedBytes_ || size > expectedBytes_ - decodedBytes_) return fail();
            chunkRemaining_ = size;
            state_ = State::Data;
          }
          continue;
        }
        lineLen_ = 0;
        continue;
      }
      if (c == '\r') {
        sawCR_ = true;
      } else {
        if ((c < 32 && c != '\t') || c == 127 || lineLen_ + 1 >= sizeof(line_)) return fail();
        line_[lineLen_++] = c;
      }
      continue;
    }
  }
  return state_ == State::Done ? ChunkFeed::Complete : ChunkFeed::NeedInput;
}

bool queryParam(const char* target, const char* key, char* out, size_t cap) {
  if (!target || !key || !out || cap == 0) return false;
  const char* q = strchr(target, '?');
  if (!q) return false;
  q++;
  const size_t keyLen = strlen(key);
  while (*q) {
    if (strncmp(q, key, keyLen) == 0 && q[keyLen] == '=') {
      q += keyLen + 1;
      size_t n = 0;
      while (*q && *q != '&') {
        char c = *q++;
        if (c == '+') {
          c = ' ';  // form-urlencoding, as in LocalSend's own server
        } else if (c == '%') {
          if (q[0] == 0 || q[1] == 0) return false;
          const int hi = hexDigit(q[0]);
          const int lo = hexDigit(q[1]);
          const int v = hi >= 0 && lo >= 0 ? hi * 16 + lo : 0;
          if (v < 1) return false;  // malformed escape or NUL
          c = static_cast<char>(v);
          q += 2;
        }
        if (n + 1 >= cap) return false;
        out[n++] = c;
      }
      out[n] = 0;
      return true;
    }
    q = strchr(q, '&');
    if (!q) return false;
    q++;
  }
  return false;
}

bool localsendParseRequestLine(const char* line, char* method, size_t methodCap, char* target, size_t targetCap) {
  if (!line || !method || !target || methodCap == 0 || targetCap == 0) return false;
  const char* sp1 = strchr(line, ' ');
  if (!sp1) return false;
  const char* sp2 = strchr(sp1 + 1, ' ');
  if (!sp2 || sp2[1] == 0) return false;
  const size_t methodLen = static_cast<size_t>(sp1 - line);
  const size_t targetLen = static_cast<size_t>(sp2 - (sp1 + 1));
  if (methodLen == 0 || methodLen >= methodCap || targetLen == 0 || targetLen >= targetCap) return false;
  memcpy(method, line, methodLen);
  method[methodLen] = 0;
  memcpy(target, sp1 + 1, targetLen);
  target[targetLen] = 0;
  return true;
}

LocalsendRoute matchLocalsendRoute(const char* method, const char* target) {
  if (!method || !target) return LocalsendRoute::Unknown;
  if (strncmp(target, API_PREFIX, API_PREFIX_LEN) != 0) return LocalsendRoute::Unknown;
  const char* route = target + API_PREFIX_LEN;
  const bool post = strcmp(method, "POST") == 0;
  if (post && strcmp(route, "register") == 0) return LocalsendRoute::Register;
  if (post && strcmp(route, "prepare-upload") == 0) return LocalsendRoute::PrepareUpload;
  if (post && strncmp(route, "upload?", 7) == 0) return LocalsendRoute::Upload;
  if (post && strncmp(route, "cancel?", 7) == 0) return LocalsendRoute::Cancel;
  if (strcmp(method, "GET") == 0 && strcmp(route, "info") == 0) return LocalsendRoute::Info;
  return LocalsendRoute::Unknown;
}

HeaderFeed LocalsendHeaderParser::feed(const char* line) {
  if (!line) return HeaderFeed::Overflow;
  if (line[0] == 0) return HeaderFeed::Done;
  if (lines_ >= MAX_LINES) return HeaderFeed::Overflow;
  lines_++;
  if (strncasecmp(line, "Content-Length:", 15) == 0) {
    uint64_t length = 0;
    if (hasContentLength_ || !parseContentLength(line + 15, &length)) {
      invalid = true;
    } else {
      contentLength = length;
      hasContentLength_ = true;
    }
  } else if (strncasecmp(line, "Transfer-Encoding:", 18) == 0) {
    const char* value = line + 18;
    while (isOWS(*value)) value++;
    size_t len = strlen(value);
    while (len > 0 && isOWS(value[len - 1])) len--;
    if (hasTransferEncoding_ || hasContentLength_ || len != 7 || strncasecmp(value, "chunked", 7) != 0) {
      invalid = true;
    } else {
      chunked = true;
      hasTransferEncoding_ = true;
    }
  }
  if (hasContentLength_ && hasTransferEncoding_) invalid = true;
  return HeaderFeed::More;
}

bool bodyDest(uint8_t* buf, size_t bufLen, uint64_t offset, uint8_t** dst, size_t* room) {
  if (!buf || !dst || !room || offset >= bufLen) return false;
  *dst = buf + offset;
  *room = bufLen - static_cast<size_t>(offset);
  return true;
}
