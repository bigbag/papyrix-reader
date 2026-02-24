#include "CalibreSyncState.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <errno.h>
#include <unistd.h>

#include "../config.h"
#include "../core/Core.h"
#include "../ui/Elements.h"
#include "ThemeManager.h"

#define TAG "CALIBRE"

namespace papyrix {

namespace {

inline int32_t saturateToInt32(uint64_t value) { return value > INT32_MAX ? INT32_MAX : static_cast<int32_t>(value); }
}  // namespace

CalibreSyncState::CalibreSyncState(GfxRenderer& renderer)
    : renderer_(renderer),
      needsRender_(true),
      goBack_(false),
      restartConn_(false),
      syncComplete_(false),
      conn_(nullptr),
      libraryInitialized_(false),
      booksReceived_(0) {}

CalibreSyncState::~CalibreSyncState() { cleanup(); }

void CalibreSyncState::enter(Core& core) {
  LOG_INF(TAG, "Entering");

  needsRender_ = true;
  goBack_ = false;
  restartConn_ = false;
  syncComplete_ = false;
  libraryInitialized_ = false;
  booksReceived_ = 0;

  // Clear pending sync mode now that we've entered
  core.pendingSync = SyncMode::None;

  // Initialize Calibre connection
  initializeCalibre(core);
}

void CalibreSyncState::initializeCalibre(Core& core) {
  calibreView_.setWaiting();

  // Initialize Calibre library
  calibre_err_t err = calibre_init();
  if (err != CALIBRE_OK) {
    LOG_ERR(TAG, "Failed to init library: %s", calibre_err_str(err));
    calibreView_.setError("Failed to initialize");
    needsRender_ = true;
    return;
  }
  libraryInitialized_ = true;

  // Configure device
  calibre_device_config_t config;
  calibre_device_config_init(&config);

  snprintf(config.device_name, sizeof(config.device_name), "Papyrix Reader");
  snprintf(config.manufacturer, sizeof(config.manufacturer), "Papyrix");
  snprintf(config.model, sizeof(config.model), "X4");

  // Add supported formats (Xteink: epub, txt, md, xtc, xtch)
  calibre_device_config_add_ext(&config, "epub");
  calibre_device_config_add_ext(&config, "txt");
  calibre_device_config_add_ext(&config, "md");
  calibre_device_config_add_ext(&config, "xtc");
  calibre_device_config_add_ext(&config, "xtch");

  // Safety: don't allow deletion from Calibre
  config.can_delete_books = 0;

  // Set up callbacks with this pointer as context
  calibre_callbacks_t callbacks = {
      .on_progress = onProgress, .on_book = onBook, .on_message = onMessage, .on_delete = onDelete, .user_ctx = this};

  // Create connection
  conn_ = calibre_conn_create(&config, &callbacks);
  if (!conn_) {
    LOG_ERR(TAG, "Failed to create connection");
    calibreView_.setError("Connection failed");
    needsRender_ = true;
    calibre_deinit();
    libraryInitialized_ = false;
    return;
  }

  // Set books directory
  calibre_set_books_dir(conn_, CALIBRE_BOOKS_DIR);

  // Get IP address to display with help text
  char ip[46];
  core.network.getIpAddress(ip, sizeof(ip));
  calibreView_.setWaitingWithIP(ip);

  // Start discovery (broadcast to find Calibre server)
  err = calibre_start_discovery(conn_, 0);  // Port parameter not used for client mode
  if (err != CALIBRE_OK) {
    LOG_ERR(TAG, "Failed to start discovery: %s", calibre_err_str(err));
    calibreView_.setError("Discovery failed");
    needsRender_ = true;
    cleanup();
    return;
  }

  LOG_INF(TAG, "Discovery started, IP: %s", ip);
}

void CalibreSyncState::exit(Core& core) {
  LOG_INF(TAG, "Exiting");

  cleanup();
  core.network.shutdown();
}

StateTransition CalibreSyncState::update(Core& core) {
  // Poll Calibre protocol if connection active
  if (conn_) {
    calibre_err_t err = calibre_process(conn_, CALIBRE_PROCESS_TIMEOUT_MS);

    if (err != CALIBRE_OK && err != CALIBRE_ERR_TIMEOUT) {
      LOG_ERR(TAG, "Process error: %s", calibre_err_str(err));

      if (err == CALIBRE_ERR_DISCONNECTED) {
        if (booksReceived_ > 0) {
          // Sync complete - Calibre disconnected after sending books
          syncComplete_ = true;
          calibreView_.setComplete(booksReceived_);
          needsRender_ = true;
        } else {
          // Show disconnected message with restart option
          calibreView_.setDisconnected();
          needsRender_ = true;
        }
      } else if (err != CALIBRE_ERR_BUSY) {
        calibreView_.setError(calibre_err_str(err));
        cleanup();  // Stop processing broken connection
        needsRender_ = true;
      }
    }

    // Update connecting status if we detect connection
    if (calibre_is_connected(conn_) && calibreView_.status == ui::CalibreView::Status::Waiting) {
      calibreView_.setConnecting();
      needsRender_ = true;
    }
  }

  // Process button events
  Event e;
  while (core.events.pop(e)) {
    if (e.type != EventType::ButtonPress) continue;
    handleInput(core, e.button);
  }

  if (goBack_) {
    goBack_ = false;
    // exit() will handle restart
    return StateTransition::to(StateId::Sync);
  }

  if (restartConn_) {
    restartConn_ = false;
    restartConnection(core);
  }

  return StateTransition::stay(StateId::CalibreSync);
}

void CalibreSyncState::render(Core& core) {
  if (!needsRender_ && !calibreView_.needsRender) return;

  ui::render(renderer_, THEME, calibreView_);
  calibreView_.needsRender = false;
  needsRender_ = false;
  core.display.markDirty();
}

void CalibreSyncState::handleInput(Core& /* core */, Button button) {
  switch (button) {
    case Button::Left:
    case Button::Back:
      if (calibreView_.buttons.isActive(0)) {
        if (calibreView_.status == ui::CalibreView::Status::Complete ||
            calibreView_.status == ui::CalibreView::Status::Error ||
            calibreView_.status == ui::CalibreView::Status::Waiting) {
          goBack_ = true;
        }
      }
      break;

    case Button::Center:
      if (calibreView_.buttons.isActive(1)) {
        if (calibreView_.status == ui::CalibreView::Status::Complete) {
          goBack_ = true;
        } else if (calibreView_.showRestartOption) {
          restartConn_ = true;
        }
      }
      break;

    default:
      break;
  }
}

void CalibreSyncState::cleanup() {
  if (conn_) {
    calibre_stop_discovery(conn_);
    calibre_disconnect(conn_);
    calibre_conn_destroy(conn_);
    conn_ = nullptr;
  }
  if (libraryInitialized_) {
    calibre_deinit();
    libraryInitialized_ = false;
  }
}

void CalibreSyncState::restartConnection(Core& core) {
  LOG_INF(TAG, "Restarting Calibre connection (WiFi kept active)");

  // Clean up only Calibre resources, keep WiFi active
  cleanup();

  // Reset state
  syncComplete_ = false;
  booksReceived_ = 0;

  // Re-initialize Calibre connection
  initializeCalibre(core);
}

// Static callbacks - bridge C library to C++ class

bool CalibreSyncState::onProgress(void* ctx, uint64_t current, uint64_t total) {
  auto* self = static_cast<CalibreSyncState*>(ctx);
  if (!self) return true;

  // Use current statusMsg if it contains a book title, otherwise show generic message
  const char* title = self->calibreView_.statusMsg;
  if (!title[0] || strncmp(title, "IP:", 3) == 0) {
    title = "Receiving...";
  }
  self->calibreView_.setReceiving(title, saturateToInt32(current), saturateToInt32(total));

  return true;  // Continue transfer
}

void CalibreSyncState::onBook(void* ctx, const calibre_book_meta_t* meta, const char* path) {
  auto* self = static_cast<CalibreSyncState*>(ctx);
  if (!self || !meta) return;

  self->booksReceived_++;
  LOG_INF(TAG, "Book received: \"%s\" -> %s", meta->title ? meta->title : "(null)", path ? path : "(null)");

  // Show "received N books" status instead of stuck progress bar
  snprintf(self->calibreView_.statusMsg, ui::CalibreView::MAX_STATUS_LEN, "Received %d book(s)", self->booksReceived_);
  self->calibreView_.status = ui::CalibreView::Status::Connecting;  // Use Connecting (no progress bar)
  self->calibreView_.needsRender = true;
  self->needsRender_ = true;
}

void CalibreSyncState::onMessage(const void* ctx, const char* message) {
  const auto* self = static_cast<const CalibreSyncState*>(ctx);
  if (!self || !message) return;

  LOG_DBG(TAG, "Calibre message: %s", message);
}

bool CalibreSyncState::onDelete(void* ctx, const char* lpath) {
  (void)ctx; /* Unused - delete doesn't need instance state */
  if (!lpath || lpath[0] == '\0') return false;

  /* Security: Reject path traversal and suspicious patterns BEFORE building path */
  if (strstr(lpath, "..") != nullptr) {
    LOG_ERR(TAG, "Rejected path with '..': %s", lpath);
    return false;
  }
  if (strchr(lpath, '~') != nullptr) {
    LOG_ERR(TAG, "Rejected path with '~': %s", lpath);
    return false;
  }
  if (lpath[0] == '/') {
    LOG_ERR(TAG, "Rejected absolute path: %s", lpath);
    return false;
  }
  /* Build full path */
  char full_path[256];
  int written = snprintf(full_path, sizeof(full_path), "%s/%s", CALIBRE_BOOKS_DIR, lpath);
  if (written < 0 || (size_t)written >= sizeof(full_path)) {
    LOG_ERR(TAG, "Path too long: %s", lpath);
    return false;
  }

  /* Delete the file */
  if (unlink(full_path) == 0) {
    LOG_INF(TAG, "Deleted book: %s", full_path);
    return true;
  } else {
    LOG_ERR(TAG, "Failed to delete book: %s (errno=%d)", full_path, errno);
    return false;
  }
}

}  // namespace papyrix
