#pragma once

#include <Arduino.h>

#include <cstddef>

constexpr int CONFIG_LWIP_TCP_ACCEPTMBOX_SIZE = 6;

class NetworkServer {
 public:
  explicit NetworkServer(int = 80, uint8_t = 4) {}
};

class WebServer {
 public:
  explicit WebServer(int = 80) {}
  virtual ~WebServer() = default;

  inline static size_t liveRequestArguments = 0;

 protected:
  NetworkServer _server;

  struct RequestArgument {
    String key;
    String value;
    RequestArgument* next = nullptr;

    RequestArgument() { ++liveRequestArguments; }
    ~RequestArgument() { --liveRequestArguments; }
  };

  // The SDK destructor does not free these arrays.
  RequestArgument* _currentArgs = nullptr;
  RequestArgument* _postArgs = nullptr;
};
