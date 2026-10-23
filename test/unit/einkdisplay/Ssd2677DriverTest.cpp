#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "Ssd2677Driver.h"
#include "test_utils.h"
#include <platform_stubs.h>

namespace {
struct Packet { uint8_t command; std::vector<uint8_t> data; };
class RecordingBus final : public papyrix::eink::Uc8279Bus {
 public:
  std::vector<Packet> packets;
  std::string failure;
  void reset(uint16_t) override {}
  void command(uint8_t value) override { packets.push_back({value, {}}); }
  void data(uint8_t value) override { packets.back().data.push_back(value); }
  void data(const uint8_t* bytes, size_t size) override {
    packets.back().data.insert(packets.back().data.end(), bytes, bytes + size);
  }
  void commandData(uint8_t value, const uint8_t* bytes, size_t size) override { command(value); data(bytes, size); }
  void beginData(uint8_t value) override { command(value); }
  void write(const uint8_t* bytes, size_t size) override { data(bytes, size); }
  void endData() override {}
  bool waitBusy(const char* label, bool = false) override { return failure != label; }
  bool waitReady(const char* label) override { return failure != label; }
  const std::vector<uint8_t>& payload(uint8_t command) const {
    for (auto it = packets.rbegin(); it != packets.rend(); ++it) if (it->command == command) return it->data;
    static const std::vector<uint8_t> absent;
    return absent;
  }
  bool commandBefore(uint8_t first, uint8_t second) const {
    auto a = std::find_if(packets.begin(), packets.end(), [first](const auto& p) { return p.command == first; });
    auto b = std::find_if(packets.begin(), packets.end(), [second](const auto& p) { return p.command == second; });
    return a != packets.end() && b != packets.end() && a < b;
  }
  void clear() { packets.clear(); failure.clear(); }
};
}

int main() {
  using namespace papyrix::eink;
  TestUtils::TestRunner runner("Sticky SSD2677 driver");
  Ssd2677Driver driver;
  RecordingBus bus;
  std::array<uint8_t, 48000> frame;
  frame.fill(0xFF);
  frame[47900] = 0x80;
  frame[0] = 0x40;
  runner.expectTrue(driver.begin(bus) == Ssd2677InitResult::Ok, "driver initializes required state");
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Ssd2677RefreshMode::Fast, true), "first page succeeds");
  const auto& packed = bus.payload(0x10);
  runner.expectEq(size_t{96000}, packed.size(), "two physical bits represent every pixel");
  if (packed.size() == 96000) {
    runner.expectEq(uint8_t{0xC0}, packed[0], "bottom row transfers first with old equal to new");
    runner.expectEq(uint8_t{0x00}, packed[1], "remaining bottom-edge pixels are black");
    runner.expectEq(uint8_t{0x30}, packed[95800], "top row transfers last");
  }
  runner.expectTrue(bus.payload(0xE6) == std::vector<uint8_t>({0xEE}), "first fast request selects a full OTP clean");
  runner.expectTrue(bus.commandBefore(0x12, 0x02), "full refresh precedes power off");
  std::array<uint8_t, 48000> lsb{}, msb{};
  lsb[47900] = 0x50;
  msb[47900] = 0x30;
  bus.clear();
  runner.expectTrue(driver.copyGrayscaleLsb(bus, lsb.data(), true), "absolute LSB stages independently");
  runner.expectTrue(driver.copyGrayscaleMsb(bus, msb.data()), "absolute MSB completes the image");
  runner.expectTrue(bus.payload(0x10).size() == 96000 && bus.payload(0x10)[0] == 0x1B,
                    "absolute black dark light white use physical level order");
  runner.expectTrue(driver.displayGray(bus, true), "absolute gray refresh completes");
  runner.expectTrue(bus.payload(0xE6) == std::vector<uint8_t>({0xFA}) && bus.payload(0x20).empty(),
                    "absolute gray uses OTP rather than an uploaded LUT");
  runner.expectTrue(driver.cleanupGrayscale(bus, frame.data()), "absolute staging is released after refresh");
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Ssd2677RefreshMode::Fast, false), "mono resumes after gray");
  runner.expectTrue(bus.payload(0xE6) == std::vector<uint8_t>({0xEE}), "absolute gray cleanup cannot fake a mono baseline");
  bus.clear();
  runner.expectTrue(driver.copyGrayscaleLsb(bus, lsb.data(), false), "overlay LSB stages above the mono base");
  runner.expectTrue(driver.copyGrayscaleMsb(bus, msb.data()), "overlay MSB completes the masks");
  runner.expectTrue(driver.displayGray(bus, false), "overlay gray refresh completes");
  runner.expectEq(size_t{535}, bus.payload(0x20).size(), "overlay uses the uploaded AA waveform");
  runner.expectTrue(driver.cleanupGrayscale(bus, frame.data()), "overlay cleanup releases staging");
  bus.clear();
  frame[47900] = 0x40;
  runner.expectTrue(driver.display(bus, frame.data(), Ssd2677RefreshMode::Fast, false), "mono resumes after AA");
  runner.expectTrue(bus.payload(0xE6) == std::vector<uint8_t>({0x19}), "AA resets OTP state without a full flash per page");
  runner.expectTrue(bus.payload(0x10).size() == 96000 && bus.payload(0x10)[0] == 0x90,
                    "fast refresh compares against the retained physical mono base");
  bus.clear();
  bus.failure = "2677_DRF";
  runner.expectFalse(driver.display(bus, frame.data(), Ssd2677RefreshMode::Fast, false), "refresh timeout is reported");
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Ssd2677RefreshMode::Fast, false), "refresh recovers");
  runner.expectTrue(bus.payload(0xE6) == std::vector<uint8_t>({0xEE}), "failed refresh requires a full baseline");
  bus.clear();
  runner.expectTrue(driver.copyGrayscaleLsb(bus, lsb.data(), true), "new gray operation stages LSB");
  runner.expectFalse(driver.displayGray(bus, false), "missing MSB prevents refresh");
  runner.expectTrue(bus.payload(0x12).empty(), "incomplete gray data never reaches the glass");
  driver.cleanupGrayscale(bus, frame.data());
  testSetPsramAllocationFailure(true);
  runner.expectFalse(driver.copyGrayscaleLsb(bus, lsb.data(), true), "staging allocation failure is reported");
  runner.expectFalse(driver.copyGrayscaleMsb(bus, msb.data()), "failed staging rejects MSB");
  runner.expectFalse(driver.displayGray(bus, false), "failed staging cannot reuse a prior gray image");
  Ssd2677Driver noMemory;
  runner.expectTrue(noMemory.begin(bus) == Ssd2677InitResult::OutOfMemory, "missing required old plane stops initialization");
  testSetPsramAllocationFailure(false);
  runner.expectTrue(driver.display(bus, frame.data(), Ssd2677RefreshMode::Fast, false), "mono can resume after staging failure");
  bus.clear();
  bus.failure = "2677_POF";
  runner.expectFalse(driver.deepSleep(bus), "failed power off prevents sleep");
  runner.expectTrue(bus.payload(0x07).empty(), "failed power off sends no sleep command");
  bus.clear();
  runner.expectTrue(driver.deepSleep(bus), "sleep retries power off after recovery");
  runner.expectTrue(bus.commandBefore(0x02, 0x07) && bus.payload(0x07) == std::vector<uint8_t>({0xA5}),
                    "sleep powers off before sending its check code");
  return runner.allPassed() ? 0 : 1;
}
