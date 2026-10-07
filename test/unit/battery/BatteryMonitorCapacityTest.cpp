#include <Arduino.h>
#include <Wire.h>
#include <cstdint>

#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "BatteryMonitor.h"
#include "test_utils.h"

// Model of the X3's BQ27220, adapted from the FreeInk SDK test
// libs/hardware/BatteryMonitor/test/host/test_bq27220_capacity.cpp (MIT; see
// lib/BatteryMonitor/FREEINK_LICENSE). It
// holds the load to TRM 6.1: keys sent back to back are ignored, a second key
// sent more than 4 s after the first is dropped, MACData() moves to the next
// block when MACDataSum() is read, and a block select sent right after a block
// write can be missed. FullChargeCapacity() copies Learned Full Charge Capacity
// at each reinit (TRM 1.1.10).

namespace {

constexpr uint16_t TARGET = 650;
constexpr uint16_t DM_BASE = 0x9280;
constexpr uint16_t DM_FCC = 0x929D;
constexpr uint16_t DM_DC = 0x929F;

struct FakeGauge {
  uint8_t sec = 3;  // SEC[1:0]: 3 sealed, 2 unsealed, 1 full access
  bool cfg = false;
  int refuse = -1;  // index of the transaction the bus refuses
  bool refuseSeal = false;
  unsigned long cfgClearDelayMs = 0;  // exit latency before CFGUPDATE clears
  int reinits = 0;
  uint16_t fcc = 3000;  // FullChargeCapacity()
  std::vector<std::string> log;
  std::array<uint8_t, 0x100> dm{};  // from DM_BASE
  uint16_t mac = 0;                 // selected block
  uint16_t staged = 0;              // MACData() written, not yet committed
  uint16_t lastKey = 0;
  unsigned long lastKeyAt = 0;
  unsigned long committedAt = 0;
  unsigned long cfgClearAt = ~0ul;
  bool exitPending = false;
  bool entryPending = false;      // ENTER_CFG_UPDATE sent, bit not landed yet
  bool refuseExitOnce = false;    // fail the first exit write, then behave
  int refuseExitsLeft = 0;        // fail this many exit writes, then behave
  int refuseStatusReadsLeft = 0;  // fail this many OperationStatus reads
  unsigned long cfgSetDelayMs = 0;
  unsigned long cfgSetAt = 0;
  // Transaction times the bus spends inside write()/read(); they advance the
  // manual clock, so key timing measures receipt, not issue.
  unsigned long firstKeyLatencyMs = 0;
  unsigned long writeLatencyMs = 0;
  unsigned long readLatencyMs = 0;

  FakeGauge() {
    for (size_t i = 0; i < dm.size(); ++i) dm[i] = static_cast<uint8_t>(i * 37 + 11);
    set(DM_FCC, 3000);
    set(DM_DC, 3000);
  }
  void set(const uint16_t address, const uint16_t value) {
    dm.at(address - DM_BASE) = value >> 8;
    dm.at(address - DM_BASE + 1) = value & 0xFF;
  }
  uint16_t get(const uint16_t address) const {
    return dm.at(address - DM_BASE) << 8 | dm.at(address - DM_BASE + 1);
  }
  // MACDataSum() over the address and the block, first two bytes replaced.
  uint8_t sum(const uint8_t first, const uint8_t second) const {
    unsigned total = (mac & 0xFF) + (mac >> 8) + first + second;
    for (int i = 2; i < 32; ++i) total += dm.at(mac - DM_BASE + i);
    return static_cast<uint8_t>(255 - total);
  }
  bool keyFollows(const uint16_t first, const uint16_t second, const uint16_t word) const {
    const unsigned long gap = millis() - lastKeyAt;
    return word == second && lastKey == first && gap >= 1500 && gap < 4000;
  }

  bool write(const uint8_t reg, const uint16_t word) {
    testManualMillisValue += (reg == 0x00 && word == 0x0414) ? firstKeyLatencyMs : writeLatencyMs;
    if (!record("W" + hex(reg) + "=" + hex(word, 4))) return false;
    if (reg == 0x00) {
      if (refuseSeal && word == 0x0030) return false;
      if (refuseExitOnce && word == 0x0091) {
        refuseExitOnce = false;
        return false;
      }
      if (refuseExitsLeft > 0 && word == 0x0091) {
        --refuseExitsLeft;
        return false;
      }
      if (sec == 3 && keyFollows(0x0414, 0x3672, word)) sec = 2;
      if (sec == 2 && keyFollows(0xFFFF, 0xFFFF, word)) sec = 1;
      if (word == 0x0090 && sec == 1) {
        entryPending = true;
        cfgSetAt = millis() + cfgSetDelayMs;
        if (cfgSetDelayMs == 0) {
          cfg = true;
          entryPending = false;
        }
        exitPending = false;
      }
      if (word == 0x0091) {
        ++reinits;
        fcc = get(DM_FCC);
      }
      if (word == 0x0091 || word == 0x0092) {
        exitPending = true;
        cfgClearAt = millis() + cfgClearDelayMs;
        if (cfgClearDelayMs == 0) {
          cfg = false;
          exitPending = false;
        }
      }
      if (word == 0x0030) sec = 3;
      lastKey = word;
      lastKeyAt = millis();
    }
    if (reg == 0x3E && millis() - committedAt >= 100) mac = word;
    if (reg == 0x40) staged = word;
    if (reg == 0x60 && cfg && sec == 1 && word == (0x2400 | sum(staged & 0xFF, staged >> 8))) {
      dm.at(mac - DM_BASE) = staged & 0xFF;
      dm.at(mac - DM_BASE + 1) = staged >> 8;
      committedAt = millis();
    }
    return true;
  }

  bool read(const uint8_t reg, uint16_t& word) {
    testManualMillisValue += readLatencyMs;
    if (!record("R" + hex(reg))) return false;
    if (reg == 0x3A && refuseStatusReadsLeft > 0) {
      --refuseStatusReadsLeft;
      return false;
    }
    if (entryPending && millis() >= cfgSetAt) {
      cfg = true;
      entryPending = false;
    }
    if (cfg && exitPending && millis() >= cfgClearAt) {
      cfg = false;
      exitPending = false;
    }
    if (reg == 0x3A) word = static_cast<uint16_t>((cfg ? 0x0400 : 0) | sec << 1);
    if (reg == 0x3C) word = get(DM_DC);
    if (reg == 0x12) word = fcc;
    if (reg == 0x40) word = static_cast<uint16_t>(dm.at(mac - DM_BASE) | dm.at(mac - DM_BASE + 1) << 8);
    if (reg == 0x60) {
      word = static_cast<uint16_t>(0x2400 | sum(dm.at(mac - DM_BASE), dm.at(mac - DM_BASE + 1)));
      mac += 32;
    }
    return true;
  }

  int writes() const {
    int count = 0;
    for (const auto& entry : log) count += entry[0] == 'W';
    return count;
  }
  bool sawWrite(const std::string& entry) const {
    for (const auto& e : log)
      if (e == entry) return true;
    return false;
  }
  int countWrite(const std::string& entry) const {
    int count = 0;
    for (const auto& e : log)
      if (e == entry) ++count;
    return count;
  }

 private:
  static std::string hex(const unsigned value, const int digits = 2) {
    char text[8];
    std::snprintf(text, sizeof(text), "%0*X", digits, value);
    return text;
  }
  bool record(const std::string& entry) {
    const bool ok = static_cast<int>(log.size()) != refuse;
    log.push_back(entry + (ok ? "" : "!"));
    return ok;
  }
};

FakeGauge* gauge = nullptr;

bool hostI2cWrite(const uint8_t addr, const uint8_t* bytes, const size_t n) {
  return addr == 0x55 && n == 3 && gauge->write(bytes[0], static_cast<uint16_t>(bytes[1] | bytes[2] << 8));
}
bool hostI2cRead(const uint8_t addr, const uint8_t reg, uint8_t* out, const size_t n) {
  uint16_t word = 0;
  if (addr != 0x55 || n != 2 || !gauge->read(reg, word)) return false;
  out[0] = word & 0xFF;
  out[1] = word >> 8;
  return true;
}

BatteryMonitor makeMonitor(const uint16_t mah = TARGET) {
  return BatteryMonitor(BatteryMonitor::Bq27220Config{20, 0, 400000, mah});
}

// Calls the load the way the main loop would: every gapMs of manual time.
void run(TestUtils::TestRunner& runner, FakeGauge& fake, const unsigned long gapMs = 10) {
  gauge = &fake;
  BatteryMonitor monitor = makeMonitor();
  unsigned long tick = testManualMillisValue;
  for (int i = 0; i < 4000; ++i) {
    tick += gapMs;
    if (testManualMillisValue > tick) tick = testManualMillisValue;
    testSetManualMillis(tick);
    if (!monitor.serviceDesignCapacity()) return;
  }
  runner.expectTrue(false, "load terminates");
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("BatteryMonitorCapacityTest");
  Wire.setDeviceHooks(&hostI2cWrite, &hostI2cRead);

  // Fresh gauge: both capacities load, the block stays intact, one reinit,
  // sealed and out of CONFIG UPDATE at the end.
  {
    FakeGauge fake;
    run(runner, fake);
    runner.expectEq(TARGET, fake.get(DM_DC), "design capacity loads");
    runner.expectEq(TARGET, fake.get(DM_FCC), "learned FCC loads");
    fake.set(DM_FCC, 3000);
    fake.set(DM_DC, 3000);
    runner.expectTrue(fake.dm == FakeGauge().dm, "rest of the block unchanged");
    runner.expectEq(1, fake.reinits, "one reinit");
    runner.expectTrue(!fake.cfg, "ends out of CONFIG UPDATE");
    runner.expectEq(3, static_cast<int>(fake.sec), "ends sealed");
  }

  // A capacity the firmware does not know is left alone; no capacity means no
  // bus traffic at all.
  {
    FakeGauge other;
    other.set(DM_DC, 1200);
    run(runner, other);
    runner.expectEq(0, other.writes(), "unknown capacity is left alone");
    runner.expectEq(uint16_t(1200), other.get(DM_DC), "unknown capacity unchanged");
    FakeGauge idle;
    gauge = &idle;
    BatteryMonitor off = makeMonitor(0);
    testSetManualMillis(100);
    runner.expectTrue(!off.serviceDesignCapacity(), "mah 0 disables the load");
    runner.expectTrue(idle.log.empty(), "no bus traffic without a capacity");
  }

  // Learned against TI's default: replaced whether Design Capacity still reads
  // 3000 or was loaded before; one learned on the cell below is kept.
  for (const uint16_t dc : {uint16_t{3000}, TARGET}) {
    FakeGauge fake;
    fake.set(DM_DC, dc);
    fake.set(DM_FCC, 2744);
    fake.fcc = 2744;
    run(runner, fake);
    runner.expectEq(TARGET, fake.get(DM_FCC), "too-high learned FCC replaced");
    runner.expectEq(TARGET, fake.get(DM_DC), "design capacity replaced");
    runner.expectEq(1, fake.reinits, "one reinit on FCC replacement");
    runner.expectEq(3, static_cast<int>(fake.sec), "sealed after FCC replacement");
  }
  {
    FakeGauge aged;
    aged.set(DM_FCC, 560);
    run(runner, aged);
    runner.expectEq(uint16_t(560), aged.get(DM_FCC), "learned-on-cell FCC kept");
    runner.expectEq(TARGET, aged.get(DM_DC), "design capacity still loads");
  }

  // Sparse polling (5 s gaps) breaks the key pair: the sequence must still
  // terminate sealed, out of CONFIG UPDATE, with Data Memory untouched.
  {
    FakeGauge fake;
    run(runner, fake, 5000);
    runner.expectEq(uint16_t(3000), fake.get(DM_DC), "sparse polling writes nothing");
    runner.expectEq(0, fake.reinits, "sparse polling reinits nothing");
    runner.expectTrue(!fake.cfg, "sparse polling ends out of CONFIG UPDATE");
    runner.expectEq(3, static_cast<int>(fake.sec), "sparse polling ends sealed");
  }

  // One blocking loop section longer than the 4 s key window must not abandon
  // the load: the sequence starts the pair over and still completes.
  {
    FakeGauge fake;
    gauge = &fake;
    BatteryMonitor monitor = makeMonitor();
    unsigned long tick = testManualMillisValue;
    bool blocked = false;
    for (int i = 0; i < 4000; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      if (!blocked && fake.sawWrite("W00=0414")) {
        tick += 5000;  // one render-shaped stall between the unlock keys
        blocked = true;
      }
      testSetManualMillis(tick);
      if (!monitor.serviceDesignCapacity()) break;
    }
    runner.expectEq(TARGET, fake.get(DM_DC), "load completes after a blocked key window");
    runner.expectEq(TARGET, fake.get(DM_FCC), "FCC loads after a blocked key window");
    runner.expectEq(1, fake.reinits, "one reinit after a blocked key window");
    runner.expectEq(3, static_cast<int>(fake.sec), "sealed after a blocked key window");
  }

  // Unequal I2C transaction times must not shrink the pair window below the
  // gauge's 1500 ms floor: scheduling anchors at key receipt.
  {
    FakeGauge fake;
    fake.firstKeyLatencyMs = 200;
    fake.writeLatencyMs = 10;
    fake.readLatencyMs = 5;
    run(runner, fake);
    runner.expectEq(TARGET, fake.get(DM_DC), "load completes with uneven transaction times");
    runner.expectEq(TARGET, fake.get(DM_FCC), "FCC loads with uneven transaction times");
    runner.expectEq(3, static_cast<int>(fake.sec), "sealed with uneven transaction times");
  }

  // Resume with the gauge half or fully unsealed after long uptime: the first
  // key must not burn the restart budget, and a lost pair window recovers.
  {
    FakeGauge half;
    half.sec = 2;
    gauge = &half;
    BatteryMonitor monitor = makeMonitor();
    testSetManualMillis(60000);
    unsigned long tick = testManualMillisValue;
    bool stalled = false;
    for (int i = 0; i < 4000; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      if (!stalled && half.sawWrite("W00=FFFF")) {
        tick += 5000;
        stalled = true;
      }
      testSetManualMillis(tick);
      if (!monitor.serviceDesignCapacity()) break;
    }
    runner.expectEq(TARGET, half.get(DM_DC), "sec 2 resume completes after a stall");
    runner.expectEq(3, static_cast<int>(half.sec), "sec 2 resume seals");
  }
  {
    FakeGauge open;
    open.sec = 1;
    gauge = &open;
    BatteryMonitor monitor = makeMonitor();
    testSetManualMillis(60000);
    unsigned long tick = testManualMillisValue;
    for (int i = 0; i < 4000; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      testSetManualMillis(tick);
      if (!monitor.serviceDesignCapacity()) break;
    }
    runner.expectEq(TARGET, open.get(DM_DC), "sec 1 resume completes at long uptime");
    runner.expectEq(3, static_cast<int>(open.sec), "sec 1 resume seals");
  }

  // ENTER_CFG_UPDATE lands asynchronously: sleep right after the entry write
  // must still end sealed and out of CONFIG UPDATE.
  {
    FakeGauge fake;
    fake.cfgSetDelayMs = 800;
    gauge = &fake;
    BatteryMonitor monitor = makeMonitor();
    unsigned long tick = testManualMillisValue;
    bool entered = false;
    for (int i = 0; i < 4000 && !entered; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      testSetManualMillis(tick);
      monitor.serviceDesignCapacity();
      entered = fake.sawWrite("W00=0090");
    }
    runner.expectTrue(entered, "entry was sent");
    runner.expectTrue(monitor.finishDesignCapacity(), "finish resolves a pending entry");
    runner.expectTrue(!fake.cfg, "pending entry exited before sleep");
    runner.expectTrue(!fake.entryPending, "no entry still pending");
    runner.expectEq(3, static_cast<int>(fake.sec), "sealed after pending entry");
  }

  // One failed exit write must not abandon the gauge in CONFIG UPDATE: the
  // machine retries the exit and completes.
  {
    FakeGauge fake;
    fake.refuseExitOnce = true;
    run(runner, fake);
    runner.expectEq(TARGET, fake.get(DM_DC), "load completes after a refused exit");
    runner.expectEq(TARGET, fake.get(DM_FCC), "FCC loads after a refused exit");
    runner.expectTrue(!fake.cfg, "ends out of CONFIG UPDATE after a refused exit");
    runner.expectEq(3, static_cast<int>(fake.sec), "sealed after a refused exit");
  }

  // Stalls before non-pair keys (first full-access key, ENTER_CFG_UPDATE) are
  // free: only pair-second keys own the 4 s deadline.
  {
    FakeGauge fake;
    gauge = &fake;
    BatteryMonitor monitor = makeMonitor();
    unsigned long tick = testManualMillisValue;
    bool stalledBeforeFullAccess = false;
    bool stalledBeforeEntry = false;
    for (int i = 0; i < 4000; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      if (!stalledBeforeFullAccess && fake.countWrite("W00=3672") >= 1) {
        tick += 5000;
        stalledBeforeFullAccess = true;
      }
      if (!stalledBeforeEntry && fake.countWrite("W00=FFFF") >= 2) {
        tick += 5000;
        stalledBeforeEntry = true;
      }
      testSetManualMillis(tick);
      if (!monitor.serviceDesignCapacity()) break;
    }
    runner.expectEq(TARGET, fake.get(DM_DC), "stalls at free keys still load");
    runner.expectEq(TARGET, fake.get(DM_FCC), "stalls at free keys still load FCC");
    runner.expectEq(3, static_cast<int>(fake.sec), "sealed after free-key stalls");
  }

  // Exit refusals beyond any retry budget keep the load pending — never done
  // with CONFIG UPDATE active — and the same monitor recovers once the bus
  // behaves again.
  {
    FakeGauge fake;
    fake.refuseExitsLeft = 12;
    gauge = &fake;
    BatteryMonitor monitor = makeMonitor();
    unsigned long tick = testManualMillisValue;
    bool pending = true;
    for (int i = 0; i < 6000; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      testSetManualMillis(tick);
      pending = monitor.serviceDesignCapacity();
      if (!pending) break;
    }
    runner.expectTrue(pending, "exhausted exit retries stay pending");
    runner.expectTrue(fake.cfg, "gauge still in CONFIG UPDATE while pending");
    runner.expectEq(TARGET, fake.get(DM_DC), "capacity written before the stuck exit");
    fake.refuseExitsLeft = 0;
    for (int i = 0; i < 6000 && pending; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      testSetManualMillis(tick);
      pending = monitor.serviceDesignCapacity();
    }
    runner.expectTrue(!pending, "load finishes after the bus recovers");
    runner.expectEq(TARGET, fake.get(DM_FCC), "FCC loads after the bus recovers");
    runner.expectTrue(!fake.cfg, "out of CONFIG UPDATE after recovery");
    runner.expectEq(3, static_cast<int>(fake.sec), "sealed after recovery");
  }

  // An accepted exit whose CFGUPDATE clear takes longer than the 5 s wait
  // must still end observed-clear, sealed, and loaded — never declared done
  // on a timeout.
  {
    FakeGauge fake;
    fake.cfgClearDelayMs = 20000;
    run(runner, fake);
    runner.expectEq(TARGET, fake.get(DM_DC), "slow clear still loads");
    runner.expectEq(1, fake.reinits, "slow clear exits exactly once");
    runner.expectTrue(!fake.cfg, "slow clear observed before done");
    runner.expectEq(3, static_cast<int>(fake.sec), "sealed after slow clear");
  }

  // Status-read failures after the exit must not fabricate completion either:
  // the load stays pending through the outage, then finishes.
  {
    FakeGauge fake;
    gauge = &fake;
    BatteryMonitor monitor = makeMonitor();
    unsigned long tick = testManualMillisValue;
    bool exited = false;
    bool pending = true;
    for (int i = 0; i < 1200; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      if (!exited && fake.sawWrite("W00=0091")) {
        exited = true;
        fake.refuseStatusReadsLeft = 300;  // outlast any wait in this phase
      }
      testSetManualMillis(tick);
      pending = monitor.serviceDesignCapacity();
      if (!pending) break;
    }
    runner.expectTrue(pending, "read outage keeps the load pending");
    fake.refuseStatusReadsLeft = 0;
    for (int i = 0; i < 8000 && pending; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      testSetManualMillis(tick);
      pending = monitor.serviceDesignCapacity();
    }
    runner.expectTrue(!pending, "load finishes after reads recover");
    runner.expectEq(TARGET, fake.get(DM_DC), "capacity loaded after read outage");
    runner.expectTrue(!fake.cfg, "out of CONFIG UPDATE after read outage");
    runner.expectEq(3, static_cast<int>(fake.sec), "sealed after read outage");
  }

  // A refused seal in the service path must not declare done either: the
  // machine stays pending and seals once the bus behaves again.
  {
    FakeGauge fake;
    fake.refuseSeal = true;
    gauge = &fake;
    BatteryMonitor monitor = makeMonitor();
    unsigned long tick = testManualMillisValue;
    bool pending = true;
    for (int i = 0; i < 1200; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      testSetManualMillis(tick);
      pending = monitor.serviceDesignCapacity();
      if (!pending) break;
    }
    runner.expectTrue(pending, "refused machine seal stays pending");
    runner.expectEq(TARGET, fake.get(DM_DC), "capacity written before the stuck seal");
    runner.expectEq(1, static_cast<int>(fake.sec), "honestly unsealed while the seal is refused");
    fake.refuseSeal = false;
    for (int i = 0; i < 8000 && pending; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      testSetManualMillis(tick);
      pending = monitor.serviceDesignCapacity();
    }
    runner.expectTrue(!pending, "machine finishes after the seal recovers");
    runner.expectEq(3, static_cast<int>(fake.sec), "sealed after the seal recovers");
    runner.expectTrue(!fake.cfg, "out of CONFIG UPDATE after the seal recovers");
  }






  // Sleep during the exit wait with a slow gauge: finish polls the real
  // CFGUPDATE state, then seals and verifies.
  {
    FakeGauge fake;
    fake.cfgClearDelayMs = 1200;
    gauge = &fake;
    BatteryMonitor monitor = makeMonitor();
    unsigned long tick = testManualMillisValue;
    bool exited = false;
    for (int i = 0; i < 4000 && !exited; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      testSetManualMillis(tick);
      monitor.serviceDesignCapacity();
      exited = fake.sawWrite("W00=0091");
    }
    runner.expectTrue(exited, "exit was sent");
    runner.expectTrue(fake.cfg, "gauge still in CONFIG UPDATE at interrupt");
    runner.expectTrue(monitor.finishDesignCapacity(), "finish closes a delayed exit");
    runner.expectTrue(!fake.cfg, "finish leaves CONFIG UPDATE");
    runner.expectEq(3, static_cast<int>(fake.sec), "finish seals the gauge");
  }

  // Failed seal reports failure and stays pending; the next call retries from
  // the actual gauge state and succeeds.
  {
    FakeGauge fake;
    fake.refuseSeal = true;
    gauge = &fake;
    BatteryMonitor monitor = makeMonitor();
    unsigned long tick = testManualMillisValue;
    for (int i = 0; i < 4000; ++i) {
      tick += 10;
      if (testManualMillisValue > tick) tick = testManualMillisValue;
      testSetManualMillis(tick);
      if (!monitor.serviceDesignCapacity()) break;
    }
    runner.expectTrue(!monitor.finishDesignCapacity(), "refused seal reports failure");
    runner.expectEq(1, static_cast<int>(fake.sec), "gauge honestly unsealed after failed close-out");
    fake.refuseSeal = false;
    runner.expectTrue(monitor.finishDesignCapacity(), "retry succeeds");
    runner.expectEq(3, static_cast<int>(fake.sec), "gauge sealed after retry");
  }

  // Every refused transaction ends out of CONFIG UPDATE where the exit was not
  // lost, and the next start finishes the job.
  for (const uint16_t dc : {uint16_t{3000}, TARGET}) {
    FakeGauge start;
    start.set(DM_DC, dc);
    if (dc == TARGET) {
      start.set(DM_FCC, 2744);
      start.fcc = 2744;
    }
    FakeGauge clean;
    clean.set(DM_DC, dc);
    if (dc == TARGET) {
      clean.set(DM_FCC, 2744);
      clean.fcc = 2744;
    }
    run(runner, clean);
    for (size_t refuse = 0; refuse < clean.log.size(); ++refuse) {
      FakeGauge fake = start;
      fake.refuse = static_cast<int>(refuse);
      run(runner, fake);
      const std::string& refused = clean.log[refuse];
      const bool lostExit = refused == "W00=0091" || refused == "W00=0092";
      if (!lostExit) {
        runner.expectTrue(!fake.cfg, "refused path ends out of CONFIG UPDATE");
      }
      fake.refuse = -1;
      fake.log.clear();
      run(runner, fake);
      runner.expectEq(TARGET, fake.get(DM_DC), "retry loads design capacity");
      runner.expectEq(TARGET, fake.get(DM_FCC), "retry loads learned FCC");
      runner.expectEq(3, static_cast<int>(fake.sec), "retry seals");
    }
  }

  return runner.allPassed() ? 0 : 1;
}
