#include "HttpDownloader.h"

#include <HTTPClient.h>
#include <HardwareSerial.h>
#include <WiFiClientSecure.h>
#include <base64.h>

#include <memory>

#include "config.h"

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  const std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure());
  client->setInsecure();
  client->setHandshakeTimeout(30);  // 30 seconds for slow SSL handshakes
  HTTPClient http;

  Serial.printf("[%lu] [HTTP] Fetching: %s\n", millis(), url.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "Papyrix-ESP32-" CROSSPOINT_VERSION);

  // Add Basic Auth if credentials provided
  if (!username.empty()) {
    const std::string credentials = username + ":" + password;
    const String encoded = base64::encode(credentials.c_str());
    http.addHeader("Authorization", String("Basic ") + encoded);
    Serial.printf("[%lu] [HTTP] Using Basic Auth for user: %s\n", millis(), username.c_str());
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[%lu] [HTTP] Fetch failed: %d\n", millis(), httpCode);
    http.end();
    return false;
  }

  outContent = http.getString().c_str();
  http.end();
  Serial.printf("[%lu] [HTTP] Fetched %zu bytes\n", millis(), outContent.size());
  return true;
}

bool HttpDownloader::fetchUrlStreaming(const std::string& url, ChunkCallback onChunk, const std::string& username,
                                       const std::string& password) {
  const std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure());
  client->setInsecure();
  client->setHandshakeTimeout(30);
  HTTPClient http;

  Serial.printf("[%lu] [HTTP] Streaming fetch: %s\n", millis(), url.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "Papyrix-ESP32-" CROSSPOINT_VERSION);

  if (!username.empty()) {
    const std::string credentials = username + ":" + password;
    const String encoded = base64::encode(credentials.c_str());
    http.addHeader("Authorization", String("Basic ") + encoded);
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[%lu] [HTTP] Fetch failed: %d\n", millis(), httpCode);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (!stream) {
    Serial.printf("[%lu] [HTTP] Failed to get stream\n", millis());
    http.end();
    return false;
  }

  const size_t contentLength = http.getSize();
  size_t totalRead = 0;
  char buffer[DOWNLOAD_CHUNK_SIZE];

  constexpr unsigned long TIMEOUT_MS = 30000;
  unsigned long lastProgressTime = millis();

  while (http.connected() && (contentLength == 0 || totalRead < contentLength)) {
    const size_t available = stream->available();
    if (available == 0) {
      if (millis() - lastProgressTime > TIMEOUT_MS) {
        Serial.printf("[%lu] [HTTP] Streaming timeout\n", millis());
        http.end();
        return false;
      }
      delay(1);
      continue;
    }

    lastProgressTime = millis();

    const size_t toRead = available < DOWNLOAD_CHUNK_SIZE ? available : DOWNLOAD_CHUNK_SIZE;
    const size_t bytesRead = stream->readBytes(buffer, toRead);

    if (bytesRead == 0) {
      break;
    }

    totalRead += bytesRead;

    if (!onChunk(buffer, bytesRead)) {
      Serial.printf("[%lu] [HTTP] Streaming aborted by callback\n", millis());
      http.end();
      return true;  // Not an error - callback requested abort
    }
  }

  http.end();
  Serial.printf("[%lu] [HTTP] Streamed %zu bytes\n", millis(), totalRead);
  return true;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, const std::string& username,
                                                             const std::string& password) {
  const std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure());
  client->setInsecure();
  client->setHandshakeTimeout(30);  // 30 seconds for slow SSL handshakes
  HTTPClient http;

  Serial.printf("[%lu] [HTTP] Downloading: %s\n", millis(), url.c_str());
  Serial.printf("[%lu] [HTTP] Destination: %s\n", millis(), destPath.c_str());

  http.begin(*client, url.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "Papyrix-ESP32-" CROSSPOINT_VERSION);

  // Add Basic Auth if credentials provided
  if (!username.empty()) {
    const std::string credentials = username + ":" + password;
    const String encoded = base64::encode(credentials.c_str());
    http.addHeader("Authorization", String("Basic ") + encoded);
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[%lu] [HTTP] Download failed: %d\n", millis(), httpCode);
    http.end();
    return HTTP_ERROR;
  }

  const size_t contentLength = http.getSize();
  Serial.printf("[%lu] [HTTP] Content-Length: %zu\n", millis(), contentLength);

  if (SdMan.exists(destPath.c_str())) {
    SdMan.remove(destPath.c_str());
  }

  FsFile file;
  if (!SdMan.openFileForWrite("HTTP", destPath.c_str(), file)) {
    Serial.printf("[%lu] [HTTP] Failed to open file for writing\n", millis());
    http.end();
    return FILE_ERROR;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (!stream) {
    Serial.printf("[%lu] [HTTP] Failed to get stream\n", millis());
    file.close();
    SdMan.remove(destPath.c_str());
    http.end();
    return HTTP_ERROR;
  }

  uint8_t buffer[DOWNLOAD_CHUNK_SIZE];
  size_t downloaded = 0;
  const size_t total = contentLength > 0 ? contentLength : 0;

  // Timeout tracking to prevent infinite loop on network stall
  constexpr unsigned long DOWNLOAD_TIMEOUT_MS = 30000;
  unsigned long lastProgressTime = millis();

  while (http.connected() && (contentLength == 0 || downloaded < contentLength)) {
    const size_t available = stream->available();
    if (available == 0) {
      // Check for timeout
      if (millis() - lastProgressTime > DOWNLOAD_TIMEOUT_MS) {
        Serial.printf("[%lu] [HTTP] Download timeout after %lu ms\n", millis(), DOWNLOAD_TIMEOUT_MS);
        file.close();
        SdMan.remove(destPath.c_str());
        http.end();
        return HTTP_ERROR;
      }
      delay(1);
      continue;
    }

    lastProgressTime = millis();  // Reset timeout on progress

    const size_t toRead = available < DOWNLOAD_CHUNK_SIZE ? available : DOWNLOAD_CHUNK_SIZE;
    const size_t bytesRead = stream->readBytes(buffer, toRead);

    if (bytesRead == 0) {
      break;
    }

    const size_t written = file.write(buffer, bytesRead);
    if (written != bytesRead) {
      Serial.printf("[%lu] [HTTP] Write failed: wrote %zu of %zu bytes\n", millis(), written, bytesRead);
      file.close();
      SdMan.remove(destPath.c_str());
      http.end();
      return FILE_ERROR;
    }

    downloaded += bytesRead;

    if (progress && total > 0) {
      progress(downloaded, total);
    }
  }

  file.close();
  http.end();
  Serial.printf("[%lu] [HTTP] Downloaded %zu bytes\n", millis(), downloaded);

  if (contentLength > 0 && downloaded != contentLength) {
    Serial.printf("[%lu] [HTTP] Size mismatch: got %zu, expected %zu\n", millis(), downloaded, contentLength);
    SdMan.remove(destPath.c_str());
    return HTTP_ERROR;
  }

  return OK;
}
