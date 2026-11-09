#pragma once

#include <WebServer.h>

namespace papyrix {

class RequestWebServer : public WebServer {
 public:
  using WebServer::WebServer;

  ~RequestWebServer() override {
    // Arduino-ESP32 3.3.12 does not free request arguments.
    delete[] _currentArgs;
    _currentArgs = nullptr;
    delete[] _postArgs;
    _postArgs = nullptr;
  }
};

}  // namespace papyrix
