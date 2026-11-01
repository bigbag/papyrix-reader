#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "Uc8279X3Driver.h"
#include "Uc8279X3Luts.h"
#include "test_utils.h"

using papyrix::eink::kUc8279X3_BwDu;
using papyrix::eink::kUc8279X3_BwGc;
using papyrix::eink::kUc8279X3_Init;
using papyrix::eink::kUc8279X3_XtfAa;
using papyrix::eink::Uc8279Bus;
using papyrix::eink::Uc8279X3Driver;

namespace {

struct CommandRecord {
  uint8_t command;
  std::vector<uint8_t> data;
};

class RecordingBus : public Uc8279Bus {
 public:
  uint16_t extraResetSettleMs = 0;
  std::vector<CommandRecord> records;
  std::vector<std::string> waits;
  std::vector<std::string> events;
  std::string failedWait;

  void reset(uint16_t extraSettleMs) override { extraResetSettleMs = extraSettleMs; }

  void command(uint8_t value) override {
    records.push_back({value, {}});
    events.push_back("C:" + hex(value));
  }

  void data(uint8_t value) override { records.back().data.push_back(value); }

  void data(const uint8_t* values, size_t size) override {
    records.back().data.insert(records.back().data.end(), values, values + size);
  }

  void commandData(uint8_t commandValue, const uint8_t* values, size_t size) override {
    command(commandValue);
    data(values, size);
  }

  void beginData(uint8_t commandValue) override { command(commandValue); }

  void write(const uint8_t* values, size_t size) override {
    records.back().data.insert(records.back().data.end(), values, values + size);
  }

  void endData() override {}

  bool waitBusy(const char* operation, bool) override {
    const std::string label = operation == nullptr ? "" : operation;
    waits.push_back(label);
    events.push_back("W:" + label);
    return label != failedWait;
  }
  bool waitReady(const char* operation) override { return waitBusy(operation, false); }

  bool hasCommandData(uint8_t commandValue, std::initializer_list<uint8_t> expected) const {
    return std::any_of(records.begin(), records.end(), [&](const CommandRecord& record) {
      return record.command == commandValue && record.data == std::vector<uint8_t>(expected.begin(), expected.end());
    });
  }

  void clear() {
    records.clear();
    waits.clear();
    events.clear();
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

uint32_t fnv1a(const uint8_t* values, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; i++) {
    hash ^= values[i];
    hash *= 16777619u;
  }
  return hash;
}

std::vector<const CommandRecord*> recordsFor(const RecordingBus& bus, uint8_t command) {
  std::vector<const CommandRecord*> result;
  for (const auto& record : bus.records) {
    if (record.command == command) result.push_back(&record);
  }
  return result;
}

bool loadedBank(const RecordingBus& bus, uint8_t transitionByte) {
  for (const auto& record : bus.records) {
    if (record.command == 0x20 && record.data.size() == 42 && record.data[1] == transitionByte) return true;
  }
  return false;
}

int eventIndex(const RecordingBus& bus, const std::string& event) {
  const auto found = std::find(bus.events.begin(), bus.events.end(), event);
  return found == bus.events.end() ? -1 : static_cast<int>(found - bus.events.begin());
}

bool filledWith(const CommandRecord& record, uint8_t value) {
  return std::all_of(record.data.begin(), record.data.end(), [value](uint8_t current) { return current == value; });
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("UC8279 X3 driver");

  runner.expectEq(215, static_cast<int>(sizeof(kUc8279X3_BwGc)), "GC LUT size");
  runner.expectEq(215, static_cast<int>(sizeof(kUc8279X3_BwDu)), "DU LUT size");
  runner.expectEq(245, static_cast<int>(sizeof(kUc8279X3_XtfAa)), "AA LUT size");
  runner.expectEq(41, static_cast<int>(sizeof(kUc8279X3_Init)), "init script size");
  runner.expectEq(0xABC1A21Bu, fnv1a(&kUc8279X3_BwGc[0][0], sizeof(kUc8279X3_BwGc)), "GC LUT hash");
  runner.expectEq(0x6FC822A7u, fnv1a(&kUc8279X3_BwDu[0][0], sizeof(kUc8279X3_BwDu)), "DU LUT hash");
  runner.expectEq(0x4D2659E7u, fnv1a(&kUc8279X3_XtfAa[0][0], sizeof(kUc8279X3_XtfAa)), "AA LUT hash");
  runner.expectEq(0xC7154590u, fnv1a(kUc8279X3_Init, sizeof(kUc8279X3_Init)), "init script hash");

  Uc8279X3Driver driver;
  RecordingBus bus;
  driver.begin(bus);

  runner.expectEq(50, static_cast<int>(bus.extraResetSettleMs), "uses 50 ms reset settle");
  runner.expectTrue(bus.hasCommandData(0x00, {0x3F, 0x4A}), "writes PSR");
  runner.expectTrue(bus.hasCommandData(0x90, {0x00, 0x00, 0x03, 0x17, 0x00, 0x00, 0x02, 0x0F, 0x01}),
                    "sets 792x528 window");
  runner.expectTrue(bus.hasCommandData(0x01, {0x43, 0x00, 0x78, 0x78, 0x17}), "writes drive voltages");
  runner.expectTrue(bus.hasCommandData(0x82, {0x24}), "writes VCOM");

  std::vector<uint8_t> frame(Uc8279X3Driver::BUFFER_SIZE, 0xA5);
  std::fill(frame.begin(), frame.begin() + Uc8279X3Driver::WIDTH_BYTES, 0x11);
  std::fill(frame.end() - Uc8279X3Driver::WIDTH_BYTES, frame.end(), 0xEE);

  bus.clear();
  driver.display(bus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  auto dtm1 = recordsFor(bus, 0x10);
  auto dtm2 = recordsFor(bus, 0x13);
  runner.expectEq(2, static_cast<int>(dtm1.size()), "first refresh seeds and syncs DTM1");
  runner.expectEq(static_cast<int>(Uc8279X3Driver::BUFFER_SIZE), static_cast<int>(dtm1[0]->data.size()),
                  "first DTM1 size");
  runner.expectTrue(filledWith(*dtm1[0], 0xFF), "first refresh seeds DTM1 white");
  runner.expectEq(1, static_cast<int>(dtm2.size()), "first refresh writes one DTM2 plane");
  runner.expectEq(static_cast<int>(Uc8279X3Driver::BUFFER_SIZE), static_cast<int>(dtm2[0]->data.size()), "DTM2 size");
  runner.expectTrue(std::all_of(dtm2[0]->data.begin(), dtm2[0]->data.begin() + Uc8279X3Driver::WIDTH_BYTES,
                                [](uint8_t value) { return value == 0xEE; }),
                    "DTM2 starts with source last row");
  runner.expectTrue(std::all_of(dtm2[0]->data.end() - Uc8279X3Driver::WIDTH_BYTES, dtm2[0]->data.end(),
                                [](uint8_t value) { return value == 0x11; }),
                    "DTM2 ends with source first row");
  runner.expectTrue(dtm1[1]->data == dtm2[0]->data, "completed refresh syncs DTM1");
  runner.expectTrue(loadedBank(bus, 0x1A), "first Fast refresh loads GC");
  runner.expectTrue(bus.hasCommandData(0x50, {papyrix::eink::kUc8279X3_CdiFirst}), "first refresh uses first CDI");

  bus.clear();
  driver.display(bus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  runner.expectTrue(loadedBank(bus, 0x1A), "second Fast refresh loads GC");
  runner.expectFalse(bus.hasCommandData(0x50, {papyrix::eink::kUc8279X3_CdiFirst}),
                     "second refresh does not use first CDI");
  runner.expectTrue(bus.hasCommandData(0x50, {papyrix::eink::kUc8279X3_CdiLater}), "second refresh uses later CDI");

  bus.clear();
  driver.display(bus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  runner.expectTrue(loadedBank(bus, 0x07), "third Fast refresh loads DU");

  bus.clear();
  driver.display(bus, frame.data(), papyrix::eink::Uc8279RefreshMode::Full, false);
  runner.expectTrue(loadedBank(bus, 0x1A), "Full refresh loads GC");

  bus.clear();
  driver.display(bus, frame.data(), papyrix::eink::Uc8279RefreshMode::Half, false);
  runner.expectTrue(loadedBank(bus, 0x1A), "Half refresh loads GC");

  bus.clear();
  driver.requestResync();
  driver.display(bus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  runner.expectTrue(loadedBank(bus, 0x1A), "resync promotes Fast refresh to GC");

  bus.clear();
  driver.display(bus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, true);
  const int drfWait = eventIndex(bus, "W:8279_DRF");
  const int powerOff = eventIndex(bus, "C:02");
  runner.expectTrue(drfWait >= 0 && powerOff > drfWait, "turn-off follows refresh completion");
  runner.expectTrue(eventIndex(bus, "W:8279_POF") > powerOff, "turn-off waits for power-off");

  Uc8279X3Driver pofDriver;
  RecordingBus pofBus;
  pofDriver.begin(pofBus);
  pofDriver.display(pofBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  pofDriver.display(pofBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  pofBus.clear();
  pofBus.failedWait = "8279_POF";
  runner.expectFalse(pofDriver.display(pofBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, true),
                     "power-off failure prevents a confirmed standby frame");
  pofBus.clear();
  pofDriver.display(pofBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  runner.expectTrue(eventIndex(pofBus, "C:04") >= 0, "POF timeout retries PON");
  runner.expectTrue(loadedBank(pofBus, 0x1A), "POF timeout forces GC");
  dtm1 = recordsFor(pofBus, 0x10);
  runner.expectEq(1, static_cast<int>(dtm1.size()), "POF timeout preserves DTM1 baseline");
  runner.expectFalse(filledWith(*dtm1[0], 0xFF), "POF timeout does not seed DTM1 white");
  pofBus.clear();
  pofBus.failedWait = "8279_power_down";
  runner.expectFalse(pofDriver.deepSleep(pofBus), "failed power-down prevents deep sleep");
  runner.expectEq(-1, eventIndex(pofBus, "C:07"), "failed power-down sends no deep-sleep command");
  pofBus.clear();
  runner.expectTrue(pofDriver.deepSleep(pofBus), "deep sleep retries an incomplete power-down");

  Uc8279X3Driver darkDriver;
  RecordingBus darkBus;
  darkDriver.begin(darkBus);
  darkDriver.display(darkBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  darkDriver.display(darkBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  darkBus.clear();
  darkDriver.setBackgroundHint(true);
  darkDriver.display(darkBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  dtm1 = recordsFor(darkBus, 0x10);
  runner.expectEq(2, static_cast<int>(dtm1.size()), "dark DU writes opposite and baseline planes");
  runner.expectTrue(std::all_of(dtm1[0]->data.begin(), dtm1[0]->data.begin() + Uc8279X3Driver::WIDTH_BYTES,
                                [](uint8_t value) { return value == 0x11; }),
                    "dark DU complements source last row");
  runner.expectTrue(std::all_of(dtm1[0]->data.end() - Uc8279X3Driver::WIDTH_BYTES, dtm1[0]->data.end(),
                                [](uint8_t value) { return value == 0xEE; }),
                    "dark DU complements source first row");
  runner.expectTrue(dtm1[1]->data == recordsFor(darkBus, 0x13)[0]->data, "dark DU restores true baseline");

  Uc8279X3Driver timeoutDriver;
  RecordingBus timeoutBus;
  timeoutDriver.begin(timeoutBus);
  timeoutBus.clear();
  timeoutBus.failedWait = "8279_DRF";
  runner.expectFalse(timeoutDriver.display(timeoutBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false),
                     "refresh timeout is visible to the Clock owner");
  runner.expectEq(1, static_cast<int>(recordsFor(timeoutBus, 0x10).size()),
                  "DRF timeout does not sync displayed frame");
  timeoutBus.clear();
  runner.expectTrue(timeoutDriver.display(timeoutBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false),
                    "a full refresh recovers after timeout");
  runner.expectTrue(loadedBank(timeoutBus, 0x1A), "DRF timeout forces GC on next refresh");
  timeoutBus.clear();
  runner.expectTrue(timeoutDriver.deepSleep(timeoutBus), "the recovered panel enters deep sleep");
  runner.expectTrue(eventIndex(timeoutBus, "C:02") >= 0, "DRF timeout keeps power on for deep sleep");

  timeoutDriver.begin(timeoutBus);
  timeoutBus.clear();
  timeoutBus.failedWait = "8279_PON";
  runner.expectFalse(timeoutDriver.display(timeoutBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false),
                     "power-on failure is visible to the Clock owner");
  runner.expectEq(-1, eventIndex(timeoutBus, "C:12"), "PON timeout does not start refresh");

  Uc8279X3Driver grayDriver;
  RecordingBus grayBus;
  grayDriver.begin(grayBus);
  grayBus.clear();
  grayDriver.copyGrayscaleMsb(grayBus, frame.data());
  runner.expectTrue(grayBus.records.empty(), "MSB before LSB emits no command");
  grayDriver.displayGray(grayBus, false);
  runner.expectEq(-1, eventIndex(grayBus, "C:12"), "gray display waits for both planes");

  grayDriver.display(grayBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  grayDriver.display(grayBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  grayBus.clear();
  grayDriver.copyGrayscaleLsb(grayBus, frame.data());
  dtm1 = recordsFor(grayBus, 0x10);
  runner.expectEq(1, static_cast<int>(dtm1.size()), "LSB writes DTM1");
  runner.expectTrue(std::all_of(dtm1[0]->data.begin(), dtm1[0]->data.begin() + Uc8279X3Driver::WIDTH_BYTES,
                                [](uint8_t value) { return value == 0xEE; }),
                    "LSB is row flipped");

  grayBus.clear();
  grayDriver.copyGrayscaleMsb(grayBus, frame.data());
  dtm2 = recordsFor(grayBus, 0x13);
  runner.expectEq(1, static_cast<int>(dtm2.size()), "MSB writes DTM2");
  runner.expectTrue(std::all_of(dtm2[0]->data.begin(), dtm2[0]->data.begin() + Uc8279X3Driver::WIDTH_BYTES,
                                [](uint8_t value) { return value == 0xEE; }),
                    "MSB is row flipped");

  grayBus.clear();
  grayDriver.displayGray(grayBus, false);
  for (uint8_t command = 0x20; command <= 0x24; command++) {
    const auto tables = recordsFor(grayBus, command);
    runner.expectEq(1, static_cast<int>(tables.size()), "gray activation writes one LUT table");
    runner.expectEq(49, static_cast<int>(tables[0]->data.size()), "gray LUT table size");
  }
  runner.expectTrue(eventIndex(grayBus, "C:12") >= 0, "gray activation starts refresh");
  runner.expectTrue(eventIndex(grayBus, "W:8279_gray_DRF") >= 0, "gray activation waits for refresh");

  grayBus.clear();
  grayDriver.cleanupGrayscale(grayBus, frame.data());
  dtm2 = recordsFor(grayBus, 0x13);
  dtm1 = recordsFor(grayBus, 0x10);
  runner.expectEq(1, static_cast<int>(dtm2.size()), "gray cleanup writes DTM2");
  runner.expectEq(1, static_cast<int>(dtm1.size()), "gray cleanup writes DTM1");
  runner.expectTrue(dtm2[0]->data == dtm1[0]->data, "gray cleanup restores matching planes");

  grayBus.clear();
  grayDriver.display(grayBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  runner.expectTrue(loadedBank(grayBus, 0x07), "gray cleanup restores DU baseline");

  grayDriver.copyGrayscaleLsb(grayBus, frame.data());
  grayDriver.copyGrayscaleMsb(grayBus, frame.data());
  grayDriver.displayGray(grayBus, false);
  grayBus.clear();
  grayDriver.grayscaleRevert(grayBus);
  dtm1 = recordsFor(grayBus, 0x10);
  dtm2 = recordsFor(grayBus, 0x13);
  runner.expectEq(1, static_cast<int>(dtm1.size()), "gray revert writes DTM1");
  runner.expectEq(1, static_cast<int>(dtm2.size()), "gray revert writes DTM2");
  runner.expectTrue(filledWith(*dtm1[0], 0xFF), "gray revert fills DTM1 white");
  runner.expectTrue(filledWith(*dtm2[0], 0xFF), "gray revert fills DTM2 white");
  runner.expectTrue(loadedBank(grayBus, 0x1A), "gray revert loads GC");

  Uc8279X3Driver grayPofDriver;
  RecordingBus grayPofBus;
  grayPofDriver.begin(grayPofBus);
  grayPofDriver.copyGrayscaleLsb(grayPofBus, frame.data());
  grayPofDriver.copyGrayscaleMsb(grayPofBus, frame.data());
  grayPofBus.failedWait = "8279_gray_POF";
  grayPofDriver.displayGray(grayPofBus, true);
  grayPofBus.clear();
  grayPofDriver.grayscaleRevert(grayPofBus);
  runner.expectTrue(eventIndex(grayPofBus, "C:12") >= 0, "gray POF timeout keeps grayscale state");

  Uc8279X3Driver revertTimeoutDriver;
  RecordingBus revertTimeoutBus;
  revertTimeoutDriver.begin(revertTimeoutBus);
  revertTimeoutDriver.copyGrayscaleLsb(revertTimeoutBus, frame.data());
  revertTimeoutDriver.copyGrayscaleMsb(revertTimeoutBus, frame.data());
  revertTimeoutDriver.displayGray(revertTimeoutBus, false);
  revertTimeoutBus.clear();
  revertTimeoutBus.failedWait = "8279_gray_DRF";
  revertTimeoutDriver.grayscaleRevert(revertTimeoutBus);
  revertTimeoutBus.clear();
  revertTimeoutDriver.grayscaleRevert(revertTimeoutBus);
  runner.expectEq(-1, eventIndex(revertTimeoutBus, "C:12"), "revert DRF timeout clears grayscale mode");
  revertTimeoutDriver.display(revertTimeoutBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  runner.expectTrue(loadedBank(revertTimeoutBus, 0x1A), "revert DRF timeout forces GC");

  Uc8279X3Driver autoRevertDriver;
  RecordingBus autoRevertBus;
  autoRevertDriver.begin(autoRevertBus);
  autoRevertDriver.display(autoRevertBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  autoRevertDriver.display(autoRevertBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  autoRevertDriver.copyGrayscaleLsb(autoRevertBus, frame.data());
  autoRevertDriver.copyGrayscaleMsb(autoRevertBus, frame.data());
  autoRevertDriver.displayGray(autoRevertBus, false);
  autoRevertBus.clear();
  autoRevertDriver.display(autoRevertBus, frame.data(), papyrix::eink::Uc8279RefreshMode::Fast, false);
  dtm1 = recordsFor(autoRevertBus, 0x10);
  dtm2 = recordsFor(autoRevertBus, 0x13);
  runner.expectTrue(filledWith(*dtm1[0], 0xFF), "B/W display after gray seeds DTM1 white");
  runner.expectTrue(loadedBank(autoRevertBus, 0x1A), "B/W display after gray uses GC");
  runner.expectTrue(dtm2.back()->data == dtm1.back()->data, "B/W display restores a matching baseline");

  grayBus.clear();
  grayDriver.deepSleep(grayBus);
  runner.expectTrue(eventIndex(grayBus, "C:02") >= 0, "deep sleep powers off active analog section");
  runner.expectTrue(grayBus.hasCommandData(0x07, {0xA5}), "deep sleep sends check code");

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
