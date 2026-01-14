#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "../Activity.h"
#include "opds/OpdsServerStore.h"

class OpdsServerListActivity final : public Activity {
 public:
  explicit OpdsServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  const std::function<void()>& onGoBack,
                                  const std::function<void(const OpdsServerConfig&)>& onServerSelected)
      : Activity("OpdsServerList", renderer, mappedInput), onGoBack(onGoBack), onServerSelected(onServerSelected) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  int selectedIndex = 0;
  std::vector<std::string> serverNames;  // Like FileSelectionActivity::files

  const std::function<void()> onGoBack;
  const std::function<void(const OpdsServerConfig&)> onServerSelected;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;
};
