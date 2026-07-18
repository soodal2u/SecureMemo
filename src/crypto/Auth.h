#pragma once

#include "crypto/Crypto.h"

#include <string>
#include <vector>

namespace securememo {

struct AuthMeta {
  std::vector<uint8_t> salt;       // Crypto::kSaltSize
  std::vector<uint8_t> verifier;   // 32-byte HMAC of fixed message
  uint32_t iterations = Crypto::kPbkdf2Iterations;
};

class Auth {
 public:
  static bool Create(const std::wstring& password, AuthMeta& meta, SecureBytes& outKey);
  static bool VerifyAndDerive(const std::wstring& password, const AuthMeta& meta,
                              SecureBytes& outKey);
  static bool ChangePassword(const std::wstring& oldPassword,
                             const std::wstring& newPassword,
                             AuthMeta& meta,
                             SecureBytes& outNewKey);
};

}  // namespace securememo
