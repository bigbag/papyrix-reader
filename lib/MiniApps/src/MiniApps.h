#pragma once

#include <cassert>
#include <cstdint>

namespace miniapps {

enum class Action : uint8_t { None, Redraw, OpenMenu, CloseMenu, Exit, RetryExit, CancelExit, DiscardExit };
enum class ExitTarget : uint8_t { None, Menu, Apps, Sleep, Navigation };
enum class ExitResult : uint8_t { Ready, Blocked, OpenMenu };
enum class Refresh : uint8_t { None, Fast, Full };

struct Frame {
  Refresh refresh = Refresh::None;
  bool turnOffScreen = false;
};

template <typename Context, typename Event>
struct App {
  const char* name;
  void (*enter)(Context&);
  bool (*update)(Context&);
  Action (*onEvent)(Context&, const Event&, bool menuOpen);
  Frame (*render)(Context&, bool menuOpen);
  void (*exit)(Context&);
  ExitResult (*prepareExit)(Context&, ExitTarget) = nullptr;
  bool keepAwake = true;
  bool throttleInMenu = false;
};

template <typename Context, typename Event>
class Host {
 public:
  using Application = App<Context, Event>;

  const Application* active() const { return active_; }
  bool menuOpen() const { return menuOpen_; }
  ExitTarget pendingExit() const { return pendingExit_; }
  bool needsRender() const { return needsRender_; }
  bool keepAwake() const { return active_ && (active_->keepAwake || pendingExit_ != ExitTarget::None); }
  bool throttleInMenu() const { return active_ && menuOpen_ && active_->throttleInMenu; }

  void invalidate() { needsRender_ = active_ != nullptr; }
  void closeMenu() {
    menuOpen_ = false;
    invalidate();
  }

  bool start(Context& context, const Application& app) {
    if (active_ && requestExit(context, ExitTarget::Apps) == ExitTarget::None) return false;
    restore(app);
    if (active_->enter) active_->enter(context);
    return true;
  }

  void restore(const Application& app) {
    assert(!active_);
    active_ = &app;
    menuOpen_ = false;
    pendingExit_ = ExitTarget::None;
    needsRender_ = true;
  }

  void stop(Context& context) {
    if (active_ && active_->exit) active_->exit(context);
    abandon();
  }

  void abandon() {
    active_ = nullptr;
    menuOpen_ = false;
    pendingExit_ = ExitTarget::None;
    needsRender_ = false;
  }

  bool prepareExit(Context& context, ExitTarget target) {
    if (!active_) return true;
    if (pendingExit_ != ExitTarget::None && pendingExit_ != target) return false;
    const auto result = active_->prepareExit ? active_->prepareExit(context, target) : ExitResult::Ready;
    if (result == ExitResult::Ready) {
      pendingExit_ = ExitTarget::None;
      return true;
    }
    if (result == ExitResult::OpenMenu) {
      menuOpen_ = true;
      pendingExit_ = ExitTarget::None;
    } else {
      pendingExit_ = target;
    }
    invalidate();
    return false;
  }

  ExitTarget requestExit(Context& context, ExitTarget target) {
    if (!active_ || target == ExitTarget::None || !prepareExit(context, target)) return ExitTarget::None;
    if (target == ExitTarget::Menu) {
      closeMenu();
      return ExitTarget::None;
    }
    stop(context);
    return target;
  }

  ExitTarget handleEvent(Context& context, const Event& event) {
    if (!active_ || !active_->onEvent) return ExitTarget::None;
    const auto action = active_->onEvent(context, event, menuOpen_);
    if (action == Action::None) return ExitTarget::None;
    invalidate();
    switch (action) {
      case Action::OpenMenu:
        menuOpen_ = true;
        break;
      case Action::CloseMenu:
        if (menuOpen_) return requestExit(context, ExitTarget::Menu);
        break;
      case Action::Exit:
        return requestExit(context, ExitTarget::Apps);
      case Action::RetryExit:
        if (pendingExit_ != ExitTarget::None) return requestExit(context, pendingExit_);
        break;
      case Action::CancelExit:
        pendingExit_ = ExitTarget::None;
        break;
      case Action::DiscardExit:
        if (pendingExit_ != ExitTarget::None) {
          const auto target = pendingExit_;
          stop(context);
          return target;
        }
        break;
      case Action::None:
      case Action::Redraw:
        break;
    }
    return ExitTarget::None;
  }

  void update(Context& context) {
    if (active_ && !menuOpen_ && active_->update && active_->update(context)) invalidate();
  }

  Frame render(Context& context) {
    if (!active_ || !needsRender_) return {};
    const Frame frame = active_->render ? active_->render(context, menuOpen_) : Frame{};
    needsRender_ = false;
    return frame;
  }

 private:
  const Application* active_ = nullptr;
  ExitTarget pendingExit_ = ExitTarget::None;
  bool menuOpen_ = false;
  bool needsRender_ = false;
};

}  // namespace miniapps
