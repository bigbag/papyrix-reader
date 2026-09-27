#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "Uc8179X4ProDriver.h"
#include "test_utils.h"

using papyrix::eink::Uc8179RefreshMode;
using papyrix::eink::Uc8179X4ProDriver;
using papyrix::eink::Uc8279Bus;

namespace {

struct CommandRecord {
  uint8_t command;
  std::vector<uint8_t> data;
};

class RecordingBus final : public Uc8279Bus {
 public:
  uint16_t resetSettleMs = 0;
  std::vector<CommandRecord> records;
  std::vector<std::string> events;
  std::vector<bool> strictWaits;
  std::string failedWait;

  void reset(uint16_t extraSettleMs) override {
    resetSettleMs = extraSettleMs;
    events.push_back("R");
  }

  void command(uint8_t value) override {
    records.push_back({value, {}});
    events.push_back("C:" + hex(value));
  }

  void data(uint8_t value) override { records.back().data.push_back(value); }

  void data(const uint8_t* values, size_t size) override {
    records.back().data.insert(records.back().data.end(), values, values + size);
  }

  void commandData(uint8_t value, const uint8_t* values, size_t size) override {
    command(value);
    data(values, size);
  }

  void beginData(uint8_t value) override { command(value); }

  void write(const uint8_t* values, size_t size) override {
    records.back().data.insert(records.back().data.end(), values, values + size);
  }

  void endData() override {}

  bool waitBusy(const char* operation, bool requireAssertion) override {
    const std::string label = operation == nullptr ? "" : operation;
    events.push_back("W:" + label);
    strictWaits.push_back(requireAssertion);
    return label != failedWait;
  }
  bool waitReady(const char* operation) override { return waitBusy(operation, false); }

  bool hasCommandData(uint8_t value, std::initializer_list<uint8_t> expected) const {
    return std::any_of(records.begin(), records.end(), [&](const CommandRecord& record) {
      return record.command == value && record.data == std::vector<uint8_t>(expected.begin(), expected.end());
    });
  }

  void clear() {
    records.clear();
    events.clear();
    strictWaits.clear();
    failedWait.clear();
  }

 private:
  static std::string hex(uint8_t value) {
    const char digits[] = "0123456789ABCDEF";
    std::string result = "00";
    result[0] = digits[value >> 4];
    result[1] = digits[value & 0x0F];
    return result;
  }
};

std::vector<const CommandRecord*> recordsFor(const RecordingBus& bus, uint8_t command) {
  std::vector<const CommandRecord*> result;
  for (const auto& record : bus.records) {
    if (record.command == command) result.push_back(&record);
  }
  return result;
}

int eventIndex(const RecordingBus& bus, const std::string& event) {
  const auto found = std::find(bus.events.begin(), bus.events.end(), event);
  return found == bus.events.end() ? -1 : static_cast<int>(found - bus.events.begin());
}

bool filledWith(const CommandRecord& record, uint8_t value) {
  return std::all_of(record.data.begin(), record.data.end(), [value](uint8_t current) { return current == value; });
}

uint8_t mixedByte(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return static_cast<uint8_t>(state >> 16);
}

std::vector<uint8_t> mixedPlane(uint32_t seed) {
  std::vector<uint8_t> plane(Uc8179X4ProDriver::BUFFER_SIZE);
  uint32_t state = seed != 0 ? seed : 1;
  for (auto& byte : plane) byte = mixedByte(state);
  return plane;
}

std::vector<uint8_t> streamedFromBase(const std::vector<uint8_t>& base) {
  std::vector<uint8_t> streamed(Uc8179X4ProDriver::TRANSFER_SIZE, 0xFF);
  for (uint16_t row = 0; row < Uc8179X4ProDriver::HEIGHT; ++row) {
    const size_t from = static_cast<size_t>(row) * Uc8179X4ProDriver::WIDTH_BYTES;
    const size_t to = static_cast<size_t>(Uc8179X4ProDriver::HEIGHT - 1 - row) * Uc8179X4ProDriver::WIDTH_BYTES;
    std::copy(base.begin() + from, base.begin() + from + Uc8179X4ProDriver::WIDTH_BYTES, streamed.begin() + to);
  }
  return streamed;
}

size_t unalignedCopy(std::vector<uint8_t>& storage, const std::vector<uint8_t>& source) {
  size_t offset = 0;
  while (offset < 4 && (reinterpret_cast<uintptr_t>(storage.data() + offset) % 4) == 0) ++offset;
  std::copy(source.begin(), source.end(), storage.data() + offset);
  return offset;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("UC8179 X4 Pro driver");
  Uc8179X4ProDriver driver;
  RecordingBus bus;

  runner.expectTrue(driver.begin(bus), "begin initializes the source-backed UC8179 path");
  runner.expectEq(50, static_cast<int>(bus.resetSettleMs), "uses the source-backed reset settle");
  runner.expectTrue(bus.hasCommandData(0x00, {0x3F, 0x0A}), "writes the OEM panel setting");
  runner.expectTrue(bus.hasCommandData(0x61, {0x03, 0x20, 0x02, 0x58}), "addresses the 800 by 600 OTP geometry");
  runner.expectTrue(bus.hasCommandData(0x65, {0x00, 0x00, 0x00, 0x00}), "writes the OEM gate-source start");
  runner.expectTrue(bus.hasCommandData(0x03, {0x20}), "writes the OEM power-off sequence");
  runner.expectTrue(bus.hasCommandData(0x06, {0x25, 0x25, 0x3C, 0x25}), "writes the OEM booster sequence");
  runner.expectTrue(bus.hasCommandData(0xE1, {0x02}), "writes the OEM gate scan");
  runner.expectTrue(bus.hasCommandData(0xE3, {0x22}), "writes the source-backed power-save value");

  std::vector<uint8_t> frame(Uc8179X4ProDriver::BUFFER_SIZE, 0xA5);
  std::fill(frame.begin(), frame.begin() + Uc8179X4ProDriver::WIDTH_BYTES, 0x11);
  std::fill(frame.end() - Uc8179X4ProDriver::WIDTH_BYTES, frame.end(), 0xEE);

  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8179RefreshMode::Fast, false),
                    "first refresh completes with the OTP waveform");
  auto newPlanes = recordsFor(bus, 0x13);
  auto oldPlanes = recordsFor(bus, 0x10);
  runner.expectEq(1, static_cast<int>(newPlanes.size()), "first refresh writes one new plane");
  runner.expectEq(2, static_cast<int>(oldPlanes.size()), "first refresh seeds and synchronizes the old plane");
  runner.expectEq(static_cast<int>(Uc8179X4ProDriver::TRANSFER_SIZE), static_cast<int>(newPlanes[0]->data.size()),
                  "new plane includes the 600-gate padding");
  runner.expectEq(uint8_t{0xEE}, newPlanes[0]->data.front(), "new plane starts with the last visible row");
  runner.expectEq(uint8_t{0x11}, newPlanes[0]->data[Uc8179X4ProDriver::BUFFER_SIZE - 1],
                  "new plane ends its visible data with the first row");
  runner.expectTrue(std::all_of(newPlanes[0]->data.begin() + Uc8179X4ProDriver::BUFFER_SIZE, newPlanes[0]->data.end(),
                                [](uint8_t value) { return value == 0xFF; }),
                    "new plane pads hidden gates white");
  runner.expectTrue(filledWith(*oldPlanes[0], 0xFF), "first refresh starts from known white");
  runner.expectTrue(oldPlanes[1]->data == newPlanes[0]->data, "completed refresh synchronizes the old plane");
  runner.expectTrue(bus.hasCommandData(0x00, {0x1F, 0x0A}), "first refresh selects controller OTP");
  runner.expectTrue(bus.hasCommandData(0xE5, {0x1E}), "first refresh uses the full OTP timing");
  runner.expectEq(0, static_cast<int>(recordsFor(bus, 0x20).size()), "black-white refresh uploads no custom LUT");
  runner.expectTrue(eventIndex(bus, "W:8179_DRF") > eventIndex(bus, "C:12"), "refresh waits for BUSY completion");
  runner.expectTrue(bus.strictWaits.size() == 2 && bus.strictWaits[0] && bus.strictWaits[1],
                    "power-on and refresh require BUSY assertion");

  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8179RefreshMode::Fast, false), "warm fast refresh completes");
  runner.expectTrue(bus.hasCommandData(0xE5, {0x5A}), "warm fast refresh uses the OEM frame-rate selector");
  runner.expectTrue(eventIndex(bus, "C:91") >= 0 && eventIndex(bus, "C:92") > eventIndex(bus, "W:8179_DRF"),
                    "warm fast refresh brackets activation with partial mode");
  oldPlanes = recordsFor(bus, 0x10);
  runner.expectEq(1, static_cast<int>(oldPlanes.size()), "warm fast refresh reuses the resident old plane");

  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8179RefreshMode::Half, true),
                    "half refresh and analog turn-off complete");
  oldPlanes = recordsFor(bus, 0x10);
  runner.expectEq(2, static_cast<int>(oldPlanes.size()), "half refresh writes opposite and synchronized old planes");
  runner.expectEq(uint8_t{0x11}, oldPlanes[0]->data.front(), "half refresh drives the opposite target");
  runner.expectTrue(eventIndex(bus, "C:02") > eventIndex(bus, "W:8179_DRF"), "power-off follows refresh completion");
  runner.expectTrue(eventIndex(bus, "W:8179_POF") > eventIndex(bus, "C:02"), "power-off waits for BUSY completion");

  std::vector<uint8_t> lsb(Uc8179X4ProDriver::BUFFER_SIZE, 0x00);
  std::vector<uint8_t> msb(Uc8179X4ProDriver::BUFFER_SIZE, 0x00);
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8179RefreshMode::Full, false),
                    "grayscale base refresh completes");
  bus.clear();
  runner.expectTrue(driver.copyGrayscaleLsb(bus, lsb.data()), "grayscale LSB plane loads");
  runner.expectTrue(driver.copyGrayscaleMsb(bus, msb.data()), "grayscale MSB plane loads");
  runner.expectTrue(driver.displayGray(bus, false), "source-backed grayscale activation completes");
  runner.expectTrue(bus.hasCommandData(0x00, {0x3F, 0x0A}), "grayscale selects the custom LUT registers");
  for (uint8_t command = 0x20; command <= 0x24; ++command) {
    const auto tables = recordsFor(bus, command);
    runner.expectEq(1, static_cast<int>(tables.size()), "grayscale writes one source table");
    runner.expectEq(42, static_cast<int>(tables[0]->data.size()), "grayscale table has the source length");
  }

  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8179RefreshMode::Fast, false),
                    "Fast after grayscale completes the black-white redrive");
  runner.expectTrue(bus.hasCommandData(0x00, {0x3F, 0x0A}) && recordsFor(bus, 0x20).size() == 1,
                    "grayscale exit uses the custom redrive waveform");
  runner.expectTrue(eventIndex(bus, "W:8179_gray_pre_DRF") > eventIndex(bus, "C:12"),
                    "grayscale exit waits for the redrive");
  newPlanes = recordsFor(bus, 0x13);
  oldPlanes = recordsFor(bus, 0x10);
  runner.expectTrue(newPlanes.size() == 1 && oldPlanes.size() == 1 && oldPlanes[0]->data == newPlanes[0]->data,
                    "grayscale exit synchronizes the new black-white baseline");
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8179RefreshMode::Fast, false),
                    "second Fast refresh completes after redrive");
  runner.expectTrue(bus.hasCommandData(0x00, {0x1F, 0x0A}) && recordsFor(bus, 0x20).empty(),
                    "redrive is one-shot and returns to OTP");
  driver.requestResync();
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8179RefreshMode::Fast, false), "resync accepts a Fast request");
  oldPlanes = recordsFor(bus, 0x10);
  runner.expectTrue(bus.hasCommandData(0xE5, {0x1E}) && oldPlanes.size() == 2 && filledWith(*oldPlanes[0], 0xFF),
                    "resync promotes Fast to a full update from a known baseline");
  std::vector<uint8_t> abortedLsb(Uc8179X4ProDriver::BUFFER_SIZE, 0x3C);
  std::vector<uint8_t> bwFrame(Uc8179X4ProDriver::BUFFER_SIZE, 0xC3);
  auto streamedPlane = [&](uint8_t firstRow, uint8_t body, uint8_t lastRow) {
    std::vector<uint8_t> plane(Uc8179X4ProDriver::TRANSFER_SIZE, 0xFF);
    for (uint16_t row = 0; row < Uc8179X4ProDriver::HEIGHT; ++row) {
      const uint8_t value = row == 0 ? firstRow : row == Uc8179X4ProDriver::HEIGHT - 1 ? lastRow : body;
      const size_t at = static_cast<size_t>(Uc8179X4ProDriver::HEIGHT - 1 - row) * Uc8179X4ProDriver::WIDTH_BYTES;
      std::fill(plane.begin() + at, plane.begin() + at + Uc8179X4ProDriver::WIDTH_BYTES, value);
    }
    return plane;
  };
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8179RefreshMode::Full, false),
                    "aborted-transaction base refresh completes");
  bus.clear();
  runner.expectTrue(driver.copyGrayscaleLsb(bus, abortedLsb.data()), "aborted transaction loads the LSB plane");
  runner.expectTrue(driver.cleanupGrayscale(bus, bwFrame.data()),
                    "cleanup restores black-white after an aborted grayscale transaction");
  runner.expectEq(0, static_cast<int>(recordsFor(bus, 0x12).size()),
                  "an aborted grayscale transaction never activates the panel");
  oldPlanes = recordsFor(bus, 0x10);
  newPlanes = recordsFor(bus, 0x13);
  runner.expectEq(2, static_cast<int>(oldPlanes.size()), "cleanup rewrites the overwritten old plane");
  runner.expectEq(1, static_cast<int>(newPlanes.size()), "cleanup rewrites the new plane with black-white");
  if (oldPlanes.size() == 2 && newPlanes.size() == 1) {
    runner.expectTrue(oldPlanes[0]->data == streamedPlane(0x3D, 0xBD, 0xFE),
                      "LSB loading overwrites the resident black-white old plane");
    runner.expectTrue(
        oldPlanes[1]->data == streamedPlane(0xC3, 0xC3, 0xC3) && newPlanes[0]->data == streamedPlane(0xC3, 0xC3, 0xC3),
        "cleanup restores both planes with the black-white frame");
  }
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8179RefreshMode::Fast, false),
                    "fast refresh resumes on the restored baseline");
  runner.expectEq(1, static_cast<int>(recordsFor(bus, 0x13).size()),
                  "fast refresh after cleanup reuses the restored old plane");
  runner.expectEq(1, static_cast<int>(recordsFor(bus, 0x10).size()),
                  "fast refresh updates the old plane without a full reseed");

  bus.clear();
  runner.expectTrue(driver.deepSleep(bus), "controller deep sleep completes");
  runner.expectTrue(bus.hasCommandData(0x07, {0xA5}), "controller deep sleep uses the UC8179 key");
  runner.expectTrue(eventIndex(bus, "C:07") > eventIndex(bus, "C:02"), "deep sleep follows analog power-off");

  Uc8179X4ProDriver failedDriver;
  RecordingBus failedBus;
  runner.expectTrue(failedDriver.begin(failedBus), "failure-path driver initializes");
  failedBus.clear();
  failedBus.failedWait = "8179_PON";
  runner.expectFalse(failedDriver.display(failedBus, frame.data(), Uc8179RefreshMode::Fast, false),
                     "power-on timeout rejects the refresh");
  runner.expectEq(-1, eventIndex(failedBus, "C:12"), "power-on timeout does not activate the panel");

  Uc8179X4ProDriver powerOffDriver;
  RecordingBus powerOffBus;
  runner.expectTrue(powerOffDriver.begin(powerOffBus), "power-off failure driver initializes");
  powerOffBus.clear();
  powerOffBus.failedWait = "8179_POF";
  runner.expectFalse(powerOffDriver.display(powerOffBus, frame.data(), Uc8179RefreshMode::Full, true),
                     "power-off timeout rejects the refresh result");
  powerOffBus.clear();
  runner.expectTrue(powerOffDriver.deepSleep(powerOffBus), "deep sleep retries a failed power-off");
  runner.expectTrue(eventIndex(powerOffBus, "C:02") >= 0 &&
                        eventIndex(powerOffBus, "C:07") > eventIndex(powerOffBus, "W:8179_power_down"),
                    "deep sleep enters only after the power-off retry completes");

  const std::vector<uint8_t> mixedBase = mixedPlane(0x1234567);
  const std::vector<uint8_t> mixedLsb = mixedPlane(0x9E3779B9);
  const std::vector<uint8_t> mixedMsb = mixedPlane(0xDEADBEEF);

  std::vector<uint8_t> expectedBase = mixedBase;
  for (uint32_t i = 0; i < Uc8179X4ProDriver::BUFFER_SIZE; ++i) {
    expectedBase[i] = static_cast<uint8_t>(mixedBase[i] | mixedLsb[i]);
  }
  const std::vector<uint8_t> expectedAfterLsb = expectedBase;

  Uc8179X4ProDriver mixedDriver;
  RecordingBus mixedBus;
  runner.expectTrue(mixedDriver.begin(mixedBus), "mixed-plane driver initializes");
  runner.expectTrue(mixedDriver.display(mixedBus, mixedBase.data(), Uc8179RefreshMode::Full, false),
                    "mixed base refresh completes");
  mixedBus.clear();
  runner.expectTrue(mixedDriver.copyGrayscaleLsb(mixedBus, mixedLsb.data()), "mixed LSB plane loads");
  auto mixedDtm1 = recordsFor(mixedBus, 0x10);
  runner.expectEq(1, static_cast<int>(mixedDtm1.size()), "mixed LSB load writes one DTM1 plane");
  if (mixedDtm1.size() == 1) {
    runner.expectTrue(mixedDtm1[0]->data == streamedFromBase(expectedAfterLsb),
                      "mixed DTM1 matches the byte-wise OR reference in reversed row order");
  }

  std::vector<uint8_t> expectedDtm2(Uc8179X4ProDriver::BUFFER_SIZE);
  for (uint32_t i = 0; i < Uc8179X4ProDriver::BUFFER_SIZE; ++i) {
    uint8_t merged = expectedBase[i];
    const uint8_t msb = mixedMsb[i];
    expectedDtm2[i] = static_cast<uint8_t>(merged ^ msb);
    merged &= merged ^ msb;
    expectedBase[i] = merged;
  }

  mixedBus.clear();
  runner.expectTrue(mixedDriver.copyGrayscaleMsb(mixedBus, mixedMsb.data()), "mixed MSB plane loads");
  auto mixedDtm2 = recordsFor(mixedBus, 0x13);
  runner.expectEq(1, static_cast<int>(mixedDtm2.size()), "mixed MSB load writes one DTM2 plane");
  if (mixedDtm2.size() == 1) {
    runner.expectTrue(mixedDtm2[0]->data == streamedFromBase(expectedDtm2),
                      "mixed DTM2 matches the byte-wise XOR reference in reversed row order");
  }
  mixedBus.clear();
  runner.expectTrue(mixedDriver.displayGray(mixedBus, false), "mixed grayscale activation completes");
  auto mixedGrayDtm1 = recordsFor(mixedBus, 0x10);
  auto mixedGrayDtm2 = recordsFor(mixedBus, 0x13);
  runner.expectTrue(mixedGrayDtm1.size() == 1 && mixedGrayDtm2.size() == 1 &&
                        mixedGrayDtm1[0]->data == streamedFromBase(expectedBase) &&
                        mixedGrayDtm2[0]->data == streamedFromBase(expectedBase),
                    "grayscale activation replays the merged base to both planes");

  std::vector<uint8_t> shiftedLsbStorage(mixedLsb.size() + 3);
  const uint8_t* shiftedLsb = shiftedLsbStorage.data() + unalignedCopy(shiftedLsbStorage, mixedLsb);
  runner.expectNe(uintptr_t{0}, static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(shiftedLsb) % 4),
                  "unaligned LSB plane pointer is not 4-byte aligned");

  Uc8179X4ProDriver unalignedDriver;
  RecordingBus unalignedBus;
  runner.expectTrue(unalignedDriver.begin(unalignedBus), "unaligned-plane driver initializes");
  runner.expectTrue(unalignedDriver.display(unalignedBus, mixedBase.data(), Uc8179RefreshMode::Full, false),
                    "unaligned base refresh completes");
  unalignedBus.clear();
  runner.expectTrue(unalignedDriver.copyGrayscaleLsb(unalignedBus, shiftedLsb), "unaligned LSB plane loads");
  auto unalignedDtm1 = recordsFor(unalignedBus, 0x10);
  runner.expectEq(1, static_cast<int>(unalignedDtm1.size()), "unaligned LSB load writes one DTM1 plane");
  if (unalignedDtm1.size() == 1) {
    runner.expectTrue(unalignedDtm1[0]->data == streamedFromBase(expectedAfterLsb),
                      "unaligned LSB merges byte for byte like the aligned path");
  }

  std::vector<uint8_t> shiftedMsbStorage(mixedMsb.size() + 3);
  const uint8_t* shiftedMsb = shiftedMsbStorage.data() + unalignedCopy(shiftedMsbStorage, mixedMsb);
  unalignedBus.clear();
  runner.expectTrue(unalignedDriver.copyGrayscaleMsb(unalignedBus, shiftedMsb), "unaligned MSB plane loads");
  auto unalignedDtm2 = recordsFor(unalignedBus, 0x13);
  runner.expectEq(1, static_cast<int>(unalignedDtm2.size()), "unaligned MSB load writes one DTM2 plane");
  if (unalignedDtm2.size() == 1) {
    runner.expectTrue(unalignedDtm2[0]->data == streamedFromBase(expectedDtm2),
                      "unaligned MSB XORs byte for byte like the aligned path");
  }
  unalignedBus.clear();
  runner.expectTrue(unalignedDriver.displayGray(unalignedBus, false), "unaligned grayscale activation completes");
  auto unalignedGrayDtm2 = recordsFor(unalignedBus, 0x13);
  runner.expectTrue(unalignedGrayDtm2.size() == 1 && unalignedGrayDtm2[0]->data == streamedFromBase(expectedBase),
                    "unaligned path leaves the same merged base as the aligned path");

  return runner.allPassed() ? 0 : 1;
}
