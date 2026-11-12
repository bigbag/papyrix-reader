#pragma once

#include <WebServer.h>

namespace papyrix {

class RequestWebServer : public WebServer {
 public:
  explicit RequestWebServer(int port = 80) : WebServer(port) {
    _server = NetworkServer(port, CONFIG_LWIP_TCP_ACCEPTMBOX_SIZE);
  }

  ~RequestWebServer() override {
    // The SDK destructor does not free request arguments.
    delete[] _currentArgs;
    _currentArgs = nullptr;
    delete[] _postArgs;
    _postArgs = nullptr;
  }
};

}  // namespace papyrix
