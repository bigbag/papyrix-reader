#include <SDCardManager.h>

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "network/WifiCredentialStore.h"
#include "test_utils.h"

int main() {
  TestUtils::TestRunner runner("WifiCredentialOrderTest");
  auto& store = papyrix::WIFI_STORE;
  SdMan.reset();
  store.clearAll();

  runner.expectTrue(store.addCredential("First", "one"), "Save first network without connecting");
  runner.expectTrue(store.addCredential("Second", "two"), "Save second network without connecting");
  runner.expectTrue(store.addCredential("Third", "three"), "Save third network without connecting");

  runner.expectTrue(store.moveCredential("Third", -1), "Manually move saved network up");
  runner.expectTrue(strcmp(store.getCredentials()[1].ssid, "Third") == 0, "Moved network comes before Second");
  runner.expectTrue(store.moveCredential("First", 1), "Manually move saved network down");
  runner.expectTrue(strcmp(store.getCredentials()[0].ssid, "Third") == 0,
                    "Recent tries manually selected network first");

  runner.expectTrue(store.promoteCredential("Second"), "Successful connection promotes network");
  runner.expectTrue(strcmp(store.getCredentials()[0].ssid, "Second") == 0, "Connected network becomes Recent");
  runner.expectTrue(store.loadFromFile(), "Stored order reloads from SD");
  runner.expectTrue(strcmp(store.getCredentials()[0].ssid, "Second") == 0, "Recent order survives restart");
  runner.expectTrue(strcmp(store.getCredentials()[1].ssid, "Third") == 0, "Manual order survives restart");
  runner.expectFalse(store.moveCredential("Second", -1), "Cannot move first network past start");
  runner.expectFalse(store.moveCredential("Unknown", 1), "Cannot move missing network");
  runner.expectTrue(store.updateCredential("Third", "Renamed", "updated"), "Edit saved SSID and password offline");
  runner.expectTrue(store.findCredential("Third") == nullptr, "Old SSID is removed after edit");
  runner.expectTrue(strcmp(store.getCredentials()[1].ssid, "Renamed") == 0, "Edited network keeps its priority");
  runner.expectTrue(strcmp(store.findCredential("Renamed")->password, "updated") == 0, "Edited password persists");
  runner.expectFalse(store.updateCredential("Renamed", "Second", "overwrite"),
                     "Cannot overwrite another saved network");
  runner.expectTrue(store.loadFromFile(), "Edited network reloads from SD");
  runner.expectTrue(strcmp(store.findCredential("Renamed")->password, "updated") == 0,
                    "Edited password survives restart");
  runner.expectTrue(store.removeCredential("Renamed"), "Forget selected network");
  runner.expectTrue(store.findCredential("Renamed") == nullptr, "Forgotten network is not offered for connection");
  runner.expectFalse(store.addCredential("", "secret"), "Reject empty SSID");
  runner.expectFalse(store.addCredential("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", "secret"),
                     "Reject SSIDs longer than 32 bytes");
  SdMan.setSyncResult(false);
  runner.expectFalse(store.moveCredential("Second", 1), "Reject priority change when SD sync fails");
  runner.expectTrue(strcmp(store.getCredentials()[0].ssid, "Second") == 0, "Failed reorder keeps in-memory priority");
  SdMan.setSyncResult(true);
  runner.expectTrue(store.loadFromFile(), "Read saved network order after failed write");
  runner.expectTrue(strcmp(store.getCredentials()[0].ssid, "Second") == 0, "Failed reorder keeps stored priority");
  SdMan.setRenameResult(false);
  runner.expectFalse(store.removeCredential("First"), "Reject forget when SD rename fails");
  runner.expectTrue(store.findCredential("First") != nullptr, "Failed forget keeps in-memory credential");
  SdMan.setRenameResult(true);
  runner.expectTrue(store.loadFromFile(), "Failed forget keeps credential file readable");
  runner.expectTrue(store.findCredential("First") != nullptr, "Failed forget does not erase stored password");

  auto record = [](const std::string& ssid, const std::string& password) {
    constexpr char key[] = {'P', 'a', 'p', 'y', 'r', 'i', 'x'};
    std::string data(1, static_cast<char>(ssid.size()));
    data += ssid;
    data.push_back(static_cast<char>(password.size()));
    for (size_t i = 0; i < password.size(); ++i) data.push_back(password[i] ^ key[i % sizeof(key)]);
    return data;
  };
  const std::string valid = std::string("\1\1", 2) + record("Keep", "pass");
  const std::vector<std::string> damaged = {
      {},
      std::string("\1", 1),
      std::string("\2\0", 2),
      std::string({1, 9}),
      std::string("\1\1", 2),
      std::string("\1\1\0", 3),
      std::string("\1\1", 2) + record(std::string(33, 'S'), "pass"),
      std::string("\1\1\4Ke", 5),
      valid.substr(0, 7),
      std::string("\1\1", 2) + record("Keep", std::string(65, 'P')),
      valid.substr(0, valid.size() - 1),
      std::string("\1\2", 2) + record("Other", "changed"),
      std::string("\1\1", 2) + record(std::string("A\0B", 3), "pass"),
      std::string("\1\1", 2) + record("Keep", std::string("a\0b", 3)),
  };
  for (const auto& data : damaged) {
    SdMan.registerFile(PAPYRIX_WIFI_FILE, valid);
    assert(store.loadFromFile());
    SdMan.registerFile(PAPYRIX_WIFI_FILE, data);
    runner.expectFalse(store.loadFromFile(), "Reject damaged credential file");
    const auto* kept = store.findCredential("Keep");
    runner.expectTrue(store.getCount() == 1 && kept && strcmp(kept->password, "pass") == 0,
                      "Failed load preserves the last complete credentials");
  }
  const std::string maxSsid(32, 'S');
  const std::string maxPassword(64, 'P');
  SdMan.registerFile(PAPYRIX_WIFI_FILE, std::string("\1\2", 2) + record(maxSsid, maxPassword) + record("Open", ""));
  runner.expectTrue(store.loadFromFile(), "Legacy boundary-length credentials and open networks load");
  const auto* maximum = store.findCredential(maxSsid.c_str());
  const auto* open = store.findCredential("Open");
  runner.expectTrue(store.getCount() == 2 && maximum && strcmp(maximum->password, maxPassword.c_str()) == 0 && open &&
                        open->password[0] == '\0',
                    "Valid version 1 credentials keep their complete bytes");

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
