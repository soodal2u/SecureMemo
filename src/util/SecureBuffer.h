#pragma once

#include <windows.h>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace securememo {

inline void SecureWipe(void* data, size_t size) {
  if (data && size) {
    SecureZeroMemory(data, size);
  }
}

template <typename T>
inline void SecureWipeContainer(T& c) {
  if (!c.empty()) {
    SecureWipe(c.data(), c.size() * sizeof(typename T::value_type));
    c.clear();
    c.shrink_to_fit();
  }
}

inline void SecureWipeString(std::wstring& s) {
  if (!s.empty()) {
    SecureWipe(s.data(), s.size() * sizeof(wchar_t));
    s.clear();
    s.shrink_to_fit();
  }
}

inline void SecureWipeString(std::string& s) {
  if (!s.empty()) {
    SecureWipe(s.data(), s.size());
    s.clear();
    s.shrink_to_fit();
  }
}

// RAII buffer wiped on destruction
class SecureBytes {
 public:
  SecureBytes() = default;
  explicit SecureBytes(size_t n) : data_(n) {}
  SecureBytes(const uint8_t* p, size_t n) : data_(p, p + n) {}

  SecureBytes(const SecureBytes&) = delete;
  SecureBytes& operator=(const SecureBytes&) = delete;

  SecureBytes(SecureBytes&& other) noexcept : data_(std::move(other.data_)) {}
  SecureBytes& operator=(SecureBytes&& other) noexcept {
    if (this != &other) {
      Wipe();
      data_ = std::move(other.data_);
    }
    return *this;
  }

  ~SecureBytes() { Wipe(); }

  uint8_t* data() { return data_.data(); }
  const uint8_t* data() const { return data_.data(); }
  size_t size() const { return data_.size(); }
  bool empty() const { return data_.empty(); }
  void resize(size_t n) { data_.resize(n); }
  void assign(const uint8_t* p, size_t n) { data_.assign(p, p + n); }
  std::vector<uint8_t>& vec() { return data_; }
  const std::vector<uint8_t>& vec() const { return data_; }

  void Wipe() { SecureWipeContainer(data_); }

 private:
  std::vector<uint8_t> data_;
};

}  // namespace securememo
