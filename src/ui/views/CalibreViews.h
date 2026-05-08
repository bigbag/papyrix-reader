#pragma once

#include <GfxRenderer.h>
#include <I18n.h>
#include <Theme.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../Elements.h"

namespace ui {

struct CalibreView {
  static constexpr int MAX_STATUS_LEN = 64;
  static constexpr int MAX_HELP_LEN = 96;

  enum class Status : uint8_t { Waiting, Connecting, Receiving, Complete, Error };

  ButtonBar buttons;
  char statusMsg[MAX_STATUS_LEN] = "";
  char helpText[MAX_HELP_LEN] = "";
  Status status = Status::Waiting;
  int32_t received = 0;
  int32_t total = 0;
  bool needsRender = true;
  bool showRestartOption = false;

  void setWaiting() {
    status = Status::Waiting;
    strncpy(statusMsg, tr(WAITING_FOR_CALIBRE), MAX_STATUS_LEN - 1);
    statusMsg[MAX_STATUS_LEN - 1] = '\0';
    helpText[0] = '\0';
    showRestartOption = false;
    buttons = ButtonBar{tr(CANCEL)};
    needsRender = true;
  }

  void setWaitingWithIP(const char* ip) {
    status = Status::Waiting;
    snprintf(statusMsg, MAX_STATUS_LEN, tr(FMT_IP), ip);
    strncpy(helpText, tr(CALIBRE_HELP), MAX_HELP_LEN - 1);
    helpText[MAX_HELP_LEN - 1] = '\0';
    showRestartOption = false;
    buttons = ButtonBar{tr(CANCEL)};
    needsRender = true;
  }

  void setConnecting() {
    status = Status::Connecting;
    strncpy(statusMsg, tr(CONNECTING_TO_CALIBRE), MAX_STATUS_LEN - 1);
    statusMsg[MAX_STATUS_LEN - 1] = '\0';
    helpText[0] = '\0';
    showRestartOption = false;
    buttons = ButtonBar{tr(CANCEL)};
    needsRender = true;
  }

  void setReceiving(const char* filename, int recv, int tot) {
    status = Status::Receiving;
    strncpy(statusMsg, filename, MAX_STATUS_LEN - 1);
    statusMsg[MAX_STATUS_LEN - 1] = '\0';
    helpText[0] = '\0';
    received = recv;
    total = tot;
    showRestartOption = false;
    buttons = ButtonBar{tr(CANCEL)};
    needsRender = true;
  }

  void setComplete(int bookCount) {
    status = Status::Complete;
    snprintf(statusMsg, MAX_STATUS_LEN, tr(FMT_RECEIVED_BOOKS), bookCount);
    helpText[0] = '\0';
    showRestartOption = true;
    buttons = ButtonBar{tr(BACK), tr(RESTART)};
    needsRender = true;
  }

  void setError(const char* msg) {
    status = Status::Error;
    strncpy(statusMsg, msg, MAX_STATUS_LEN - 1);
    statusMsg[MAX_STATUS_LEN - 1] = '\0';
    helpText[0] = '\0';
    showRestartOption = true;
    buttons = ButtonBar{tr(BACK), tr(RESTART)};
    needsRender = true;
  }

  void setDisconnected() {
    status = Status::Waiting;
    strncpy(statusMsg, tr(DISCONNECTED_RESTART), MAX_STATUS_LEN - 1);
    statusMsg[MAX_STATUS_LEN - 1] = '\0';
    helpText[0] = '\0';
    showRestartOption = true;
    buttons = ButtonBar{tr(BACK), tr(RESTART)};
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const CalibreView& v);

}  // namespace ui
