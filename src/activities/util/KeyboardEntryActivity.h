#pragma once
#include <GfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <utility>

#include "../Activity.h"

/**
 * Reusable keyboard entry activity for text input.
 * Can be started from any activity that needs text entry.
 *
 * Usage:
 *   1. Create a KeyboardEntryActivity instance
 *   2. Set callbacks with setOnComplete() and setOnCancel()
 *   3. Call onEnter() to start the activity
 *   4. Call loop() in your main loop
 *   5. When complete or cancelled, callbacks will be invoked
 */
class KeyboardEntryActivity : public Activity {
 public:
  // Callback types
  using OnCompleteCallback = std::function<void(const std::string&)>;
  using OnCancelCallback = std::function<void()>;

  /**
   * Constructor
   * @param renderer Reference to the GfxRenderer for drawing
   * @param mappedInput Reference to MappedInputManager for handling input
   * @param title Title to display above the keyboard
   * @param initialText Initial text to show in the input field
   * @param startY Y position to start rendering the keyboard
   * @param maxLength Maximum length of input text (0 for unlimited)
   * @param isPassword If true, display asterisks instead of actual characters
   * @param onComplete Callback invoked when input is complete
   * @param onCancel Callback invoked when input is cancelled
   */
  explicit KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string title = "Enter Text", std::string initialText = "", const int startY = 10,
                                 const size_t maxLength = 0, const bool isPassword = false,
                                 OnCompleteCallback onComplete = nullptr, OnCancelCallback onCancel = nullptr)
      : Activity("KeyboardEntry", renderer, mappedInput),
        title(std::move(title)),
        text(std::move(initialText)),
        startY(startY),
        maxLength(maxLength),
        isPassword(isPassword),
        onComplete(std::move(onComplete)),
        onCancel(std::move(onCancel)) {}

  // Activity overrides
  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  std::string title;
  int startY;
  std::string text;
  size_t maxLength;
  bool isPassword;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  // Keyboard state - start on first letter row (row 1), not control row
  int selectedRow = 1;
  int selectedCol = 0;

  // Callbacks
  OnCompleteCallback onComplete;
  OnCancelCallback onCancel;

  // Keyboard layout - Full Grid (10 rows x 10 columns)
  // Control row at top for easy access to Backspace, Space, Confirm
  static constexpr int NUM_ROWS = 10;
  static constexpr int KEYS_PER_ROW = 10;
  static constexpr char keyboard[NUM_ROWS][KEYS_PER_ROW] = {
      {'\x02', '\x02', '\x02', '\x01', '\x01', '\x01', '\x01', '\x03', '\x03', '\x03'},  // row 0: controls
      {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'},                                // row 1: lowercase
      {'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't'},                                // row 2: lowercase
      {'u', 'v', 'w', 'x', 'y', 'z', '.', '-', '_', '@'},                                // row 3: lowercase + symbols
      {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'},                                // row 4: uppercase
      {'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T'},                                // row 5: uppercase
      {'U', 'V', 'W', 'X', 'Y', 'Z', '!', '#', '$', '%'},                                // row 6: uppercase + symbols
      {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},                                // row 7: numbers
      {'^', '&', '*', '(', ')', '+', ' ', '[', ']', '\\'},                               // row 8: symbols
      {'/', ':', ';', '~', '?', '=', '\'', '"', ',', '<'}                                // row 9: URL/extra symbols
  };
  // Control characters: \x01 = SPACE, \x02 = BACKSPACE, \x03 = CONFIRM

  // Control row (row 0) key positions - BACKSPACE, SPACE, CONFIRM
  static constexpr int CONTROL_ROW = 0;
  static constexpr int BACKSPACE_START = 0;
  static constexpr int BACKSPACE_END = 2;  // cols 0-2 (3 keys wide)
  static constexpr int SPACE_START = 3;
  static constexpr int SPACE_END = 6;  // cols 3-6 (4 keys wide)
  static constexpr int CONFIRM_START = 7;
  static constexpr int CONFIRM_END = 9;  // cols 7-9 (3 keys wide)

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  char getSelectedChar() const;
  void handleKeyPress();
  void render() const;
  void renderItemWithSelector(int x, int y, const char* item, bool isSelected) const;
};
