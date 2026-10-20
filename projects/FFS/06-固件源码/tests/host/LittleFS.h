#pragma once
#include "Arduino.h"
struct File {
  explicit operator bool() const { return true; }
  template<class T> size_t print(const T&) { return 1; }
  template<class T> size_t println(const T&) { return 1; }
  size_t size() const { return 0; }
  void close() {}
};
struct FakeFS {
  bool begin(bool) { return true; }
  bool exists(const char*) { return false; }
  File open(const char*, const char*) { return {}; }
};
static FakeFS LittleFS;
