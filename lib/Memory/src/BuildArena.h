#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

class BuildArena {
 public:
  class Scope {
   public:
    Scope() = default;
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&& other) noexcept { moveFrom(other); }
    Scope& operator=(Scope&& other) noexcept {
      if (this != &other) {
        assert(!valid() && "overwriting a live BuildArena::Scope");
        moveFrom(other);
      }
      return *this;
    }
    ~Scope() {
      if (valid()) owner_->release(*this);
    }

    bool valid() const { return owner_ != nullptr && id_ != 0; }
    bool release() { return owner_ && owner_->release(*this); }
    bool commit() { return owner_ && owner_->commit(*this); }

   private:
    friend class BuildArena;
    Scope(BuildArena* owner, size_t start, uint32_t id, uint32_t parent)
        : owner_(owner), start_(start), id_(id), parentId_(parent) {}

    void moveFrom(Scope& other) {
      owner_ = other.owner_;
      start_ = other.start_;
      id_ = other.id_;
      parentId_ = other.parentId_;
      other.owner_ = nullptr;
      other.id_ = 0;
    }

    BuildArena* owner_ = nullptr;
    size_t start_ = 0;
    uint32_t id_ = 0;
    uint32_t parentId_ = 0;
  };

  explicit BuildArena(size_t capacity)
      : owned_(capacity == 0 ? nullptr : new (std::nothrow) uint8_t[capacity]),
        base_(owned_.get()),
        capacity_(base_ ? capacity : 0) {}
  BuildArena(uint8_t* buffer, size_t capacity) : base_(buffer), capacity_(buffer ? capacity : 0) {}

  BuildArena(const BuildArena&) = delete;
  BuildArena& operator=(const BuildArena&) = delete;

  bool valid() const { return base_ != nullptr; }
  size_t capacity() const { return capacity_; }
  size_t used() const { return cursor_; }
  size_t remaining() const { return cursor_ <= capacity_ ? capacity_ - cursor_ : 0; }
  size_t highWater() const { return highWater_; }
  size_t failedAllocSize() const { return failedAllocSize_; }
  uint32_t fallbackCount() const { return fallbackCount_; }
  uint32_t scopeFailureCount() const { return scopeFailureCount_; }

  void* alloc(size_t bytes, size_t align = alignof(std::max_align_t)) {
    if (!base_ || align == 0 || (align & (align - 1)) != 0 || cursor_ > SIZE_MAX - (align - 1)) {
      failedAllocSize_ = bytes;
      return nullptr;
    }
    const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(base_);
    if (baseAddress > UINTPTR_MAX - cursor_) {
      failedAllocSize_ = bytes;
      return nullptr;
    }
    const uintptr_t address = baseAddress + cursor_;
    if (address > UINTPTR_MAX - (align - 1)) {
      failedAllocSize_ = bytes;
      return nullptr;
    }
    const size_t padding = static_cast<size_t>((0 - address) & (align - 1));
    if (padding > capacity_ - cursor_) {
      failedAllocSize_ = bytes;
      return nullptr;
    }
    const size_t aligned = cursor_ + padding;
    if (bytes > capacity_ - aligned) {
      failedAllocSize_ = bytes;
      return nullptr;
    }
    cursor_ = aligned + bytes;
    if (cursor_ > highWater_) highWater_ = cursor_;
    return base_ + aligned;
  }

  template <typename T>
  T* allocArray(size_t count) {
    if (count != 0 && count > SIZE_MAX / sizeof(T)) {
      failedAllocSize_ = SIZE_MAX;
      return nullptr;
    }
    return static_cast<T*>(alloc(count * sizeof(T), alignof(T)));
  }

  Scope scope() {
    uint32_t id = nextScopeId_++;
    if (id == 0) id = nextScopeId_++;
    Scope result(this, cursor_, id, activeScopeId_);
    activeScopeId_ = id;
    return result;
  }

  bool release(Scope& scope) {
    if (!scope.valid() || scope.owner_ != this || scope.id_ != activeScopeId_) {
      ++scopeFailureCount_;
      return false;
    }
    cursor_ = scope.start_;
    activeScopeId_ = scope.parentId_;
    scope.owner_ = nullptr;
    scope.id_ = 0;
    return true;
  }

  bool commit(Scope& scope) {
    if (!scope.valid() || scope.owner_ != this || scope.id_ != activeScopeId_) {
      ++scopeFailureCount_;
      return false;
    }
    activeScopeId_ = scope.parentId_;
    scope.owner_ = nullptr;
    scope.id_ = 0;
    return true;
  }

  void noteFallback(size_t requested) {
    failedAllocSize_ = requested;
    ++fallbackCount_;
  }

  void reset() {
    cursor_ = 0;
    activeScopeId_ = 0;
  }

 private:
  std::unique_ptr<uint8_t[]> owned_;
  uint8_t* base_ = nullptr;
  size_t capacity_ = 0;
  size_t cursor_ = 0;
  size_t highWater_ = 0;
  size_t failedAllocSize_ = 0;
  uint32_t fallbackCount_ = 0;
  uint32_t scopeFailureCount_ = 0;
  uint32_t activeScopeId_ = 0;
  uint32_t nextScopeId_ = 1;
};
