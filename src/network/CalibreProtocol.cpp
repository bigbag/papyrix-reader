#include "CalibreProtocol.h"

#include <Arduino.h>
#include <mbedtls/sha1.h>

const char* calibreErrorString(CalibreError error) {
  switch (error) {
    case CalibreError::OK:
      return "Success";
    case CalibreError::NETWORK_ERROR:
      return "Network error";
    case CalibreError::TIMEOUT:
      return "Connection timeout";
    case CalibreError::PARSE_ERROR:
      return "Parse error";
    case CalibreError::PROTOCOL_ERROR:
      return "Protocol error";
    case CalibreError::AUTH_FAILED:
      return "Authentication failed";
    case CalibreError::DISK_ERROR:
      return "Disk error";
    default:
      return "Unknown error";
  }
}

namespace CalibreProtocol {

bool parseMessage(WiFiClient& client, uint8_t& opcode, std::string& jsonData, unsigned long timeoutMs) {
  unsigned long startTime = millis();

  // Read length prefix (ASCII digits until '[')
  std::string lengthStr;
  while (millis() - startTime < timeoutMs) {
    if (client.available()) {
      char c = client.read();
      if (c == '[') {
        break;  // Found start of JSON array
      }
      if (c >= '0' && c <= '9') {
        lengthStr += c;
      } else if (c == '\n' || c == '\r' || c == ' ') {
        // Skip whitespace
        continue;
      } else {
        Serial.printf("[CAL] Unexpected char in length: 0x%02X\n", c);
        return false;
      }
    } else {
      delay(1);
    }
  }

  if (lengthStr.empty()) {
    return false;  // No data or timeout
  }

  size_t messageLen;
  try {
    messageLen = std::stoul(lengthStr);
  } catch (const std::exception&) {
    Serial.printf("[CAL] Failed to parse message length: %s\n", lengthStr.c_str());
    return false;
  }
  if (messageLen == 0 || messageLen > MAX_MESSAGE_LEN) {
    Serial.printf("[CAL] Invalid message length: %zu\n", messageLen);
    return false;
  }

  // We already consumed the '[', so read the rest (messageLen - 1 bytes + the '[' we ate)
  // Actually the length includes the entire "[opcode, {...}]" so we need messageLen - 1 more bytes
  std::string message = "[";
  size_t remaining = messageLen - 1;

  while (remaining > 0 && millis() - startTime < timeoutMs) {
    if (client.available()) {
      char c = client.read();
      message += c;
      remaining--;
    } else {
      delay(1);
    }
  }

  if (remaining > 0) {
    Serial.printf("[CAL] Timeout reading message, %zu bytes remaining\n", remaining);
    return false;
  }

  // Parse "[opcode, {...}]"
  // Find the opcode (first number after '[')
  size_t pos = 1;  // Skip '['
  while (pos < message.size() && (message[pos] == ' ' || message[pos] == '\t')) {
    pos++;
  }

  std::string opcodeStr;
  while (pos < message.size() && message[pos] >= '0' && message[pos] <= '9') {
    opcodeStr += message[pos++];
  }

  if (opcodeStr.empty()) {
    Serial.println("[CAL] Failed to parse opcode");
    return false;
  }

  try {
    opcode = static_cast<uint8_t>(std::stoul(opcodeStr));
  } catch (const std::exception&) {
    Serial.printf("[CAL] Failed to parse opcode: %s\n", opcodeStr.c_str());
    return false;
  }

  // Skip to the comma and then the data
  while (pos < message.size() && message[pos] != ',') {
    pos++;
  }
  if (pos < message.size()) {
    pos++;  // Skip ','
  }

  // Skip whitespace
  while (pos < message.size() && (message[pos] == ' ' || message[pos] == '\t')) {
    pos++;
  }

  // Extract the data portion (everything between comma and final ']')
  if (pos < message.size() && message.back() == ']') {
    jsonData = message.substr(pos, message.size() - pos - 1);
  } else {
    jsonData = message.substr(pos);
  }

  return true;
}

bool sendMessage(WiFiClient& client, uint8_t opcode, const char* json) {
  // Format: "length[opcode, json]"
  std::string message = "[";
  message += std::to_string(opcode);
  message += ", ";
  message += json;
  message += "]";

  std::string lengthPrefix = std::to_string(message.size());
  std::string fullMessage = lengthPrefix + message;

  size_t written = client.write(reinterpret_cast<const uint8_t*>(fullMessage.c_str()), fullMessage.size());
  return written == fullMessage.size();
}

bool sendRawBytes(WiFiClient& client, const uint8_t* data, size_t len) {
  size_t written = 0;
  while (written < len) {
    size_t toWrite = std::min(len - written, static_cast<size_t>(4096));
    size_t result = client.write(data + written, toWrite);
    if (result == 0) {
      return false;
    }
    written += result;
  }
  return true;
}

std::string computePasswordHash(const std::string& password, const std::string& challenge) {
  std::string combined = password + challenge;

  uint8_t hash[20];
  mbedtls_sha1(reinterpret_cast<const uint8_t*>(combined.c_str()), combined.size(), hash);

  // Convert to hex string
  char hexHash[41];
  for (int i = 0; i < 20; i++) {
    sprintf(hexHash + i * 2, "%02x", hash[i]);
  }
  hexHash[40] = '\0';

  return std::string(hexHash);
}

std::string extractJsonString(const std::string& json, const char* key) {
  // Look for "key": "value" or "key":"value"
  std::string searchKey = "\"";
  searchKey += key;
  searchKey += "\"";

  size_t keyPos = json.find(searchKey);
  if (keyPos == std::string::npos) {
    return "";
  }

  // Find the colon
  size_t colonPos = json.find(':', keyPos + searchKey.size());
  if (colonPos == std::string::npos) {
    return "";
  }

  // Skip whitespace and find opening quote
  size_t valueStart = colonPos + 1;
  while (valueStart < json.size() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
    valueStart++;
  }

  if (valueStart >= json.size() || json[valueStart] != '"') {
    return "";  // Not a string value
  }
  valueStart++;  // Skip opening quote

  // Find closing quote (handle escapes)
  std::string result;
  bool escaped = false;
  for (size_t i = valueStart; i < json.size(); i++) {
    if (escaped) {
      switch (json[i]) {
        case 'n':
          result += '\n';
          break;
        case 'r':
          result += '\r';
          break;
        case 't':
          result += '\t';
          break;
        case '\\':
          result += '\\';
          break;
        case '"':
          result += '"';
          break;
        default:
          result += json[i];
          break;
      }
      escaped = false;
    } else if (json[i] == '\\') {
      escaped = true;
    } else if (json[i] == '"') {
      break;  // End of string
    } else {
      result += json[i];
    }
  }

  return result;
}

int64_t extractJsonInt(const std::string& json, const char* key) {
  // Look for "key": 123 or "key":123
  std::string searchKey = "\"";
  searchKey += key;
  searchKey += "\"";

  size_t keyPos = json.find(searchKey);
  if (keyPos == std::string::npos) {
    return 0;
  }

  // Find the colon
  size_t colonPos = json.find(':', keyPos + searchKey.size());
  if (colonPos == std::string::npos) {
    return 0;
  }

  // Skip whitespace
  size_t valueStart = colonPos + 1;
  while (valueStart < json.size() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
    valueStart++;
  }

  // Parse the number
  bool negative = false;
  if (valueStart < json.size() && json[valueStart] == '-') {
    negative = true;
    valueStart++;
  }

  int64_t result = 0;
  while (valueStart < json.size() && json[valueStart] >= '0' && json[valueStart] <= '9') {
    result = result * 10 + (json[valueStart] - '0');
    valueStart++;
  }

  return negative ? -result : result;
}

bool extractJsonBool(const std::string& json, const char* key, bool defaultValue) {
  // Look for "key": true/false
  std::string searchKey = "\"";
  searchKey += key;
  searchKey += "\"";

  size_t keyPos = json.find(searchKey);
  if (keyPos == std::string::npos) {
    return defaultValue;
  }

  // Find the colon
  size_t colonPos = json.find(':', keyPos + searchKey.size());
  if (colonPos == std::string::npos) {
    return defaultValue;
  }

  // Skip whitespace
  size_t valueStart = colonPos + 1;
  while (valueStart < json.size() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
    valueStart++;
  }

  // Check for true/false
  if (json.compare(valueStart, 4, "true") == 0) {
    return true;
  } else if (json.compare(valueStart, 5, "false") == 0) {
    return false;
  }

  return defaultValue;
}

std::string escapeJsonString(const std::string& str) {
  std::string result;
  result.reserve(str.size() + 16);  // Preallocate with some extra space

  for (char c : str) {
    switch (c) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (c >= 0 && c < 32) {
          // Other control characters - skip or encode as \uXXXX
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          result += buf;
        } else {
          result += c;
        }
        break;
    }
  }

  return result;
}

}  // namespace CalibreProtocol
