#pragma once
#include <WiFiClient.h>

#include <cstdint>
#include <string>

/**
 * Error codes for Calibre protocol operations.
 */
enum class CalibreError {
  OK = 0,
  NETWORK_ERROR,
  TIMEOUT,
  PARSE_ERROR,
  PROTOCOL_ERROR,
  AUTH_FAILED,
  DISK_ERROR
};

/**
 * Get human-readable error message.
 */
const char* calibreErrorString(CalibreError error);

/**
 * Calibre Smart Device protocol implementation.
 *
 * Protocol uses length-prefixed JSON messages:
 * - Length is sent as ASCII decimal string
 * - Message format: [opcode, {data}]
 *
 * See: https://github.com/kovidgoyal/calibre/blob/master/src/calibre/devices/smart_device_app/driver.py
 */
namespace CalibreProtocol {

// Protocol version
static constexpr int PROTOCOL_VERSION = 1;

// Opcodes
static constexpr uint8_t OP_OK = 0;
static constexpr uint8_t OP_SET_CALIBRE_DEVICE_INFO = 1;
static constexpr uint8_t OP_SET_CALIBRE_DEVICE_NAME = 2;
static constexpr uint8_t OP_GET_DEVICE_INFORMATION = 3;
static constexpr uint8_t OP_TOTAL_SPACE = 4;
static constexpr uint8_t OP_FREE_SPACE = 5;
static constexpr uint8_t OP_GET_BOOK_COUNT = 6;
static constexpr uint8_t OP_SEND_BOOKLISTS = 7;
static constexpr uint8_t OP_SEND_BOOK = 8;
static constexpr uint8_t OP_GET_INIT_INFO = 9;
static constexpr uint8_t OP_BOOK_DONE = 11;
static constexpr uint8_t OP_NOOP = 12;
static constexpr uint8_t OP_DELETE_BOOK = 13;
static constexpr uint8_t OP_GET_BOOK_FILE_SEGMENT = 14;
static constexpr uint8_t OP_GET_BOOK_METADATA = 15;
static constexpr uint8_t OP_SEND_BOOK_METADATA = 16;
static constexpr uint8_t OP_DISPLAY_MESSAGE = 17;
static constexpr uint8_t OP_CALIBRE_BUSY = 18;
static constexpr uint8_t OP_SET_LIBRARY_INFO = 19;
static constexpr uint8_t OP_ERROR = 20;

// Max packet sizes
static constexpr size_t MAX_BOOK_PACKET_LEN = 4096;
static constexpr size_t MAX_MESSAGE_LEN = 65536;

// UDP discovery ports
static constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
static constexpr size_t UDP_PORT_COUNT = 5;

// Default TCP port
static constexpr uint16_t DEFAULT_TCP_PORT = 9090;

/**
 * Parse a message from the client.
 * Messages are length-prefixed JSON: "123[0, {...}]"
 *
 * @param client WiFi client to read from
 * @param opcode Output: parsed opcode
 * @param jsonData Output: the data portion of the message (without opcode)
 * @param timeoutMs Read timeout in milliseconds
 * @return true if message was parsed successfully
 */
bool parseMessage(WiFiClient& client, uint8_t& opcode, std::string& jsonData, unsigned long timeoutMs = 5000);

/**
 * Send a message to the client.
 *
 * @param client WiFi client to write to
 * @param opcode Message opcode
 * @param json JSON data (will be wrapped as [opcode, json])
 * @return true if message was sent successfully
 */
bool sendMessage(WiFiClient& client, uint8_t opcode, const char* json);

/**
 * Send raw bytes to the client (for binary book data).
 *
 * @param client WiFi client to write to
 * @param data Data buffer
 * @param len Data length
 * @return true if data was sent successfully
 */
bool sendRawBytes(WiFiClient& client, const uint8_t* data, size_t len);

/**
 * Compute SHA1 password hash for authentication.
 * Hash = SHA1(password + challenge)
 *
 * @param password User's password
 * @param challenge Challenge string from Calibre (ISO 8601 timestamp)
 * @return Hex-encoded SHA1 hash
 */
std::string computePasswordHash(const std::string& password, const std::string& challenge);

/**
 * Extract a string value from JSON.
 * Simple parser - handles "key": "value" patterns.
 *
 * @param json JSON string to search
 * @param key Key name to find
 * @return Value string, or empty if not found
 */
std::string extractJsonString(const std::string& json, const char* key);

/**
 * Extract an integer value from JSON.
 * Simple parser - handles "key": 123 patterns.
 *
 * @param json JSON string to search
 * @param key Key name to find
 * @return Integer value, or 0 if not found
 */
int64_t extractJsonInt(const std::string& json, const char* key);

/**
 * Extract a boolean value from JSON.
 *
 * @param json JSON string to search
 * @param key Key name to find
 * @param defaultValue Value to return if key not found
 * @return Boolean value
 */
bool extractJsonBool(const std::string& json, const char* key, bool defaultValue = false);

/**
 * Escape a string for JSON output.
 *
 * @param str String to escape
 * @return Escaped string (without surrounding quotes)
 */
std::string escapeJsonString(const std::string& str);

}  // namespace CalibreProtocol
