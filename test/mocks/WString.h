// Minimal WString.h for host tests
#pragma once

#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(unsigned long num, int base) {
    if (base == 10) {
      s_ = std::to_string(num);
    } else if (base == 16) {
      char buf[20];
      snprintf(buf, sizeof(buf), "%lx", num);
      s_ = buf;
    } else {
      s_ = "unsupported base";
    }
  }
  String(const String& other) = default;

  int length() const { return static_cast<int>(s_.size()); }
  const char* c_str() const { return s_.c_str(); }
  char operator[](size_t i) const { return s_[i]; }
  char charAt(size_t index) const {
    if (index >= s_.size()) return 0;
    return s_[index];
  }
  bool operator==(const char* rhs) const {
    if (!rhs) return s_.empty();
    return s_ == std::string(rhs);
  }
  bool operator==(const String& rhs) const { return s_ == rhs.s_; }
  bool operator!=(const char* rhs) const { return !(*this == rhs); }
  bool operator!=(const String& rhs) const { return !(*this == rhs); }
  String substring(int start, int end) const {
    if (start < 0) start = 0;
    if (end < start) end = start;
    if (end > (int)s_.size()) end = (int)s_.size();
    return String(s_.substr(start, end - start));
  }
  String substring(int start) const {
    if (start < 0) start = 0;
    if (start > (int)s_.size()) return String("");
    return String(s_.substr(start));
  }
  int lastIndexOf(char c) const {
    size_t pos = s_.rfind(c);
    return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
  }
  int indexOf(char c, int start = 0) const {
    if (start < 0) start = 0;
    size_t pos = s_.find(c, start);
    return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
  }
  int indexOf(const char* str, int start = 0) const {
    if (!str || start < 0) return -1;
    size_t pos = s_.find(str, start);
    return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
  }
  bool isEmpty() const { return s_.empty(); }
  String& operator+=(char c) {
    s_ += c;
    return *this;
  }
  String& operator+=(const char* str) {
    if (str) s_ += str;
    return *this;
  }
  String& operator+=(const String& other) {
    s_ += other.s_;
    return *this;
  }

  void reserve(size_t size) { s_.reserve(size); }

  void trim() {
    size_t start = 0;
    while (start < s_.size() && (s_[start] == ' ' || s_[start] == '\t' || s_[start] == '\n' || s_[start] == '\r')) {
      start++;
    }
    size_t end = s_.size();
    while (end > start && (s_[end - 1] == ' ' || s_[end - 1] == '\t' || s_[end - 1] == '\n' || s_[end - 1] == '\r')) {
      end--;
    }
    s_ = s_.substr(start, end - start);
  }

  void toLowerCase() {
    for (size_t i = 0; i < s_.size(); i++) {
      if (s_[i] >= 'A' && s_[i] <= 'Z') {
        s_[i] = s_[i] + 32;
      }
    }
  }

  long toInt() const {
    try {
      return std::stoi(s_);
    } catch (...) {
      return 0;
    }
  }

  bool operator<(const String& other) const { return s_ < other.s_; }

 private:
  std::string s_;
};

inline String operator+(const char* lhs, const String& rhs) {
  String result(lhs);
  result += rhs;
  return result;
}

inline String operator+(const String& lhs, const String& rhs) {
  String result(lhs);
  result += rhs;
  return result;
}
