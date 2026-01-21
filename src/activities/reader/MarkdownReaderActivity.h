/**
 * MarkdownReaderActivity.h
 *
 * Markdown reader activity for Papyrix Reader
 * Displays Markdown files with formatting support
 */

#pragma once

#include <Markdown.h>
#include <Markdown/Section.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "activities/ActivityWithSubactivity.h"

class MarkdownReaderActivity final : public ActivityWithSubactivity {
  std::shared_ptr<Markdown> markdown;
  std::unique_ptr<MarkdownSection> section = nullptr;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  int nextPageNumber = 0;
  int pagesUntilFullRefresh = 0;
  int renderRetryCount = 0;
  bool updateRequired = false;
  const std::function<void()> onGoBack;
  const std::function<void()> onGoHome;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void renderScreen();
  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar(int orientedMarginRight, int orientedMarginBottom, int orientedMarginLeft) const;
  void renderTitlePage(int orientedMarginTop, int orientedMarginRight, int orientedMarginBottom,
                       int orientedMarginLeft);

 public:
  explicit MarkdownReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                  std::unique_ptr<Markdown> markdown, const std::function<void()>& onGoBack,
                                  const std::function<void()>& onGoHome)
      : ActivityWithSubactivity("MarkdownReader", renderer, mappedInput),
        markdown(std::move(markdown)),
        onGoBack(onGoBack),
        onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
