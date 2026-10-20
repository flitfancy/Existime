#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
extern uint32_t testNow;
inline uint32_t millis() { return testNow; }
inline void delay(uint32_t ms) { testNow += ms; }
class String {
 public:
  std::string text;
  String() = default;
  String(const char* s) : text(s) {}
  template<class T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
  String(T value) : text(std::to_string(value)) {}
  String(float value, int) : text(std::to_string(value)) {}
  void reserve(size_t n) { text.reserve(n); }
  size_t length() const { return text.size(); }
  String& operator+=(char c) { text += c; return *this; }
  String& operator+=(const char* s) { text += s; return *this; }
  String& operator+=(const String& s) { text += s.text; return *this; }
  template<class T> String& operator+=(T n) { text += std::to_string(n); return *this; }
};
struct FakeSerial {
  template<class T> void println(const T&) {}
  template<class... Args> void printf(const char*, Args...) {}
};
static FakeSerial Serial;
