/**
 * @file calibre_protocol.c
 * @brief Protocol message handlers for Calibre Wireless
 *
 * Implements handlers for all Calibre protocol messages:
 * - GET_INITIALIZATION_INFO
 * - SET_LIBRARY_INFO
 * - FREE_SPACE
 * - SEND_BOOK
 * - SEND_BOOKLISTS
 * - DISPLAY_MESSAGE
 * - NOOP
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "calibre_internal.h"
#include "calibre_storage.h"
#include "calibre_wireless.h"

/* Log tag for this module */
#define TAG CAL_LOG_TAG_PROTO

/* ============================================================================
 * Storage Helper Functions
 * ============================================================================ */

calibre_err_t calibre_get_storage_info(calibre_conn_t* conn, uint64_t* total_bytes, uint64_t* free_bytes) {
  if (!conn) {
    return CALIBRE_ERR_INVALID_ARG;
  }

  /* Return reasonable estimates for storage info.
   * Calibre uses this for display purposes only.
   * Getting actual SD card free space is platform-specific. */
  if (total_bytes) {
    *total_bytes = 16ULL * 1024 * 1024 * 1024; /* 16GB typical SD card */
  }
  if (free_bytes) {
    *free_bytes = 8ULL * 1024 * 1024 * 1024; /* 8GB free estimate */
  }
  return CALIBRE_OK;
}

/* ============================================================================
 * JSON Response Builders
 * ============================================================================ */

/**
 * @brief Build extensions list as JSON array
 */
static void build_extensions_json(const calibre_device_config_t* config, char* buf, size_t size) {
  int pos = 0;
  pos += snprintf(buf + pos, size - pos, "[");

  for (int i = 0; i < config->extension_count && pos < (int)size - 10; i++) {
    if (i > 0) {
      pos += snprintf(buf + pos, size - pos, ", ");
    }
    pos += snprintf(buf + pos, size - pos, "\"%s\"", config->extensions[i]);
  }

  snprintf(buf + pos, size - pos, "]");
}

/* ============================================================================
 * Protocol Handlers
 * ============================================================================ */

/**
 * @brief Handle GET_INITIALIZATION_INFO - Initial handshake
 *
 * Calibre sends this after TCP connection. We respond with device info.
 */
calibre_err_t calibre_handle_init_info(calibre_conn_t* conn, const char* json) {
  CAL_LOGI(TAG, "Handling GET_INITIALIZATION_INFO");

  json_parser_t p;
  json_parser_init(&p, json, strlen(json));

  /* Extract server protocol version */
  int64_t protocol_version = 0;
  json_find_int(&p, "serverProtocolVersion", &protocol_version);

  if (protocol_version > CALIBRE_PROTOCOL_VERSION) {
    CAL_LOGW(TAG, "Server protocol version %lld > client %d", (long long)protocol_version, CALIBRE_PROTOCOL_VERSION);
  }

  /* Build response */
  char ext_json[256];
  build_extensions_json(&conn->config, ext_json, sizeof(ext_json));

  char response[1024];
  int len = snprintf(response, sizeof(response),
                     "{"
                     "\"appName\": \"PapyriX Reader\","
                     "\"acceptedExtensions\": %s,"
                     "\"cacheUsesLpaths\": true,"
                     "\"canAcceptLibraryInfo\": true,"
                     "\"canDeleteMultipleBooks\": true,"
                     "\"canReceiveBookBinary\": true,"
                     "\"canSendOkToSendbook\": true,"
                     "\"canStreamBooks\": true,"
                     "\"canStreamMetadata\": true,"
                     "\"canUseCachedMetadata\": true,"
                     "\"ccVersionNumber\": 128,"
                     "\"coverHeight\": 240,"
                     "\"deviceKind\": \"PapyriX E-Ink Reader\","
                     "\"deviceName\": \"%s\","
                     "\"extensionPathLengths\": {},"
                     "\"maxBookContentPacketLen\": %d,"
                     "\"passwordHash\": \"%s\","
                     "\"useUuidFileNames\": false,"
                     "\"versionOK\": true,"
                     "\"device_store_uuid\": \"%s\""
                     "}",
                     ext_json, conn->config.device_name, CALIBRE_FILE_CHUNK_SIZE,
                     conn->password_hash[0] ? conn->password_hash : "", conn->config.device_store_uuid);

  if (len < 0 || len >= (int)sizeof(response)) {
    calibre_set_error(conn, CALIBRE_ERR_NOMEM, "Response too large");
    return CALIBRE_ERR_NOMEM;
  }

  calibre_err_t err = calibre_send_ok(conn, response);
  if (err == CALIBRE_OK) {
    conn->state = CALIBRE_STATE_CONNECTED;
    conn->connected = 1;
    CAL_LOGI(TAG, "Handshake complete, connected to Calibre");
  }

  return err;
}

/**
 * @brief Handle SET_LIBRARY_INFO - Library metadata from Calibre
 */
calibre_err_t calibre_handle_library_info(calibre_conn_t* conn, const char* json) {
  CAL_LOGD(TAG, "Handling SET_LIBRARY_INFO");

  size_t json_len = strlen(json);

  /* Extract library info using common helper */
  calibre_json_extract_string(json, json_len, "libraryName", conn->library_name, sizeof(conn->library_name));
  calibre_json_extract_string(json, json_len, "libraryUuid", conn->library_uuid, sizeof(conn->library_uuid));

  CAL_LOGI(TAG, "Library: %s (%s)", conn->library_name, conn->library_uuid);

  return calibre_send_ok(conn, NULL);
}

/**
 * @brief Handle FREE_SPACE - Report available storage
 */
calibre_err_t calibre_handle_free_space(calibre_conn_t* conn, const char* json) {
  (void)json;
  CAL_LOGI(TAG, "Handling FREE_SPACE");

  uint64_t total = 0, free_space = 0;
  calibre_err_t err = calibre_get_storage_info(conn, &total, &free_space);
  if (err != CALIBRE_OK) {
    CAL_LOGE(TAG, "Failed to get storage info: %d", err);
    /* Still send response with default values */
  }

  char response[128];
  int len = snprintf(response, sizeof(response), "{\"free_space_on_device\": %llu}", (unsigned long long)free_space);

  CAL_LOGI(TAG, "FREE_SPACE response (%d bytes): %s", len, response);

  if (len < 0 || len >= (int)sizeof(response)) {
    CAL_LOGE(TAG, "FREE_SPACE response buffer overflow!");
    return calibre_send_ok(conn, "{\"free_space_on_device\": 0}");
  }

  return calibre_send_ok(conn, response);
}

/**
 * @brief Handle SEND_BOOKLISTS - Calibre sends metadata updates
 *
 * Calibre sends this with wait_for_response=False - do NOT respond!
 * It contains metadata updates for books. We just log and ignore.
 */
calibre_err_t calibre_handle_booklists(calibre_conn_t* conn, const char* json) {
  (void)conn;
  (void)json;
  CAL_LOGD(TAG, "Handling SEND_BOOKLISTS (no response needed)");

  /* Calibre sends this with wait_for_response=False - do NOT respond! */
  return CALIBRE_OK;
}

/* ============================================================================
 * Book Transfer - Split into smaller functions
 * ============================================================================ */

/**
 * @brief Parse book metadata from SEND_BOOK JSON
 */
static calibre_err_t parse_book_metadata(const char* json, calibre_book_meta_t* meta) {
  memset(meta, 0, sizeof(*meta));
  size_t json_len = strlen(json);

  /* Extract string fields using common helper */
  calibre_json_extract_string(json, json_len, "lpath", meta->lpath, sizeof(meta->lpath));
  calibre_json_extract_string(json, json_len, "title", meta->title, sizeof(meta->title));
  calibre_json_extract_string(json, json_len, "authors", meta->authors, sizeof(meta->authors));
  calibre_json_extract_string(json, json_len, "uuid", meta->uuid, sizeof(meta->uuid));

  /* Extract numeric fields */
  int64_t size = 0;
  if (calibre_json_extract_int(json, json_len, "length", &size)) {
    meta->size = (uint64_t)size;
  }

  int64_t id = 0;
  if (calibre_json_extract_int(json, json_len, "calibre_id", &id)) {
    meta->calibre_id = (uint32_t)id;
  }

  /* Validate required field */
  if (meta->lpath[0] == '\0') {
    return CALIBRE_ERR_INVALID_ARG;
  }

  return CALIBRE_OK;
}

/**
 * @brief Receive book data in chunks and write to file
 */
static calibre_err_t receive_book_data(calibre_conn_t* conn, calibre_file_t file, const calibre_book_meta_t* meta) {
  uint64_t received = 0;
  calibre_err_t err = CALIBRE_OK;

  /* Allocate chunk buffer */
  uint8_t* chunk_buf = (uint8_t*)malloc(CALIBRE_FILE_CHUNK_SIZE);
  if (!chunk_buf) {
    return CALIBRE_ERR_NOMEM;
  }

  conn->state = CALIBRE_STATE_RECEIVING_BOOK;

  /* Set socket timeout for receiving file data */
  calibre_socket_set_timeout(conn->tcp_socket, 30000);

  while (received < meta->size) {
    if (conn->cancelled) {
      err = CALIBRE_ERR_CANCELLED;
      break;
    }

    /* Calculate chunk size */
    size_t to_receive = meta->size - received;
    if (to_receive > CALIBRE_FILE_CHUNK_SIZE) {
      to_receive = CALIBRE_FILE_CHUNK_SIZE;
    }

    /* Receive raw binary data */
    size_t buf_pos = 0;
    int eagainCount = 0;
    while (buf_pos < to_receive) {
      if (conn->cancelled) {
        err = CALIBRE_ERR_CANCELLED;
        break;
      }
      ssize_t n = recv(conn->tcp_socket, chunk_buf + buf_pos, to_receive - buf_pos, 0);
      if (n < 0) {
        if (errno == EINTR) {
          if (conn->cancelled) {
            err = CALIBRE_ERR_CANCELLED;
            break;
          }
          continue;
        }
        if (errno == EAGAIN) {
          if (conn->cancelled) {
            err = CALIBRE_ERR_CANCELLED;
            break;
          }
          if (++eagainCount >= 3) {
            CAL_LOGE(TAG, "Book transfer stalled: no data");
            err = CALIBRE_ERR_SOCKET;
            break;
          }
          continue;
        }
        err = CALIBRE_ERR_SOCKET;
        CAL_LOGE(TAG, "Socket error receiving book data: %s", strerror(errno));
        break;
      }
      if (n == 0) {
        err = CALIBRE_ERR_DISCONNECTED;
        CAL_LOGE(TAG, "Connection closed while receiving book");
        break;
      }
      buf_pos += (size_t)n;
      eagainCount = 0;
    }

    if (err != CALIBRE_OK) break;

    /* Write to file */
    int written = calibre_storage_write(file, chunk_buf, to_receive);
    if (written != (int)to_receive) {
      CAL_LOGE(TAG, "Write failed");
      err = CALIBRE_ERR_WRITE_FILE;
      break;
    }

    received += to_receive;

    /* Progress callback */
    if (conn->callbacks.on_progress) {
      if (!conn->callbacks.on_progress(conn->callbacks.user_ctx, received, meta->size)) {
        err = CALIBRE_ERR_CANCELLED;
        break;
      }
    }

    CAL_LOGD(TAG, "Progress: %llu / %llu bytes", (unsigned long long)received, (unsigned long long)meta->size);
  }

  /* Restore socket timeout */
  calibre_socket_set_timeout(conn->tcp_socket, CALIBRE_RECV_TIMEOUT_MS);

  free(chunk_buf);
  conn->state = CALIBRE_STATE_CONNECTED;

  return (err == CALIBRE_OK && received == meta->size) ? CALIBRE_OK : err;
}

/**
 * @brief Handle SEND_BOOK - Receive book file from Calibre
 *
 * This is the main book transfer handler. Uses streaming to minimize
 * memory usage - file is written directly to SD card in chunks.
 */
calibre_err_t calibre_handle_send_book(calibre_conn_t* conn, const char* json) {
  CAL_LOGI(TAG, "Handling SEND_BOOK");

  /* Parse metadata */
  calibre_book_meta_t meta;
  calibre_err_t err = parse_book_metadata(json, &meta);
  if (err != CALIBRE_OK) {
    CAL_LOGE(TAG, "Failed to parse book metadata (missing lpath)");
    return calibre_send_error(conn, "Missing or invalid file path");
  }

  CAL_LOGI(TAG, "Receiving book: %s (%s) - %llu bytes", meta.title, meta.lpath, (unsigned long long)meta.size);

  /* Validate lpath (security check) */
  if (!calibre_validate_lpath(meta.lpath, &conn->config)) {
    CAL_LOGE(TAG, "Invalid or unsafe lpath: %s", meta.lpath);
    return calibre_send_error(conn, "Invalid file path");
  }

  /* Validate book size */
  if (!calibre_validate_size(meta.size)) {
    if (meta.size == 0) {
      CAL_LOGE(TAG, "Invalid book size: 0 bytes");
      return calibre_send_error(conn, "Book has zero size");
    }
    CAL_LOGE(TAG, "Invalid book size: %llu bytes (max %llu)", (unsigned long long)meta.size,
             (unsigned long long)CALIBRE_MAX_BOOK_SIZE);
    return calibre_send_error(conn, "Book too large");
  }

  /* Build full path */
  char full_path[CALIBRE_MAX_PATH_LEN];
  int path_len = snprintf(full_path, sizeof(full_path), "%s/%s", conn->books_dir, meta.lpath);
  if (path_len < 0 || (size_t)path_len >= sizeof(full_path)) {
    CAL_LOGE(TAG, "Path too long: %s/%s", conn->books_dir, meta.lpath);
    return calibre_send_error(conn, "Path too long");
  }

  /* Create parent directories */
  if (calibre_storage_mkdir_p(full_path) != 0) {
    CAL_LOGE(TAG, "Failed to create directory for %s", full_path);
    return calibre_send_error(conn, "Failed to create directory");
  }

  /* Open file for writing */
  calibre_file_t file = calibre_storage_open_write(full_path);
  if (!file) {
    CAL_LOGE(TAG, "Failed to open file %s", full_path);
    return calibre_send_error(conn, "Failed to open file");
  }

  /* Confirm we're ready to receive */
  err = calibre_send_ok(conn, "{\"willAccept\": true}");
  if (err != CALIBRE_OK) {
    calibre_storage_close(file);
    return err;
  }

  /* Receive the book data */
  err = receive_book_data(conn, file, &meta);
  calibre_storage_close(file);

  if (err == CALIBRE_OK) {
    CAL_LOGI(TAG, "Book received successfully: %s", meta.title);

    /* NOTE: Do NOT send BOOK_DONE here!
     * Calibre's _put_file() doesn't read any response after sending binary data.
     * If we send BOOK_DONE, it sits in the TCP buffer and causes protocol desync:
     * - BOOK_DONE gets read as a NOOP response
     * - The actual NOOP response becomes stale
     * - Stale NOOP response is read as FREE_SPACE response
     * - KeyError: 'free_space_on_device' not in {}
     * KOReader also doesn't send BOOK_DONE - see wireless.lua sendBook() */

    /* Notify callback */
    if (conn->callbacks.on_book) {
      conn->callbacks.on_book(conn->callbacks.user_ctx, &meta, full_path);
    }
  } else {
    /* Clean up partial file on error */
    calibre_storage_unlink(full_path);
    CAL_LOGE(TAG, "Book transfer failed: %s", calibre_err_str(err));
  }

  return err;
}

/**
 * @brief Handle DISPLAY_MESSAGE - Show message from Calibre
 */
calibre_err_t calibre_handle_message(calibre_conn_t* conn, const char* json) {
  char msg_buf[256];
  if (calibre_json_extract_string(json, strlen(json), "message", msg_buf, sizeof(msg_buf))) {
    CAL_LOGI(TAG, "Calibre message: %s", msg_buf);

    /* Notify callback */
    if (conn->callbacks.on_message) {
      conn->callbacks.on_message(conn->callbacks.user_ctx, msg_buf);
    }
  }

  return calibre_send_ok(conn, NULL);
}

/**
 * @brief Check if path has a valid configured extension
 * @param path File path to check
 * @param config Device config with accepted extensions
 * @return true if extension is valid
 */
static bool has_valid_book_extension(const char* path, const calibre_device_config_t* config) {
  if (!path) return false;
  const char* ext = strrchr(path, '.');
  if (!ext) return false;

  /* Skip the dot */
  const char* ext_no_dot = ext + 1;

  /* Check against configured extensions */
  for (int i = 0; i < config->extension_count; i++) {
    if (strcasecmp(ext_no_dot, config->extensions[i]) == 0) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Handle DELETE_BOOK - Delete books from device
 *
 * Calibre sends: {"lpaths": ["path/to/book1.epub", "path/to/book2.epub"]}
 * We respond with OK after processing deletions
 */
calibre_err_t calibre_handle_delete_book(calibre_conn_t* conn, const char* json) {
  CAL_LOGI(TAG, "Handling DELETE_BOOK");

  int deleted_count = 0;

  /* Find "lpaths" array in JSON */
  const char* lpaths_key = strstr(json, "\"lpaths\"");
  if (!lpaths_key) {
    CAL_LOGW(TAG, "No lpaths array in DELETE_BOOK");
    return calibre_send_ok(conn, "{\"count\": 0}");
  }

  /* Find the opening bracket of the array */
  const char* array_start = strchr(lpaths_key, '[');
  if (!array_start) {
    CAL_LOGW(TAG, "Malformed lpaths array");
    return calibre_send_ok(conn, "{\"count\": 0}");
  }

  /* Find the closing bracket */
  const char* array_end = strchr(array_start, ']');
  if (!array_end) {
    CAL_LOGW(TAG, "Unclosed lpaths array");
    return calibre_send_ok(conn, "{\"count\": 0}");
  }

  /* Parse paths within the array bounds only */
  const char* ptr = array_start + 1;
  while (ptr < array_end) {
    /* Find opening quote */
    const char* quote_start = strchr(ptr, '"');
    if (!quote_start || quote_start >= array_end) break;
    quote_start++; /* Skip opening quote */

    /* Find closing quote (handle escaped quotes) */
    const char* quote_end = quote_start;
    while (quote_end < array_end) {
      if (*quote_end == '\\' && quote_end + 1 < array_end) {
        quote_end += 2; /* Skip escaped char */
      } else if (*quote_end == '"') {
        break;
      } else {
        quote_end++;
      }
    }
    if (quote_end >= array_end) break;

    size_t len = quote_end - quote_start;
    if (len > 0 && len < CALIBRE_MAX_PATH_LEN) {
      char path[CALIBRE_MAX_PATH_LEN];

      /* Decode JSON escape sequences (mainly \" and \\) */
      const char* src = quote_start;
      const char* src_end = quote_end;
      char* dst = path;
      char* dst_end = path + sizeof(path) - 1;

      while (src < src_end && dst < dst_end) {
        if (*src == '\\' && src + 1 < src_end) {
          src++; /* Skip backslash */
          switch (*src) {
            case '"':
            case '\\':
            case '/':
              *dst++ = *src++;
              break;
            default:
              /* Unknown escape, copy as-is */
              *dst++ = *src++;
              break;
          }
        } else {
          *dst++ = *src++;
        }
      }
      *dst = '\0';

      /* Validate path has valid book extension */
      if (has_valid_book_extension(path, &conn->config)) {
        CAL_LOGI(TAG, "Delete requested for: %s", path);

        /* Call the delete callback if registered */
        if (conn->callbacks.on_delete) {
          if (conn->callbacks.on_delete(conn->callbacks.user_ctx, path)) {
            deleted_count++;
          }
        }
      }
    }

    ptr = quote_end + 1;
  }

  CAL_LOGI(TAG, "Deleted %d books", deleted_count);

  /* Send acknowledgment */
  char response[128];
  snprintf(response, sizeof(response), "{\"count\": %d}", deleted_count);
  return calibre_send_ok(conn, response);
}

/**
 * @brief Handle NOOP - Keep-alive ping
 *
 * Calibre sends two types of NOOPs:
 * 1. Empty payload {} - expects response (keepalive from detect_managed_devices)
 * 2. Non-empty payload {"count": N} or {"priKey": X} - NO response expected (from books())
 *
 * If we respond to type 2, the response sits in the TCP buffer and causes
 * the next _call_client to read the wrong response (KeyError: 'free_space_on_device')
 */
calibre_err_t calibre_handle_noop(calibre_conn_t* conn, const char* json) {
  /* Check if this NOOP has a non-empty payload (means no response expected) */
  /* Empty payload looks like: {} or { } */
  /* Non-empty payload looks like: {"count": 0} or {"priKey": 123} */
  bool has_payload = false;
  if (json) {
    const char* p = json;
    while (*p == ' ' || *p == '{') p++;
    if (*p != '}' && *p != '\0') {
      has_payload = true;
    }
  }

  if (has_payload) {
    CAL_LOGD(TAG, "NOOP with payload - no response");
    return CALIBRE_OK; /* Don't respond! */
  }

  CAL_LOGD(TAG, "NOOP - responding");
  return calibre_send_ok(conn, NULL);
}

/**
 * @brief Handle GET_DEVICE_INFORMATION - Device metadata request
 *
 * Calibre requests device info for identification/display purposes.
 */
calibre_err_t calibre_handle_device_info(calibre_conn_t* conn, const char* json) {
  (void)json;
  CAL_LOGD(TAG, "Handling GET_DEVICE_INFORMATION");

  char response[384];
  snprintf(response, sizeof(response),
           "{"
           "\"device_info\": {"
           "\"device_store_uuid\": \"%s\","
           "\"device_name\": \"%s\""
           "},"
           "\"device_version\": \"PapyriX 1.0\","
           "\"version\": \"1.0\""
           "}",
           conn->config.device_store_uuid, conn->config.device_name);

  return calibre_send_ok(conn, response);
}

/**
 * @brief Handle TOTAL_SPACE - Report total storage
 */
calibre_err_t calibre_handle_total_space(calibre_conn_t* conn, const char* json) {
  (void)json;
  CAL_LOGD(TAG, "Handling TOTAL_SPACE");

  uint64_t total, free_space;
  calibre_get_storage_info(conn, &total, &free_space);

  char response[64];
  snprintf(response, sizeof(response), "{\"total_space_on_device\": %llu}", (unsigned long long)total);

  return calibre_send_ok(conn, response);
}

/**
 * @brief Handle GET_BOOK_COUNT - Report number of books on device
 *
 * For simplicity, we report 0 books. Calibre will then send all books.
 */
calibre_err_t calibre_handle_book_count(calibre_conn_t* conn, const char* json) {
  (void)json;
  CAL_LOGD(TAG, "Handling GET_BOOK_COUNT");

  return calibre_send_ok(conn, "{\"count\": 0, \"willStream\": true, \"willScan\": true}");
}

/**
 * @brief Handle SEND_BOOK_METADATA - Metadata update from Calibre
 *
 * Calibre sends metadata updates for books. Just acknowledge.
 */
calibre_err_t calibre_handle_book_metadata(calibre_conn_t* conn, const char* json) {
  CAL_LOGD(TAG, "Handling SEND_BOOK_METADATA");

  char title_buf[128];
  if (calibre_json_extract_string(json, strlen(json), "title", title_buf, sizeof(title_buf))) {
    CAL_LOGD(TAG, "Metadata for: %s", title_buf);
  }

  return calibre_send_ok(conn, NULL);
}
