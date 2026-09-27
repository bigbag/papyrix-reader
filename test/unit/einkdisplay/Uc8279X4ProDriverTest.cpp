#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "Uc8279X4ProDriver.h"
#include "test_utils.h"

using papyrix::eink::Uc8279Bus;
using papyrix::eink::Uc8279X4ProDriver;
using papyrix::eink::Uc8279X4RefreshMode;

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

uint8_t mixedByte(uint32_t index, uint32_t seed) {
  uint32_t value = index * 2654435761u ^ seed;
  value ^= value >> 13;
  value *= 2246822519u;
  value ^= value >> 16;
  return static_cast<uint8_t>(value & 0xFF);
}

std::vector<uint8_t> orPlane(const std::vector<uint8_t>& base, const std::vector<uint8_t>& plane) {
  std::vector<uint8_t> merged(base.size());
  for (size_t i = 0; i < base.size(); ++i) merged[i] = static_cast<uint8_t>(base[i] | plane[i]);
  return merged;
}

std::vector<uint8_t> andXorPlane(const std::vector<uint8_t>& base, const std::vector<uint8_t>& plane) {
  std::vector<uint8_t> merged(base.size());
  for (size_t i = 0; i < base.size(); ++i) {
    merged[i] = static_cast<uint8_t>(base[i] & (base[i] ^ plane[i]));
  }
  return merged;
}

std::vector<uint8_t> expectedGrayPlane(const std::vector<uint8_t>& source, bool invert) {
  std::vector<uint8_t> plane(Uc8279X4ProDriver::TRANSFER_SIZE, 0xFF);
  for (uint16_t row = 0; row < Uc8279X4ProDriver::HEIGHT; ++row) {
    const size_t at = Uc8279X4ProDriver::GATE_OFFSET_BYTES + static_cast<size_t>(row) * Uc8279X4ProDriver::WIDTH_BYTES;
    for (size_t i = 0; i < Uc8279X4ProDriver::WIDTH_BYTES; ++i) {
      const uint8_t value = source[static_cast<size_t>(row) * Uc8279X4ProDriver::WIDTH_BYTES + i];
      plane[at + i] = invert ? static_cast<uint8_t>(~value) : value;
    }
  }
  return plane;
}

std::vector<uint8_t> xorPlane(const std::vector<uint8_t>& base, const std::vector<uint8_t>& plane) {
  std::vector<uint8_t> result(base.size());
  for (size_t i = 0; i < base.size(); ++i) result[i] = static_cast<uint8_t>(base[i] ^ plane[i]);
  return result;
}


}  // namespace

int main() {
  TestUtils::TestRunner runner("UC8279 X4 Pro driver");
  Uc8279X4ProDriver driver;
  RecordingBus bus;

  runner.expectTrue(driver.begin(bus, 0x68, true), "begin initializes the source-backed UC8279 path");
  runner.expectEq(50, static_cast<int>(bus.resetSettleMs), "uses the source-backed reset settle");
  runner.expectTrue(bus.hasCommandData(0x00, {0x37, 0x4D}), "writes the UC8279 panel setting");
  runner.expectTrue(bus.hasCommandData(0x61, {0x03, 0x20, 0x02, 0x58}), "addresses the 800 by 600 OTP geometry");
  runner.expectTrue(bus.hasCommandData(0x65, {0x00, 0x00, 0x00, 0x00}), "writes the gate-source start");
  runner.expectTrue(bus.hasCommandData(0x03, {0x20}), "writes the power-off sequence");
  runner.expectTrue(bus.hasCommandData(0x30, {0x0E}), "writes the X4 Pro PLL value");
  runner.expectTrue(bus.hasCommandData(0xE1, {0x02}), "writes the gate scan");
  runner.expectEq(0, static_cast<int>(recordsFor(bus, 0x06).size()), "does not write the UC8179 booster sequence");

  std::vector<uint8_t> frame(Uc8279X4ProDriver::BUFFER_SIZE, 0xA5);
  std::fill(frame.begin(), frame.begin() + Uc8279X4ProDriver::WIDTH_BYTES, 0x11);
  std::fill(frame.end() - Uc8279X4ProDriver::WIDTH_BYTES, frame.end(), 0xEE);

  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8279X4RefreshMode::Full, true),
                    "full refresh completes with the UC8279 OTP waveform");
  auto newPlanes = recordsFor(bus, 0x13);
  auto oldPlanes = recordsFor(bus, 0x10);
  runner.expectEq(1, static_cast<int>(newPlanes.size()), "full refresh writes one new plane");
  runner.expectEq(2, static_cast<int>(oldPlanes.size()), "full refresh seeds and synchronizes the old plane");
  runner.expectEq(static_cast<int>(Uc8279X4ProDriver::TRANSFER_SIZE), static_cast<int>(newPlanes[0]->data.size()),
                  "new plane covers the 600-gate address space");
  runner.expectTrue(
      std::all_of(newPlanes[0]->data.begin(), newPlanes[0]->data.begin() + Uc8279X4ProDriver::GATE_OFFSET_BYTES,
                  [](uint8_t value) { return value == 0xFF; }),
      "new plane pads the first 120 gates white");
  runner.expectEq(uint8_t{0x11}, newPlanes[0]->data[Uc8279X4ProDriver::GATE_OFFSET_BYTES],
                  "visible rows start in forward order after the gate offset");
  runner.expectEq(uint8_t{0xEE}, newPlanes[0]->data.back(), "visible rows end with the final framebuffer row");
  runner.expectTrue(filledWith(*oldPlanes[0], 0xFF), "full refresh starts from known white");
  runner.expectTrue(oldPlanes[1]->data == newPlanes[0]->data, "completed refresh synchronizes the old plane");
  runner.expectTrue(bus.hasCommandData(0x50, {0x97}), "full refresh writes the one-byte full CDI");
  runner.expectTrue(bus.hasCommandData(0xE5, {0x1E}), "full refresh writes the full temperature selector");
  runner.expectTrue(eventIndex(bus, "C:00") > eventIndex(bus, "W:8279x4_PON"),
                    "OTP panel setting is rewritten after power-on");
  runner.expectTrue(bus.hasCommandData(0x00, {0x17, 0x4D}), "full refresh selects UC8279 OTP after power-on");
  runner.expectTrue(eventIndex(bus, "W:8279x4_DRF") > eventIndex(bus, "C:12"), "refresh waits for BUSY completion");
  runner.expectTrue(bus.strictWaits.size() == 3 && bus.strictWaits[0] && bus.strictWaits[1] && bus.strictWaits[2],
                    "power-on, refresh, and power-off require BUSY assertion");

  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8279X4RefreshMode::Fast, false), "warm fast refresh completes");
  runner.expectTrue(bus.hasCommandData(0x50, {0xD7}), "fast refresh writes the one-byte partial CDI");
  runner.expectTrue(bus.hasCommandData(0xE5, {0x5A}), "fast refresh writes the fast temperature selector");
  runner.expectTrue(bus.hasCommandData(0x90, {0x00, 0x00, 0x03, 0x1F, 0x00, 0x78, 0x02, 0x57, 0x01}),
                    "fast refresh uses the required 120-gate partial window");
  runner.expectTrue(eventIndex(bus, "C:91") >= 0 && eventIndex(bus, "C:92") > eventIndex(bus, "W:8279x4_DRF"),
                    "fast refresh brackets activation with partial mode");

  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8279X4RefreshMode::Half, false),
                    "half refresh completes the charge scrub");
  newPlanes = recordsFor(bus, 0x13);
  oldPlanes = recordsFor(bus, 0x10);
  runner.expectTrue(newPlanes.size() == 1 && oldPlanes.size() == 2,
                    "half refresh writes the target and both old-plane states");
  if (newPlanes.size() == 1 && oldPlanes.size() == 2) {
    const auto offset = Uc8279X4ProDriver::GATE_OFFSET_BYTES;
    runner.expectTrue(std::equal(frame.begin(), frame.end(), oldPlanes[0]->data.begin() + offset,
                                 [](uint8_t target, uint8_t old) { return old == static_cast<uint8_t>(~target); }),
                      "half refresh drives every visible pixel from its opposite state");
    runner.expectTrue(oldPlanes[1]->data == newPlanes[0]->data,
                      "half refresh restores the real old plane for subsequent differential updates");
  }
  runner.expectTrue(eventIndex(bus, "C:10") < eventIndex(bus, "C:12"),
                    "charge scrub seeds the old plane before activation");
  runner.expectTrue(bus.hasCommandData(0x50, {0x97}) && bus.hasCommandData(0xE5, {0x1E}),
                    "half refresh uses the full waveform selection");

  std::vector<uint8_t> lsb(Uc8279X4ProDriver::BUFFER_SIZE, 0x00);
  std::vector<uint8_t> msb(Uc8279X4ProDriver::BUFFER_SIZE, 0x00);
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8279X4RefreshMode::Full, false),
                    "grayscale base refresh completes");
  bus.clear();
  runner.expectTrue(driver.copyGrayscaleLsb(bus, lsb.data()), "grayscale LSB plane loads");
  runner.expectTrue(driver.copyGrayscaleMsb(bus, msb.data()), "grayscale MSB plane loads");
  runner.expectTrue(driver.displayGray(bus, false), "UC8279 grayscale activation completes");
  runner.expectTrue(bus.hasCommandData(0x00, {0x37, 0x4D}), "grayscale selects custom LUT registers");
  runner.expectTrue(bus.hasCommandData(0x50, {0x97}), "grayscale writes the one-byte AA CDI");
  for (uint8_t command = 0x20; command <= 0x24; ++command) {
    const auto tables = recordsFor(bus, command);
    runner.expectEq(1, static_cast<int>(tables.size()), "grayscale writes one source table");
    runner.expectEq(49, static_cast<int>(tables[0]->data.size()), "grayscale table has the UC8279 source length");
    runner.expectEq(command == 0x22 || command == 0x23 ? uint8_t{0x83} : uint8_t{0x03}, tables[0]->data[2],
                    "LUT version 68 selects its waveform bytes");
  }

  for (int offset : {0, 1, 3}) {
    Uc8279X4ProDriver trial;
    RecordingBus trialBus;
    std::vector<uint32_t> lsbStorage((Uc8279X4ProDriver::BUFFER_SIZE + 3) / 4 + 1);
    std::vector<uint32_t> msbStorage((Uc8279X4ProDriver::BUFFER_SIZE + 3) / 4 + 1);
    auto* lsbBytes = reinterpret_cast<uint8_t*>(lsbStorage.data());
    auto* msbBytes = reinterpret_cast<uint8_t*>(msbStorage.data());
    std::vector<uint8_t> base(Uc8279X4ProDriver::BUFFER_SIZE);
    std::vector<uint8_t> lsbPlane(Uc8279X4ProDriver::BUFFER_SIZE);
    std::vector<uint8_t> msbPlane(Uc8279X4ProDriver::BUFFER_SIZE);
    for (size_t i = 0; i < base.size(); ++i) {
      base[i] = mixedByte(static_cast<uint32_t>(i), 0x51);
      lsbPlane[i] = mixedByte(static_cast<uint32_t>(i), 0xA3);
      msbPlane[i] = mixedByte(static_cast<uint32_t>(i), 0xC7);
      lsbBytes[i + static_cast<size_t>(offset)] = lsbPlane[i];
      msbBytes[i + static_cast<size_t>(offset)] = msbPlane[i];
    }
    const std::string tag = offset == 0 ? "aligned" : "unaligned offset " + std::to_string(offset);
    const uint8_t* lsbInput = lsbBytes + offset;
    const uint8_t* msbInput = msbBytes + offset;
    runner.expectEq(offset, static_cast<int>(reinterpret_cast<uintptr_t>(lsbInput) & 3),
                    tag + " plane pointer has the expected word alignment");

    runner.expectTrue(trial.begin(trialBus, 0x68, true), tag + " trial driver initializes");
    trialBus.clear();
    runner.expectTrue(trial.display(trialBus, base.data(), Uc8279X4RefreshMode::Full, false),
                      tag + " mixed base refresh completes");
    trialBus.clear();
    runner.expectTrue(trial.copyGrayscaleLsb(trialBus, lsbInput), tag + " mixed LSB plane loads");
    const auto lsbRecords = recordsFor(trialBus, 0x10);
    runner.expectEq(1, static_cast<int>(lsbRecords.size()), tag + " LSB load writes one DTM1 plane");
    if (lsbRecords.size() == 1) {
      const auto expected = expectedGrayPlane(orPlane(base, lsbPlane), true);
      runner.expectTrue(lsbRecords[0]->data == expected, tag + " DTM1 matches the byte-wise inverted OR");
    }
    trialBus.clear();
    runner.expectTrue(trial.copyGrayscaleMsb(trialBus, msbInput), tag + " mixed MSB plane loads");
    const auto msbRecords = recordsFor(trialBus, 0x13);
    runner.expectEq(1, static_cast<int>(msbRecords.size()), tag + " MSB load writes one DTM2 plane");
    if (msbRecords.size() == 1) {
      const auto expected = expectedGrayPlane(xorPlane(orPlane(base, lsbPlane), msbPlane), true);
      runner.expectTrue(msbRecords[0]->data == expected, tag + " DTM2 matches the byte-wise inverted XOR");
    }
    trialBus.clear();
    runner.expectTrue(trial.displayGray(trialBus, false), tag + " mixed grayscale activation completes");
    const auto activated = recordsFor(trialBus, 0x10);
    const auto activatedExpected = expectedGrayPlane(andXorPlane(orPlane(base, lsbPlane), msbPlane), false);
    runner.expectTrue(activated.size() == 1 && activated[0]->data == activatedExpected,
                      tag + " activated DTM1 matches the byte-wise AND-XOR base");
  }

  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8279X4RefreshMode::Fast, false),
                    "Fast after grayscale completes the black-white redrive");
  runner.expectTrue(bus.hasCommandData(0x00, {0x37, 0x4D}) && recordsFor(bus, 0x20).size() == 1,
                    "grayscale exit uses the custom redrive waveform");
  runner.expectTrue(eventIndex(bus, "W:8279x4_gray_pre_DRF") > eventIndex(bus, "C:12"),
                    "grayscale exit waits for the redrive");
  newPlanes = recordsFor(bus, 0x13);
  oldPlanes = recordsFor(bus, 0x10);
  runner.expectTrue(newPlanes.size() == 1 && oldPlanes.size() == 1 && oldPlanes[0]->data == newPlanes[0]->data,
                    "grayscale exit synchronizes the new black-white baseline");
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8279X4RefreshMode::Fast, false),
                    "second Fast refresh completes after redrive");
  runner.expectTrue(bus.hasCommandData(0x00, {0x17, 0x4D}) && recordsFor(bus, 0x20).empty(),
                    "redrive is one-shot and returns to OTP");
  driver.requestResync();
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8279X4RefreshMode::Fast, false),
                    "resync accepts a Fast request");
  oldPlanes = recordsFor(bus, 0x10);
  runner.expectTrue(bus.hasCommandData(0xE5, {0x1E}) && oldPlanes.size() == 2 && filledWith(*oldPlanes[0], 0xFF),
                    "resync promotes Fast to a full update from a known baseline");
  std::vector<uint8_t> abortedLsb(Uc8279X4ProDriver::BUFFER_SIZE, 0x3C);
  std::vector<uint8_t> bwFrame(Uc8279X4ProDriver::BUFFER_SIZE, 0xC3);
  auto paddedPlane = [&](uint8_t firstRow, uint8_t body, uint8_t lastRow) {
    std::vector<uint8_t> plane(Uc8279X4ProDriver::TRANSFER_SIZE, 0xFF);
    for (uint16_t row = 0; row < Uc8279X4ProDriver::HEIGHT; ++row) {
      const uint8_t value = row == 0 ? firstRow : row == Uc8279X4ProDriver::HEIGHT - 1 ? lastRow : body;
      const size_t at =
          Uc8279X4ProDriver::GATE_OFFSET_BYTES + static_cast<size_t>(row) * Uc8279X4ProDriver::WIDTH_BYTES;
      std::fill(plane.begin() + at, plane.begin() + at + Uc8279X4ProDriver::WIDTH_BYTES, value);
    }
    return plane;
  };
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8279X4RefreshMode::Full, false),
                    "aborted-transaction base refresh completes");
  bus.clear();
  runner.expectTrue(driver.copyGrayscaleLsb(bus, abortedLsb.data()), "aborted transaction loads the LSB plane");
  oldPlanes = recordsFor(bus, 0x10);
  runner.expectEq(1, static_cast<int>(oldPlanes.size()), "LSB loading rewrites the resident old plane");
  if (oldPlanes.size() == 1) {
    runner.expectTrue(oldPlanes[0]->data == paddedPlane(0xC2, 0x42, 0x01),
                      "LSB loading overwrites the black-white old plane with inverted gray data");
  }
  runner.expectTrue(driver.cleanupGrayscale(bus, bwFrame.data()),
                    "cleanup restores black-white after an aborted grayscale transaction");
  runner.expectEq(0, static_cast<int>(recordsFor(bus, 0x12).size()),
                  "an aborted grayscale transaction never activates the panel");
  oldPlanes = recordsFor(bus, 0x10);
  newPlanes = recordsFor(bus, 0x13);
  runner.expectEq(2, static_cast<int>(oldPlanes.size()), "cleanup rewrites the overwritten old plane");
  runner.expectEq(0, static_cast<int>(newPlanes.size()), "cleanup reuses the resident black-white new plane");
  if (oldPlanes.size() == 2) {
    runner.expectTrue(oldPlanes[1]->data == paddedPlane(0xC3, 0xC3, 0xC3),
                      "cleanup rewrites the old plane with the black-white frame");
  }
  bus.clear();
  runner.expectTrue(driver.display(bus, frame.data(), Uc8279X4RefreshMode::Fast, false),
                    "fast refresh resumes on the restored baseline");
  runner.expectEq(1, static_cast<int>(recordsFor(bus, 0x13).size()),
                  "fast refresh after cleanup reuses the restored old plane");
  runner.expectEq(1, static_cast<int>(recordsFor(bus, 0x10).size()),
                  "fast refresh updates the old plane without a full reseed");

  bus.clear();
  runner.expectTrue(driver.deepSleep(bus), "controller deep sleep completes");
  runner.expectTrue(bus.hasCommandData(0x07, {0xA5}), "controller deep sleep uses the UC key");
  runner.expectTrue(eventIndex(bus, "C:07") > eventIndex(bus, "C:02"), "deep sleep follows analog power-off");

  for (bool programPll : {false, true}) {
    Uc8279X4ProDriver variant03;
    RecordingBus variantBus;
    const bool rendered = variant03.begin(variantBus, 0x03, programPll) &&
                          variant03.display(variantBus, frame.data(), Uc8279X4RefreshMode::Full, false) &&
                          variant03.copyGrayscaleLsb(variantBus, frame.data()) &&
                          variant03.copyGrayscaleMsb(variantBus, frame.data()) &&
                          variant03.displayGray(variantBus, false);
    const auto tables = recordsFor(variantBus, 0x20);
    runner.expectTrue(rendered && tables.size() == 1 && tables[0]->data.size() == 49 &&
                          tables[0]->data[2] == (programPll ? 0x03 : 0x02),
                      programPll ? "Pro variant 03 keeps its existing waveform"
                                 : "Classic variant 03 uses the QY waveform");
  }

  Uc8279X4ProDriver failedDriver;
  RecordingBus failedBus;
  runner.expectTrue(failedDriver.begin(failedBus, 0x68, true), "failure-path driver initializes");
  for (uint8_t variant : {uint8_t{0}, uint8_t{0x67}}) {
    Uc8279X4ProDriver monochrome;
    RecordingBus monoBus;
    runner.expectTrue(monochrome.begin(monoBus, variant, false), "Classic OTP-only panel initializes");
    monoBus.clear();
    runner.expectTrue(monochrome.display(monoBus, frame.data(), Uc8279X4RefreshMode::Full, false),
                      "Classic OTP-only panel renders monochrome");
    monoBus.clear();
    runner.expectFalse(monochrome.copyGrayscaleLsb(monoBus, frame.data()),
                       "Classic unknown waveform cannot write grayscale planes");
    runner.expectFalse(monochrome.displayGray(monoBus, false), "Classic unknown waveform cannot activate gray");
    runner.expectFalse(monoBus.hasCommandData(0x00, {0x37, 0x4D}),
                       "Classic unknown waveform cannot select external LUTs");
  }
  failedBus.clear();
  failedBus.failedWait = "8279x4_PON";
  runner.expectFalse(failedDriver.display(failedBus, frame.data(), Uc8279X4RefreshMode::Full, false),
                     "power-on timeout rejects the refresh");
  runner.expectEq(-1, eventIndex(failedBus, "C:12"), "power-on timeout does not activate the panel");

  return runner.allPassed() ? 0 : 1;
}
