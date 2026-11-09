#pragma once

#include <Arduino.h>

#include <cstddef>

class WebServer {
 public:
  explicit WebServer(int = 80) {}
  virtual ~WebServer() = default;

  inline static size_t liveRequestArguments = 0;

 protected:
  struct RequestArgument {
    String key;
    String value;
    RequestArgument* next = nullptr;

    RequestArgument() { ++liveRequestArguments; }
    ~RequestArgument() { --liveRequestArguments; }
  };

  // Arduino-ESP32 3.3.12 does not release these arrays in its destructor.
  RequestArgument* _currentArgs = nullptr;
  RequestArgument* _postArgs = nullptr;
};
