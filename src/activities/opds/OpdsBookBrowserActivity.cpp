#include "OpdsBookBrowserActivity.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "ThemeManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "config.h"
#include "network/HttpDownloader.h"

namespace {
constexpr int PAGE_ITEMS = 10;
constexpr int SKIP_PAGE_MS = 700;

std::string ensureProtocol(const std::string& url) {
  if (url.find("://") == std::string::npos) {
    return "http://" + url;
  }
  return url;
}

std::string extractHost(const std::string& url) {
  const size_t protocolEnd = url.find("://");
  if (protocolEnd == std::string::npos) {
    const size_t firstSlash = url.find('/');
    return firstSlash == std::string::npos ? url : url.substr(0, firstSlash);
  }
  const size_t hostStart = protocolEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  return pathStart == std::string::npos ? url : url.substr(0, pathStart);
}

std::string buildUrl(const std::string& serverUrl, const std::string& path) {
  const std::string urlWithProtocol = ensureProtocol(serverUrl);
  if (path.empty()) {
    return urlWithProtocol;
  }
  if (path[0] == '/') {
    return extractHost(urlWithProtocol) + path;
  }
  if (urlWithProtocol.back() == '/') {
    return urlWithProtocol + path;
  }
  return urlWithProtocol + "/" + path;
}

std::string truncateWithEllipsis(const std::string& str, size_t maxLen) {
  if (str.length() <= maxLen) return str;
  return str.substr(0, maxLen - 3) + "...";
}

std::string urlEncode(const std::string& input) {
  std::string result;
  result.reserve(input.size() * 3);  // Worst case: every char encoded

  for (const unsigned char c : input) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      result += static_cast<char>(c);
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", c);
      result += hex;
    }
  }
  return result;
}

std::string stripOptionalParams(const std::string& tmpl) {
  std::string result = tmpl;
  // Remove optional params like {startPage?}, {count?} etc.
  size_t pos = 0;
  while ((pos = result.find("{", pos)) != std::string::npos) {
    const size_t endPos = result.find("}", pos);
    if (endPos == std::string::npos) break;

    // Check if it's an optional param (ends with ?)
    if (endPos > pos + 1 && result[endPos - 1] == '?') {
      // Remove the parameter and any preceding &
      // But NOT the ? (first param separator)
      size_t removeStart = pos;
      if (removeStart > 0 && result[removeStart - 1] == '&') {
        removeStart--;
      }
      result.erase(removeStart, endPos - removeStart + 1);
      pos = removeStart;
    } else {
      pos = endPos + 1;
    }
  }

  // Clean up orphaned query separators (e.g., "?&" -> "?", trailing "?")
  size_t qmark = result.find('?');
  if (qmark != std::string::npos) {
    // Remove ?& -> ?
    while (qmark + 1 < result.size() && result[qmark + 1] == '&') {
      result.erase(qmark + 1, 1);
    }
    // Remove trailing ?
    if (qmark == result.size() - 1) {
      result.erase(qmark, 1);
    }
  }

  return result;
}
}  // namespace

void OpdsBookBrowserActivity::taskTrampoline(void* param) {
  auto* self = static_cast<OpdsBookBrowserActivity*>(param);
  self->displayTaskLoop();
}

void OpdsBookBrowserActivity::checkWifiConnection() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[%lu] [OPDS] WiFi already connected\n", millis());
    state = BrowserState::LOADING;
    statusMessage = "Loading...";
    updateRequired = true;
    fetchFeed(currentPath);
    return;
  }

  Serial.printf("[%lu] [OPDS] Launching WiFi selection...\n", millis());

  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void OpdsBookBrowserActivity::onWifiSelectionComplete(const bool success) {
  exitActivity();

  if (!success) {
    Serial.printf("[%lu] [OPDS] WiFi connection failed\n", millis());
    onGoBack();
    return;
  }

  Serial.printf("[%lu] [OPDS] WiFi connected, loading feed\n", millis());
  state = BrowserState::LOADING;
  statusMessage = "Loading...";
  updateRequired = true;
  fetchFeed(currentPath);
}

void OpdsBookBrowserActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  state = BrowserState::WIFI_CHECK;
  entries.clear();
  navigationHistory.clear();
  currentPath.clear();
  currentSearchTemplate.clear();
  selectorIndex = 0;
  errorMessage.clear();
  statusMessage = "Connecting...";

  xTaskCreate(&OpdsBookBrowserActivity::taskTrampoline, "OpdsBookBrowserTask", 4096, this, 1, &displayTaskHandle);

  // Turn on WiFi
  Serial.printf("[%lu] [OPDS] Turning on WiFi...\n", millis());
  WiFi.mode(WIFI_STA);

  checkWifiConnection();
}

void OpdsBookBrowserActivity::onExit() {
  ActivityWithSubactivity::onExit();

  Serial.printf("[%lu] [OPDS] [MEM] Free heap at onExit start: %d bytes\n", millis(), ESP.getFreeHeap());

  // Turn off wifi
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);

  Serial.printf("[%lu] [OPDS] [MEM] Free heap after WiFi off: %d bytes\n", millis(), ESP.getFreeHeap());

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  entries.clear();
  navigationHistory.clear();

  // WiFi fragments heap memory permanently on ESP32
  // Restart is required to read XTC books after using WiFi
  Serial.printf("[%lu] [OPDS] Restarting to reclaim memory...\n", millis());
  ESP.restart();
}

void OpdsBookBrowserActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (state == BrowserState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      state = BrowserState::LOADING;
      statusMessage = "Loading...";
      updateRequired = true;
      fetchFeed(currentPath);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    }
    return;
  }

  if (state == BrowserState::LOADING || state == BrowserState::WIFI_CHECK) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    }
    return;
  }

  if (state == BrowserState::DOWNLOADING) {
    return;
  }

  if (state == BrowserState::BROWSING) {
    const bool prevReleased = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool nextReleased = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right);
    const bool skipPage = mappedInput.getHeldTime() > SKIP_PAGE_MS;

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!entries.empty()) {
        const auto& entry = entries[selectorIndex];
        if (entry.type == OpdsEntryType::BOOK) {
          downloadBook(entry);
        } else {
          navigateToEntry(entry);
        }
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      navigateBack();
    } else if (prevReleased && !entries.empty()) {
      if (skipPage) {
        selectorIndex = ((selectorIndex / PAGE_ITEMS - 1) * PAGE_ITEMS + static_cast<int>(entries.size())) %
                        static_cast<int>(entries.size());
      } else {
        selectorIndex = (selectorIndex + static_cast<int>(entries.size()) - 1) % static_cast<int>(entries.size());
      }
      updateRequired = true;
    } else if (nextReleased && !entries.empty()) {
      if (skipPage) {
        selectorIndex = ((selectorIndex / PAGE_ITEMS + 1) * PAGE_ITEMS) % static_cast<int>(entries.size());
      } else {
        selectorIndex = (selectorIndex + 1) % static_cast<int>(entries.size());
      }
      updateRequired = true;
    }
  }
}

void OpdsBookBrowserActivity::displayTaskLoop() {
  while (true) {
    if (subActivity) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void OpdsBookBrowserActivity::render() const {
  if (subActivity) {
    return;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen(THEME.backgroundColor);
  renderer.drawCenteredText(THEME.readerFontId, 10, "OPDS Library", THEME.primaryTextBlack, BOLD);

  if (state == BrowserState::WIFI_CHECK || state == BrowserState::LOADING) {
    renderer.drawCenteredText(THEME.uiFontId, pageHeight / 2, statusMessage.c_str(), THEME.primaryTextBlack);
    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    renderer.drawButtonHints(THEME.uiFontId, labels.btn1, labels.btn2, labels.btn3, labels.btn4,
                             THEME.primaryTextBlack);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::ERROR) {
    renderer.drawCenteredText(THEME.uiFontId, pageHeight / 2 - 20, "Error:", THEME.primaryTextBlack);
    renderer.drawCenteredText(THEME.uiFontId, pageHeight / 2 + 10, errorMessage.c_str(), THEME.primaryTextBlack);
    const auto labels = mappedInput.mapLabels("Back", "Retry", "", "");
    renderer.drawButtonHints(THEME.uiFontId, labels.btn1, labels.btn2, labels.btn3, labels.btn4,
                             THEME.primaryTextBlack);
    renderer.displayBuffer();
    return;
  }

  if (state == BrowserState::DOWNLOADING) {
    renderer.drawCenteredText(THEME.uiFontId, pageHeight / 2 - 40, "Downloading...", THEME.primaryTextBlack, BOLD);
    const std::string truncatedTitle = truncateWithEllipsis(statusMessage, 40);
    renderer.drawCenteredText(THEME.uiFontId, pageHeight / 2 - 10, truncatedTitle.c_str(), THEME.primaryTextBlack);
    if (downloadTotal > 0) {
      const int percent = static_cast<int>((downloadProgress * 100) / downloadTotal);
      char progressText[32];
      snprintf(progressText, sizeof(progressText), "%d%%", percent);
      renderer.drawCenteredText(THEME.uiFontId, pageHeight / 2 + 20, progressText, THEME.primaryTextBlack);
      // Draw progress bar
      const int barWidth = 300;
      const int barHeight = 20;
      const int barX = (pageWidth - barWidth) / 2;
      const int barY = pageHeight / 2 + 50;
      renderer.drawRect(barX, barY, barWidth, barHeight, THEME.primaryTextBlack);
      const int fillWidth = static_cast<int>((downloadProgress * (barWidth - 4)) / downloadTotal);
      renderer.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, THEME.selectionFillBlack);
    }
    renderer.displayBuffer();
    return;
  }

  // BROWSING state
  const char* confirmLabel = "Open";
  if (!entries.empty() && entries[selectorIndex].type == OpdsEntryType::BOOK) {
    confirmLabel = "Save";
  }
  const auto labels = mappedInput.mapLabels("Back", confirmLabel, "Up", "Down");
  renderer.drawButtonHints(THEME.uiFontId, labels.btn1, labels.btn2, labels.btn3, labels.btn4, THEME.primaryTextBlack);

  if (entries.empty()) {
    renderer.drawCenteredText(THEME.uiFontId, pageHeight / 2, "No entries found", THEME.primaryTextBlack);
    renderer.displayBuffer();
    return;
  }

  constexpr int startY = 50;
  constexpr int itemHeight = 55;
  constexpr int leftMargin = 15;

  const int pageStartIndex = (selectorIndex / PAGE_ITEMS) * PAGE_ITEMS;

  for (int i = pageStartIndex; i < static_cast<int>(entries.size()) && i < pageStartIndex + PAGE_ITEMS; i++) {
    const auto& entry = entries[i];
    const int y = startY + (i % PAGE_ITEMS) * itemHeight;
    const bool isSelected = (i == selectorIndex);

    std::string displayTitle;
    if (entry.type == OpdsEntryType::NAVIGATION) {
      displayTitle = "> " + entry.title;
    } else {
      displayTitle = entry.title;
    }
    displayTitle = truncateWithEllipsis(displayTitle, 40);

    if (isSelected) {
      renderer.drawText(THEME.uiFontId, leftMargin, y, displayTitle.c_str(), THEME.primaryTextBlack, BOLD);
    } else {
      renderer.drawText(THEME.uiFontId, leftMargin, y, displayTitle.c_str(), THEME.primaryTextBlack);
    }

    // Show author for books
    if (entry.type == OpdsEntryType::BOOK && !entry.author.empty()) {
      const std::string displayAuthor = truncateWithEllipsis(entry.author, 45);
      renderer.drawText(THEME.smallFontId, leftMargin + 10, y + 25, displayAuthor.c_str(), THEME.primaryTextBlack);
    }
  }

  // Page indicator
  const int totalPages = (static_cast<int>(entries.size()) + PAGE_ITEMS - 1) / PAGE_ITEMS;
  const int currentPage = (selectorIndex / PAGE_ITEMS) + 1;
  if (totalPages > 1) {
    char pageInfo[32];
    snprintf(pageInfo, sizeof(pageInfo), "%d / %d", currentPage, totalPages);
    renderer.drawText(THEME.smallFontId, pageWidth - 80, pageHeight - 80, pageInfo, THEME.primaryTextBlack);
  }

  renderer.displayBuffer();
}

void OpdsBookBrowserActivity::fetchFeed(const std::string& path) {
  if (serverConfig.url.empty()) {
    state = BrowserState::ERROR;
    errorMessage = "No server URL configured";
    updateRequired = true;
    return;
  }

  std::string url = buildUrl(serverConfig.url, path);
  Serial.printf("[%lu] [OPDS] Fetching: %s\n", millis(), url.c_str());
  Serial.printf("[%lu] [OPDS] [MEM] Free heap before fetch: %d bytes\n", millis(), ESP.getFreeHeap());

  OpdsParser parser;
  if (!parser.startParsing()) {
    state = BrowserState::ERROR;
    errorMessage = "Parser init failed";
    updateRequired = true;
    return;
  }

  constexpr size_t MAX_ENTRIES = 50;
  bool parseError = false;

  const bool fetchOk = HttpDownloader::fetchUrlStreaming(
      url,
      [&parser, &parseError, MAX_ENTRIES](const char* chunk, size_t len) -> bool {
        if (!parser.feedChunk(chunk, len)) {
          parseError = true;
          return false;  // Abort on parse error
        }
        // Stop early if we have enough entries
        if (parser.getEntryCount() >= MAX_ENTRIES) {
          Serial.printf("[%lu] [OPDS] Reached %zu entries, stopping early\n", millis(), parser.getEntryCount());
          return false;
        }
        return true;
      },
      serverConfig.username, serverConfig.password);

  if (!fetchOk && !parseError && parser.getEntryCount() == 0) {
    state = BrowserState::ERROR;
    errorMessage = "Failed to fetch feed";
    updateRequired = true;
    return;
  }

  // Finalize parsing (may fail if we aborted early, which is OK)
  parser.finishParsing();

  Serial.printf("[%lu] [OPDS] [MEM] Free heap after parse: %d bytes\n", millis(), ESP.getFreeHeap());

  entries = parser.getEntries();

  // Extract search template (prefer direct template over OpenSearch description)
  currentSearchTemplate = parser.getSearchTemplate();
  if (currentSearchTemplate.empty() && !parser.getOpenSearchUrl().empty()) {
    // Need to fetch OpenSearch description
    currentSearchTemplate = fetchOpenSearchTemplate(buildUrl(serverConfig.url, parser.getOpenSearchUrl()));
  }

  // Inject search entry at top if search is available
  if (!currentSearchTemplate.empty()) {
    OpdsEntry searchEntry;
    searchEntry.type = OpdsEntryType::NAVIGATION;
    searchEntry.title = "Search...";
    searchEntry.href = "__SEARCH__";
    searchEntry.id = "__SEARCH__";
    entries.insert(entries.begin(), searchEntry);
    Serial.printf("[%lu] [OPDS] Injected search entry\n", millis());
  }

  selectorIndex = 0;

  if (entries.empty()) {
    state = BrowserState::ERROR;
    errorMessage = parseError ? "Failed to parse feed" : "No entries found";
    updateRequired = true;
    return;
  }

  state = BrowserState::BROWSING;
  updateRequired = true;
}

void OpdsBookBrowserActivity::navigateToEntry(const OpdsEntry& entry) {
  // Special handling for search entry
  if (entry.href == "__SEARCH__") {
    handleSearchEntry();
    return;
  }

  navigationHistory.push_back(currentPath);
  currentPath = entry.href;

  state = BrowserState::LOADING;
  statusMessage = "Loading...";
  entries.clear();
  selectorIndex = 0;
  updateRequired = true;

  fetchFeed(currentPath);
}

void OpdsBookBrowserActivity::navigateBack() {
  if (navigationHistory.empty()) {
    onGoBack();
  } else {
    currentPath = navigationHistory.back();
    navigationHistory.pop_back();

    state = BrowserState::LOADING;
    statusMessage = "Loading...";
    entries.clear();
    selectorIndex = 0;
    updateRequired = true;

    fetchFeed(currentPath);
  }
}

void OpdsBookBrowserActivity::downloadBook(const OpdsEntry& book) {
  state = BrowserState::DOWNLOADING;
  statusMessage = book.title;
  downloadProgress = 0;
  downloadTotal = 0;
  updateRequired = true;

  std::string downloadUrl = buildUrl(serverConfig.url, book.href);
  std::string filename = "/Books/" + sanitizeFilename(book.title) + ".epub";

  Serial.printf("[%lu] [OPDS] Downloading: %s -> %s\n", millis(), downloadUrl.c_str(), filename.c_str());

  // Ensure /Books directory exists
  if (!SdMan.exists("/Books")) {
    if (!SdMan.mkdir("/Books")) {
      Serial.printf("[%lu] [OPDS] Failed to create /Books directory\n", millis());
      state = BrowserState::ERROR;
      errorMessage = "SD card error";
      updateRequired = true;
      return;
    }
  }

  const auto result = HttpDownloader::downloadToFile(
      downloadUrl, filename,
      [this](const size_t downloaded, const size_t total) {
        downloadProgress = downloaded;
        downloadTotal = total;
        updateRequired = true;
      },
      serverConfig.username, serverConfig.password);

  if (result == HttpDownloader::OK) {
    Serial.printf("[%lu] [OPDS] Download complete: %s\n", millis(), filename.c_str());
    state = BrowserState::BROWSING;
    updateRequired = true;
  } else {
    state = BrowserState::ERROR;
    errorMessage = "Download failed";
    updateRequired = true;
  }
}

std::string OpdsBookBrowserActivity::sanitizeFilename(const std::string& title) const {
  std::string result;
  result.reserve(title.size());

  for (const unsigned char c : title) {
    // Replace forbidden filesystem characters
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
      result += '_';
    } else if (c >= 32 && c != 127) {
      // Allow printable ASCII (32-126) and UTF-8 continuation bytes (128-255)
      result += static_cast<char>(c);
    }
    // Skip control characters (0-31, 127)
  }

  size_t start = result.find_first_not_of(" .");
  if (start == std::string::npos) {
    return "book";
  }
  size_t end = result.find_last_not_of(" .");
  result = result.substr(start, end - start + 1);

  if (result.length() > 100) {
    result.resize(100);
  }

  return result.empty() ? "book" : result;
}

std::string OpdsBookBrowserActivity::fetchOpenSearchTemplate(const std::string& url) {
  Serial.printf("[%lu] [OPDS] Fetching OpenSearch description: %s\n", millis(), url.c_str());

  std::string content;
  if (!HttpDownloader::fetchUrl(url, content, serverConfig.username, serverConfig.password)) {
    Serial.printf("[%lu] [OPDS] Failed to fetch OpenSearch description\n", millis());
    return "";
  }

  // Parse OpenSearch XML - look for <Url ... template="..." type="application/atom+xml"/>
  const std::string typeMarker = "application/atom+xml";
  const std::string templateMarker = "template=\"";

  // Find Url element with atom+xml type
  size_t urlPos = 0;
  while ((urlPos = content.find("<Url", urlPos)) != std::string::npos) {
    const size_t urlEnd = content.find(">", urlPos);
    if (urlEnd == std::string::npos) break;

    const std::string urlElement = content.substr(urlPos, urlEnd - urlPos + 1);

    // Check if this Url element has the right type
    if (urlElement.find(typeMarker) != std::string::npos) {
      // Extract template attribute
      const size_t tmplStart = urlElement.find(templateMarker);
      if (tmplStart != std::string::npos) {
        const size_t valueStart = tmplStart + templateMarker.length();
        const size_t valueEnd = urlElement.find("\"", valueStart);
        if (valueEnd != std::string::npos) {
          std::string tmpl = urlElement.substr(valueStart, valueEnd - valueStart);
          Serial.printf("[%lu] [OPDS] Extracted search template: %s\n", millis(), tmpl.c_str());
          return tmpl;
        }
      }
    }
    urlPos = urlEnd;
  }

  Serial.printf("[%lu] [OPDS] No search template found in OpenSearch description\n", millis());
  return "";
}

void OpdsBookBrowserActivity::handleSearchEntry() {
  Serial.printf("[%lu] [OPDS] Opening search keyboard\n", millis());

  enterNewActivity(new KeyboardEntryActivity(
      renderer, mappedInput,
      "Search",  // title
      "",        // initialText
      10,        // startY
      100,       // maxLength
      false,     // isPassword
      [this](const std::string& searchTerm) {
        // onComplete callback
        exitActivity();

        if (searchTerm.empty()) {
          // User submitted empty search, just return to browsing
          updateRequired = true;
          return;
        }

        // Build search URL by replacing {searchTerms} with encoded input
        std::string searchUrl = stripOptionalParams(currentSearchTemplate);
        const std::string placeholder = "{searchTerms}";
        const size_t pos = searchUrl.find(placeholder);

        if (pos != std::string::npos) {
          const std::string encoded = urlEncode(searchTerm);
          searchUrl.replace(pos, placeholder.length(), encoded);
        }

        Serial.printf("[%lu] [OPDS] Search URL: %s\n", millis(), searchUrl.c_str());

        // Navigate to search results
        navigationHistory.push_back(currentPath);
        currentPath = searchUrl;

        state = BrowserState::LOADING;
        statusMessage = "Searching...";
        entries.clear();
        selectorIndex = 0;
        updateRequired = true;

        fetchFeed(currentPath);
      },
      [this]() {
        // onCancel callback
        exitActivity();
        updateRequired = true;
      }));
}
