/**
 * @file calibre_wireless.h
 * @brief Calibre Wireless Device Protocol Library for ESP32
 *
 * Memory-efficient implementation of Calibre's Smart Device App protocol
 * for syncing ebooks with Calibre desktop application.
 *
 * Features:
 * - UDP broadcast discovery
 * - TCP connection with length-prefixed JSON protocol
 * - Streaming file reception with minimal RAM usage
 * - Book metadata handling
 *
 * @author PapyriX Project
 * @license MIT
 */

#ifndef CALIBRE_WIRELESS_H
#define CALIBRE_WIRELESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

/** Protocol version (must match Calibre's PROTOCOL_VERSION) */
#define CALIBRE_PROTOCOL_VERSION 1

/** Maximum device name length */
#define CALIBRE_MAX_DEVICE_NAME 64

/** Maximum path length for files */
#define CALIBRE_MAX_PATH_LEN 256

/** Maximum metadata field length */
#define CALIBRE_MAX_FIELD_LEN 128

/** JSON receive buffer size (keep small for memory efficiency) */
#define CALIBRE_JSON_BUF_SIZE 2048

/** File receive chunk size (balance between speed and memory) */
#define CALIBRE_FILE_CHUNK_SIZE 4096

/** Default TCP port for Calibre wireless connection */
#define CALIBRE_DEFAULT_PORT 9090

/** UDP broadcast discovery ports (Calibre probes these) */
#define CALIBRE_BROADCAST_PORTS {54982, 48123, 39001, 44044, 59678}
#define CALIBRE_BROADCAST_PORT_COUNT 5

/** Maximum discovery broadcast attempts (500ms interval = 10 seconds total) */
#define CALIBRE_MAX_DISCOVERY_BROADCASTS 20

/** Connection timeout in milliseconds */
#define CALIBRE_CONNECT_TIMEOUT_MS 10000

/** Receive timeout in milliseconds */
#define CALIBRE_RECV_TIMEOUT_MS 30000

/** Maximum extensions list size */
#define CALIBRE_MAX_EXTENSIONS 16
#define CALIBRE_MAX_EXT_LEN 8

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum {
  CALIBRE_OK = 0,
  CALIBRE_ERR_NOMEM = -1,
  CALIBRE_ERR_INVALID_ARG = -2,
  CALIBRE_ERR_SOCKET = -3,
  CALIBRE_ERR_CONNECT = -4,
  CALIBRE_ERR_TIMEOUT = -5,
  CALIBRE_ERR_PROTOCOL = -6,
  CALIBRE_ERR_JSON_PARSE = -7,
  CALIBRE_ERR_AUTH = -8,
  CALIBRE_ERR_WRITE_FILE = -9,
  CALIBRE_ERR_SD_CARD = -10,
  CALIBRE_ERR_DISCONNECTED = -11,
  CALIBRE_ERR_CANCELLED = -12,
  CALIBRE_ERR_BUSY = -13,
} calibre_err_t;

/* ============================================================================
 * Protocol Opcodes - Must match Calibre's smart_device_app/driver.py
 * ============================================================================ */

typedef enum {
  CALIBRE_OP_OK = 0,
  CALIBRE_OP_SET_CALIBRE_DEVICE_INFO = 1,
  CALIBRE_OP_SET_CALIBRE_DEVICE_NAME = 2,
  CALIBRE_OP_GET_DEVICE_INFORMATION = 3,
  CALIBRE_OP_TOTAL_SPACE = 4,
  CALIBRE_OP_FREE_SPACE = 5,
  CALIBRE_OP_GET_BOOK_COUNT = 6,
  CALIBRE_OP_SEND_BOOKLISTS = 7,
  CALIBRE_OP_SEND_BOOK = 8,
  CALIBRE_OP_GET_INITIALIZATION_INFO = 9,
  CALIBRE_OP_BOOK_DONE = 11,
  CALIBRE_OP_NOOP = 12,
  CALIBRE_OP_DELETE_BOOK = 13,
  CALIBRE_OP_GET_BOOK_FILE_SEGMENT = 14,
  CALIBRE_OP_GET_BOOK_METADATA = 15,
  CALIBRE_OP_SEND_BOOK_METADATA = 16,
  CALIBRE_OP_DISPLAY_MESSAGE = 17,
  CALIBRE_OP_CALIBRE_BUSY = 18,
  CALIBRE_OP_SET_LIBRARY_INFO = 19,
  CALIBRE_OP_ERROR = 20,
} calibre_opcode_t;

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/**
 * @brief Device configuration for Calibre connection
 */
typedef struct {
  char device_name[CALIBRE_MAX_DEVICE_NAME];                    /**< Device display name */
  char device_store_uuid[37];                                   /**< Unique device storage UUID */
  char manufacturer[32];                                        /**< Device manufacturer */
  char model[32];                                               /**< Device model */
  uint32_t cache_uses_lpath : 1;                                /**< Cache uses logical path */
  uint32_t can_use_send : 1;                                    /**< Device supports receive */
  uint32_t can_delete_books : 1;                                /**< Device supports deletion */
  uint32_t can_receive : 1;                                     /**< Device can receive books */
  char extensions[CALIBRE_MAX_EXTENSIONS][CALIBRE_MAX_EXT_LEN]; /**< Supported formats */
  uint8_t extension_count;                                      /**< Number of supported formats */
} calibre_device_config_t;

/**
 * @brief Book metadata from Calibre
 */
typedef struct {
  uint32_t calibre_id;                 /**< Calibre database ID */
  char uuid[37];                       /**< Book UUID */
  char lpath[CALIBRE_MAX_PATH_LEN];    /**< Logical path on device */
  char title[CALIBRE_MAX_FIELD_LEN];   /**< Book title */
  char authors[CALIBRE_MAX_FIELD_LEN]; /**< Authors (comma-separated) */
  uint64_t size;                       /**< File size in bytes */
  uint32_t last_modified;              /**< Unix timestamp */
} calibre_book_meta_t;

/**
 * @brief Connection state structure
 */
typedef struct calibre_conn calibre_conn_t;

/**
 * @brief Progress callback type
 * @param ctx User context pointer
 * @param current Current bytes transferred
 * @param total Total bytes to transfer
 * @return true to continue, false to cancel
 */
typedef bool (*calibre_progress_cb_t)(void* ctx, uint64_t current, uint64_t total);

/**
 * @brief File write callback type
 * @param ctx User context pointer
 * @param data Data chunk to write
 * @param len Length of data chunk
 * @param offset Offset in file
 * @return Number of bytes written, or negative on error
 */
typedef int (*calibre_write_cb_t)(void* ctx, const uint8_t* data, size_t len, uint64_t offset);

/**
 * @brief Book received callback type
 * @param ctx User context pointer
 * @param meta Book metadata
 * @param lpath Final path where book was saved
 */
typedef void (*calibre_book_received_cb_t)(void* ctx, const calibre_book_meta_t* meta, const char* lpath);

/**
 * @brief Message callback type
 * @param ctx User context pointer
 * @param message Message text from Calibre
 */
typedef void (*calibre_message_cb_t)(const void* ctx, const char* message);

/**
 * @brief Book deleted callback type
 * @param ctx User context pointer
 * @param lpath Logical path of the deleted book
 * @return true if book was deleted successfully, false otherwise
 */
typedef bool (*calibre_book_deleted_cb_t)(void* ctx, const char* lpath);

/**
 * @brief Callbacks structure
 */
typedef struct {
  calibre_progress_cb_t on_progress;   /**< Transfer progress callback */
  calibre_book_received_cb_t on_book;  /**< Book received callback */
  calibre_message_cb_t on_message;     /**< Message callback */
  calibre_book_deleted_cb_t on_delete; /**< Book deleted callback */
  void* user_ctx;                      /**< User context for callbacks */
} calibre_callbacks_t;

/* ============================================================================
 * Public API Functions
 * ============================================================================ */

/**
 * @brief Initialize the Calibre wireless library
 *
 * Must be called before any other functions. Initializes internal
 * state and prepares for connections.
 *
 * @return CALIBRE_OK on success, error code otherwise
 */
calibre_err_t calibre_init(void);

/**
 * @brief Deinitialize the library and free resources
 */
void calibre_deinit(void);

/**
 * @brief Create a new connection context
 *
 * @param config Device configuration
 * @param callbacks Optional callbacks structure
 * @return Connection handle, or NULL on error
 */
calibre_conn_t* calibre_conn_create(const calibre_device_config_t* config, const calibre_callbacks_t* callbacks);

/**
 * @brief Destroy a connection context
 *
 * @param conn Connection handle
 */
void calibre_conn_destroy(calibre_conn_t* conn);

/**
 * @brief Start UDP broadcast listener for discovery
 *
 * Starts listening on Calibre discovery ports. When Calibre sends
 * a broadcast, we respond with our port number.
 *
 * @param conn Connection handle
 * @param port TCP port to advertise (0 for default)
 * @return CALIBRE_OK on success, error code otherwise
 */
calibre_err_t calibre_start_discovery(calibre_conn_t* conn, uint16_t port);

/**
 * @brief Stop UDP broadcast listener
 *
 * @param conn Connection handle
 */
void calibre_stop_discovery(calibre_conn_t* conn);

/**
 * @brief Connect to Calibre server directly (without discovery)
 *
 * @param conn Connection handle
 * @param host Calibre server IP address
 * @param port Calibre server port
 * @return CALIBRE_OK on success, error code otherwise
 */
calibre_err_t calibre_connect(calibre_conn_t* conn, const char* host, uint16_t port);

/**
 * @brief Connect to discovered Calibre server
 *
 * After discovery has found Calibre, this connects to it.
 * Call this after calibre_start_discovery() has detected Calibre.
 *
 * @param conn Connection handle
 * @return CALIBRE_OK on success, error code otherwise
 */
calibre_err_t calibre_connect_to_discovered(calibre_conn_t* conn);

/**
 * @brief Disconnect from Calibre server
 *
 * @param conn Connection handle
 */
void calibre_disconnect(calibre_conn_t* conn);

/**
 * @brief Check if connected to Calibre
 *
 * @param conn Connection handle
 * @return true if connected
 */
bool calibre_is_connected(const calibre_conn_t* conn);

/**
 * @brief Process incoming data (call in main loop)
 *
 * Call regularly to handle discovery and protocol messages.
 * timeout_ms controls the initial TCP readiness wait, not the total call.
 * After readiness, message reception may wait. A zero timeout uses
 * CALIBRE_RECV_TIMEOUT_MS (30000 ms) for reception.
 * Discovery, connection, and handlers may also do work during the call.
 *
 * @param conn Connection handle
 * @param timeout_ms Initial TCP readiness wait in milliseconds (0 to poll)
 * @return CALIBRE_OK on success, error code otherwise
 */
calibre_err_t calibre_process(calibre_conn_t* conn, uint32_t timeout_ms);

/**
 * @brief Set the books directory path
 *
 * @param conn Connection handle
 * @param path Path to books directory on SD card
 * @return CALIBRE_OK on success, error code otherwise
 */
calibre_err_t calibre_set_books_dir(calibre_conn_t* conn, const char* path);

/**
 * @brief Get storage space information
 *
 * @param conn Connection handle
 * @param total_bytes Pointer to receive total storage (can be NULL)
 * @param free_bytes Pointer to receive free storage (can be NULL)
 * @return CALIBRE_OK on success, error code otherwise
 */
calibre_err_t calibre_get_storage_info(calibre_conn_t* conn, uint64_t* total_bytes, uint64_t* free_bytes);

/**
 * @brief Set authentication password
 *
 * @param conn Connection handle
 * @param password Password string (NULL to disable)
 * @return CALIBRE_OK on success, error code otherwise
 */
calibre_err_t calibre_set_password(calibre_conn_t* conn, const char* password);

/**
 * @brief Get last error message
 *
 * @param conn Connection handle
 * @return Error message string, or NULL if no error
 */
const char* calibre_get_error_msg(const calibre_conn_t* conn);

/**
 * @brief Get error code description
 *
 * @param err Error code
 * @return Static string describing the error
 */
const char* calibre_err_str(calibre_err_t err);

/**
 * @brief Initialize device config with defaults
 *
 * @param config Config structure to initialize
 */
void calibre_device_config_init(calibre_device_config_t* config);

/**
 * @brief Add supported extension to device config
 *
 * @param config Config structure
 * @param ext Extension string (e.g., "epub", "pdf")
 * @return CALIBRE_OK on success, error code otherwise
 */
calibre_err_t calibre_device_config_add_ext(calibre_device_config_t* config, const char* ext);

#ifdef __cplusplus
}
#endif

#endif /* CALIBRE_WIRELESS_H */
