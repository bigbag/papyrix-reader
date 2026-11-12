#include <WiFi.h>

#include "hal/WifiRadio.h"
#include "test_utils.h"

namespace {
uint32_t abortAt = 0;
bool abortConnection() { return static_cast<uint32_t>(millis()) >= abortAt; }
}  // namespace

int main() {
  TestUtils::TestRunner runner("WiFi radio lifecycle");
  testSetManualMillis(0);

  {
    WiFi.reset();
    WiFi.failMode = true;
    papyrix::hal::WifiRadio radio;
    runner.expectFalse(radio.init().ok(), "failed STA startup returns an error");
    runner.expectFalse(radio.isInitialized(), "failed STA startup does not publish initialized state");
  }
  {
    WiFi.reset();
    WiFi.failAP = true;
    papyrix::hal::WifiRadio radio;
    runner.expectFalse(radio.startAP("fixture-ap").ok(), "failed AP configuration returns an error");
    radio.shutdown();
    runner.expectEq(WIFI_OFF, WiFi.getMode(), "failed first AP startup releases the radio");
    runner.expectFalse(radio.isInitialized() || radio.isAPMode(), "failed AP startup clears HAL state");
  }
  {
    WiFi.reset();
    WiFi.failBandwidth = true;
    papyrix::hal::WifiRadio radio;
    const auto result = radio.startAP("fixture-ap");
    runner.expectTrue(result.err == papyrix::Error::NetworkFailed, "AP bandwidth failure returns a network error");
    runner.expectEq(WIFI_OFF, WiFi.getMode(), "AP bandwidth failure releases the radio");
    runner.expectFalse(radio.isInitialized() || radio.isAPMode(), "AP bandwidth failure clears HAL state");
    WiFi.failBandwidth = false;
    runner.expectTrue(radio.startAP("fixture-ap").ok(), "AP can start after a bandwidth failure");
    runner.expectTrue(radio.isAPMode(), "recovered AP publishes ownership");
    radio.shutdown();
  }
  {
    WiFi.reset();
    WiFi.failBegin = true;
    papyrix::hal::WifiRadio radio;
    const uint32_t started = millis();
    const auto result = radio.connect("fixture", "fixture-password", 15000, nullptr);
    runner.expectTrue(result.err == papyrix::Error::NetworkFailed, "immediate SDK rejection is not a timeout");
    runner.expectTrue(millis() - started < 1000, "immediate SDK rejection does not wait for the deadline");
    runner.expectEq(WIFI_OFF, WiFi.getMode(), "failed connection releases the radio");
  }
  {
    WiFi.reset();
    WiFi.connectDelayMs = 20000;
    papyrix::hal::WifiRadio radio;
    const auto result = radio.connect("fixture", "fixture-password", 1000, nullptr);
    runner.expectTrue(result.err == papyrix::Error::Timeout, "pending connection reaches its deadline");
    delay(25000);
    runner.expectTrue(!radio.isConnected() && WiFi.status() != WL_CONNECTED,
                      "timed-out attempt cannot acquire a late connection");
    runner.expectEq(WIFI_OFF, WiFi.getMode(), "timeout releases the driver");
  }
  {
    WiFi.reset();
    WiFi.connectDelayMs = 20000;
    papyrix::hal::WifiRadio radio;
    abortAt = millis() + 250;
    const auto result = radio.connect("fixture", "fixture-password", 15000, abortConnection);
    runner.expectTrue(result.err == papyrix::Error::InvalidOperation, "abort cancels the pending attempt");
    runner.expectTrue(millis() <= abortAt + 20, "abort stays within one polling interval");
    runner.expectEq(WIFI_OFF, WiFi.getMode(), "abort releases the driver");
  }
  {
    WiFi.reset();
    WiFi.connectDelayMs = 60;
    papyrix::hal::WifiRadio radio;
    runner.expectTrue(radio.connect("fixture", "fixture-password", 1000, nullptr).ok(),
                      "station can connect after earlier failures");
    runner.expectTrue(radio.isConnected(), "successful station connection publishes ownership");
    runner.expectTrue(radio.startAP("fixture-ap").ok(), "station can switch to hotspot mode");
    runner.expectTrue(radio.isAPMode() && !radio.isConnected(), "hotspot replaces station ownership");
    radio.shutdown();
    runner.expectEq(WIFI_OFF, WiFi.getMode(), "successful hotspot shutdown releases the radio");
  }

  testManualMillisEnabled = false;
  return runner.allPassed() ? 0 : 1;
}
