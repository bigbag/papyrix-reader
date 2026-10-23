#include <ProgressiveJpeg.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "test_utils.h"

using ProgressiveJpeg::Result;

namespace {
std::string readFixture(const char* name) {
  std::ifstream file(std::string(PROGRESSIVE_FIXTURES) + "/" + name, std::ios::binary);
  assert(file);
  return {std::istreambuf_iterator<char>(file), {}};
}

struct Decoded {
  Result result = Result::InvalidData;
  ProgressiveJpeg::ImageInfo info;
  std::vector<uint8_t> pixels;
};

bool collect(void* user, uint16_t y, const uint8_t* gray, uint16_t width, uint16_t rows, uint16_t stride) {
  auto& decoded = *static_cast<Decoded*>(user);
  if (width != decoded.info.width || stride < width || decoded.pixels.size() != size_t(y) * width) return false;
  for (uint16_t row = 0; row < rows; ++row) {
    decoded.pixels.insert(decoded.pixels.end(), gray + size_t(row) * stride, gray + size_t(row) * stride + width);
  }
  return true;
}

Decoded decodeBytes(const std::string& bytes, ProgressiveJpeg::AbortCallback abort = nullptr, void* user = nullptr) {
  FsFile file;
  file.setBuffer(bytes);
  file.seek(2);
  Decoded decoded;
  decoded.result = ProgressiveJpeg::probe(file, decoded.info, abort, user);
  assert(file.position() == 2);
  if (decoded.result != Result::Ok) return decoded;
  std::vector<uint8_t> workspace(ProgressiveJpeg::workspaceBytes(decoded.info));
  const ProgressiveJpeg::DecodeOptions options{workspace.data(), workspace.size(), abort, user};
  decoded.result = ProgressiveJpeg::decode(file, decoded.info, options, collect, &decoded);
  assert(file.position() == 2);
  return decoded;
}

struct Segment {
  size_t offset, end;
  unsigned marker;
};
std::vector<Segment> segments(const std::string& bytes) {
  std::vector<Segment> result;
  size_t p = 2;
  while (p < bytes.size()) {
    if (uint8_t(bytes[p]) != 255) {
      ++p;
      continue;
    }
    const size_t start = p++;
    while (p < bytes.size() && uint8_t(bytes[p]) == 255) ++p;
    assert(p < bytes.size());
    const unsigned marker = uint8_t(bytes[p++]);
    if (marker == 0 || (marker >= 0xD0 && marker <= 0xD7)) continue;
    if (marker == 0xD9) break;
    assert(p + 2 <= bytes.size());
    const size_t length = (uint8_t(bytes[p]) << 8) | uint8_t(bytes[p + 1]);
    assert(length >= 2 && length <= bytes.size() - p);
    result.push_back({start, p + length, marker});
    p += length;
  }
  return result;
}

std::string padTo(std::string bytes, size_t length) {
  while (bytes.size() < length) {
    const size_t added = std::min<size_t>(length - bytes.size(), 65537);
    assert(added >= 4);
    std::string comment(added, '\0');
    comment[0] = char(255);
    comment[1] = char(0xFE);
    comment[2] = char((added - 2) >> 8);
    comment[3] = char(added - 2);
    bytes.insert(2, comment);
  }
  return bytes;
}
}  // namespace

int main() {
  TestUtils::TestRunner runner("ProgressiveJpeg");
  for (const char* name : {"color-420", "gray", "color-444-restart"}) {
    const std::string bytes = readFixture((std::string(name) + ".jpg").c_str());
    const auto decoded = decodeBytes(bytes);
    runner.expectTrue(decoded.result == Result::Ok, std::string(name) + " decodes");
    std::istringstream reference(readFixture((std::string(name) + ".y.pgm").c_str()));
    std::string magic;
    int width, height, maximum;
    reference >> magic >> width >> height >> maximum;
    reference.get();
    const std::string pixels{std::istreambuf_iterator<char>(reference), {}};
    assert(magic == "P5" && maximum == 255 && pixels.size() == size_t(width * height));
    runner.expectEq(width, int(decoded.info.width), "reference width");
    runner.expectEq(height, int(decoded.info.height), "reference height");
    bool matches = pixels.size() == decoded.pixels.size();
    int largest = 0;
    if (matches) {
      for (size_t i = 0; i < pixels.size(); ++i) {
        largest = std::max(largest, std::abs(int(uint8_t(pixels[i])) - decoded.pixels[i]));
      }
      matches = largest <= 1;
    }
    runner.expectTrue(matches, "all grayscale pixels match independent reference", std::to_string(largest));
    runner.expectTrue(bytes.size() % 512 != 0, "fixture exercises a partial final buffer");
  }
  for (const auto& fixture : std::vector<std::pair<const char*, uint8_t>>{{"black.jpg", 0},
                                                                          {"white.jpg", 255},
                                                                          {"gray-maximum.jpg", 128},
                                                                          {"color-maximum.jpg", 128},
                                                                          {"gray-32-scans.jpg", 128}}) {
    const auto decoded = decodeBytes(readFixture(fixture.first));
    runner.expectTrue(decoded.result == Result::Ok, std::string(fixture.first) + " decodes");
    runner.expectEq(size_t(decoded.info.width) * decoded.info.height, decoded.pixels.size(),
                    "all constant rows emitted");
    runner.expectTrue(
        std::all_of(decoded.pixels.begin(), decoded.pixels.end(), [&](uint8_t p) { return p == fixture.second; }),
        "constant pixels retain their level");
  }
  runner.expectTrue(decodeBytes(readFixture("gray-33-scans.jpg")).result == Result::Unsupported,
                    "scan limit rejects 33 scans");
  const std::string valid = readFixture("color-420.jpg");
  const auto markers = segments(valid);
  const auto frame = *std::find_if(markers.begin(), markers.end(), [](const Segment& s) { return s.marker == 0xC2; });
  const auto scan = *std::find_if(markers.begin(), markers.end(), [](const Segment& s) { return s.marker == 0xDA; });
  const auto quant = *std::find_if(markers.begin(), markers.end(), [](const Segment& s) { return s.marker == 0xDB; });
  auto reject = [&](const std::string& bytes, const char* name) {
    runner.expectTrue(decodeBytes(bytes).result != Result::Ok, name);
  };
  reject(valid.substr(0, valid.size() - 2), "missing EOI fails");
  reject(valid.substr(0, scan.end), "missing entropy fails");
  std::string bad = valid;
  bad.erase(
      scan.end,
      markers[std::find_if(markers.begin(), markers.end(), [&](const Segment& s) { return s.offset == scan.offset; }) -
              markers.begin() + 1]
              .offset -
          scan.end);
  reject(bad, "empty entropy with EOI fails");
  bad = valid;
  bad[frame.offset + 11] = 0x11;
  bad.insert(scan.offset, valid.substr(frame.offset, frame.end - frame.offset));
  const auto repeated = decodeBytes(bad);
  runner.expectTrue(repeated.result == Result::InvalidData && repeated.pixels.empty(),
                    "repeated frame cannot change workspace geometry");
  bad = valid;
  bad[scan.offset + 6] = 0x40;
  reject(bad, "table selector cannot alias table zero");
  bad = valid;
  bad[quant.offset + 5] = 0;
  reject(bad, "zero quantizer fails");
  bad = valid;
  bad.erase(quant.offset, quant.end - quant.offset);
  reject(bad, "missing quantizer fails");
  bad = valid;
  bad[frame.offset + 13] = bad[frame.offset + 10];
  reject(bad, "duplicate frame IDs fail");
  bad = valid;
  bad[scan.offset + 7] = bad[scan.offset + 5];
  reject(bad, "duplicate scan selectors fail");
  bad = valid;
  bad[scan.end - 1] = 0x10;
  reject(bad, "refinement before first transmission fails");
  bad = valid;
  bad.insert(scan.offset, valid.substr(scan.offset, scan.end - scan.offset));
  reject(bad, "repeated first transmission fails");
  const auto padded = padTo(valid, 512 * 1024);
  const auto exact = decodeBytes(padded);
  runner.expectTrue(exact.result == Result::Ok && exact.pixels == decodeBytes(valid).pixels,
                    "exact input size limit preserves pixels");
  runner.expectTrue(decodeBytes(padded + " ").result == Result::Unsupported, "oversized input fails");
  runner.expectTrue(decodeBytes(valid, [](void*) { return true; }).result == Result::Aborted, "immediate abort fails");
  {
    FsFile input;
    input.setBuffer(valid);
    input.seek(2);
    ProgressiveJpeg::ImageInfo info;
    assert(ProgressiveJpeg::probe(input, info) == Result::Ok);
    std::vector<uint8_t> workspace(ProgressiveJpeg::workspaceBytes(info));
    unsigned bands = 0;
    const ProgressiveJpeg::DecodeOptions options{workspace.data(), workspace.size(),
                                                 [](void* user) { return *static_cast<unsigned*>(user) != 0; }, &bands};
    const auto result = ProgressiveJpeg::decode(
        input, info, options,
        [](void* user, uint16_t, const uint8_t*, uint16_t, uint16_t, uint16_t) {
          ++*static_cast<unsigned*>(user);
          return true;
        },
        &bands);
    runner.expectTrue(result == Result::Aborted, "cancellation after a decoded band fails conversion");
    runner.expectEq(1U, bands, "cancellation stops decoding before a second band");
    runner.expectEq(size_t(2), input.position(), "decode-phase cancellation restores input position");
  }
  for (const bool stuffed : {false, true}) {
    const size_t start = stuffed ? scan.end : frame.offset;
    std::string sequence(8192, '\xFF');
    if (stuffed) {
      for (size_t i = 1; i < sequence.size(); i += 2) sequence[i] = '\0';
    }
    std::string bytes = valid;
    bytes.insert(start, sequence);
    FsFile input;
    input.setBuffer(bytes);
    input.seek(2);
    Decoded decoded;
    assert(ProgressiveJpeg::probe(input, decoded.info) == Result::Ok);
    std::vector<uint8_t> workspace(ProgressiveJpeg::workspaceBytes(decoded.info));
    struct Cancellation {
      FsFile& input;
      size_t begin, end;
      bool reached = false;
    } cancellation{input, start + 1024, start + sequence.size()};
    const ProgressiveJpeg::DecodeOptions options{workspace.data(), workspace.size(),
                                                 [](void* user) {
                                                   auto& state = *static_cast<Cancellation*>(user);
                                                   const size_t position = state.input.position();
                                                   state.reached = position >= state.begin && position < state.end;
                                                   return state.reached;
                                                 },
                                                 &cancellation};
    const auto result = ProgressiveJpeg::decode(input, decoded.info, options, collect, &decoded);
    runner.expectTrue(
        cancellation.reached && result == Result::Aborted,
        stuffed ? "cancellation interrupts stuffed-byte scanning" : "cancellation interrupts marker fill");
    runner.expectTrue(decoded.pixels.empty(), "index cancellation emits no image bands");
    runner.expectEq(size_t(2), input.position(), "sequence cancellation restores input position");
  }
  FsFile file;
  file.setBuffer(valid);
  ProgressiveJpeg::ImageInfo info;
  assert(ProgressiveJpeg::probe(file, info) == Result::Ok);
  std::vector<uint8_t> workspace(ProgressiveJpeg::workspaceBytes(info));
  ProgressiveJpeg::DecodeOptions options{workspace.data(), workspace.size() - 1};
  auto stop = [](void*, uint16_t, const uint8_t*, uint16_t, uint16_t, uint16_t) { return false; };
  runner.expectTrue(ProgressiveJpeg::decode(file, info, options, stop, nullptr) == Result::OutOfMemory,
                    "short workspace fails");
  options.workspaceSize = workspace.size();
  runner.expectTrue(ProgressiveJpeg::decode(file, info, options, stop, nullptr) == Result::Stopped,
                    "output stop is not success");
  file.setBuffer(valid);
  file.setReadLimit(511);
  runner.expectTrue(ProgressiveJpeg::probe(file, info) == Result::IoError, "short refill fails");
  for (size_t allowed : {size_t(0), size_t(1)}) {
    file.setBuffer(readFixture("gray.jpg"));
    file.setFailSeekAfter(allowed);
    runner.expectTrue(ProgressiveJpeg::probe(file, info) == Result::IoError,
                      "failed source seek or position restoration fails probing");
  }
  return runner.allPassed() ? 0 : 1;
}
