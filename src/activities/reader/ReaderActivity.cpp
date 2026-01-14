#include "ReaderActivity.h"

#include <esp_heap_caps.h>

#include "Epub.h"
#include "EpubReaderActivity.h"
#include "FileSelectionActivity.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "config.h"
#include "util/StringUtils.h"

std::string ReaderActivity::extractFolderPath(const std::string& filePath) {
  const auto lastSlash = filePath.find_last_of('/');
  if (lastSlash == std::string::npos || lastSlash == 0) {
    return "/";
  }
  return filePath.substr(0, lastSlash);
}

std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
  if (!SdMan.exists(path.c_str())) {
    Serial.printf("[%lu] [   ] File does not exist: %s\n", millis(), path.c_str());
    return nullptr;
  }

  auto epub = std::unique_ptr<Epub>(new Epub(path, PAPYRIX_DIR));
  if (epub->load()) {
    return epub;
  }

  Serial.printf("[%lu] [   ] Failed to load epub\n", millis());
  return nullptr;
}

std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  if (!SdMan.exists(path.c_str())) {
    Serial.printf("[%lu] [   ] File does not exist: %s\n", millis(), path.c_str());
    return nullptr;
  }

  auto xtc = std::unique_ptr<Xtc>(new Xtc(path, PAPYRIX_DIR));
  if (xtc->load()) {
    return xtc;
  }

  Serial.printf("[%lu] [   ] Failed to load XTC\n", millis());
  return nullptr;
}

std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  if (!SdMan.exists(path.c_str())) {
    Serial.printf("[%lu] [   ] File does not exist: %s\n", millis(), path.c_str());
    return nullptr;
  }

  auto txt = std::unique_ptr<Txt>(new Txt(path, PAPYRIX_DIR));
  if (txt->load()) {
    return txt;
  }

  Serial.printf("[%lu] [   ] Failed to load TXT\n", millis());
  return nullptr;
}

void ReaderActivity::onSelectBookFile(const std::string& path) {
  currentBookPath = path;  // Track current book path
  exitActivity();
  enterNewActivity(new FullScreenMessageActivity(renderer, mappedInput, "Loading..."));

  if (StringUtils::isXtcFile(path)) {
    // Check if we have enough contiguous memory for XTC loading
    // After WiFi use, heap can be fragmented even with plenty of free memory
    const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    Serial.printf("[%lu] [XTC] Largest free block: %zu bytes, free heap: %d\n", millis(), largestBlock,
                  ESP.getFreeHeap());

    // Need at least 130KB contiguous: ~30KB for page table + 96KB for page buffer + margin
    if (largestBlock < 130000) {
      // Memory too fragmented - suggest restart
      Serial.printf("[%lu] [XTC] Memory fragmented (largest block %zu < 130KB), need restart\n", millis(),
                    largestBlock);
      exitActivity();
      enterNewActivity(new FullScreenMessageActivity(renderer, mappedInput, "Low memory. Please restart device.",
                                                     REGULAR, EInkDisplay::HALF_REFRESH));
      delay(3000);
      onGoToFileSelection();
      return;
    }

    // Load XTC file
    auto xtc = loadXtc(path);
    if (xtc) {
      onGoToXtcReader(std::move(xtc));
    } else {
      exitActivity();
      enterNewActivity(new FullScreenMessageActivity(renderer, mappedInput, "Failed to load XTC", REGULAR,
                                                     EInkDisplay::HALF_REFRESH));
      delay(2000);
      onGoToFileSelection();
    }
  } else if (StringUtils::isTxtFile(path)) {
    // Load TXT file
    auto txt = loadTxt(path);
    if (txt) {
      onGoToTxtReader(std::move(txt));
    } else {
      exitActivity();
      enterNewActivity(new FullScreenMessageActivity(renderer, mappedInput, "Failed to load TXT", REGULAR,
                                                     EInkDisplay::HALF_REFRESH));
      delay(2000);
      onGoToFileSelection();
    }
  } else {
    // Load EPUB file
    auto epub = loadEpub(path);
    if (epub) {
      onGoToEpubReader(std::move(epub));
    } else {
      exitActivity();
      enterNewActivity(new FullScreenMessageActivity(renderer, mappedInput, "Failed to load epub", REGULAR,
                                                     EInkDisplay::HALF_REFRESH));
      delay(2000);
      onGoToFileSelection();
    }
  }
}

void ReaderActivity::onGoToFileSelection(const std::string& fromBookPath) {
  exitActivity();
  // If coming from a book, start in that book's folder; otherwise start from root
  const auto initialPath = fromBookPath.empty() ? "/" : extractFolderPath(fromBookPath);
  enterNewActivity(new FileSelectionActivity(
      renderer, mappedInput, [this](const std::string& path) { onSelectBookFile(path); }, onGoBack, initialPath));
}

void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  const auto epubPath = epub->getPath();
  currentBookPath = epubPath;
  exitActivity();
  enterNewActivity(new EpubReaderActivity(
      renderer, mappedInput, std::move(epub), [this, epubPath] { onGoToFileSelection(epubPath); },
      [this] { onGoBack(); }));
}

void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  exitActivity();
  enterNewActivity(new XtcReaderActivity(
      renderer, mappedInput, std::move(xtc), [this, xtcPath] { onGoToFileSelection(xtcPath); },
      [this] { onGoBack(); }));
}

void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  exitActivity();
  enterNewActivity(new TxtReaderActivity(
      renderer, mappedInput, std::move(txt), [this, txtPath] { onGoToFileSelection(txtPath); },
      [this] { onGoBack(); }));
}

void ReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (initialBookPath.empty()) {
    onGoToFileSelection();  // Start from root when entering via Browse
    return;
  }

  currentBookPath = initialBookPath;

  if (StringUtils::isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      onGoBack();
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (StringUtils::isTxtFile(initialBookPath)) {
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      onGoBack();
      return;
    }
    onGoToTxtReader(std::move(txt));
  } else {
    auto epub = loadEpub(initialBookPath);
    if (!epub) {
      onGoBack();
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}
