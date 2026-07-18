#pragma once

#include "util/SecureBuffer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace securememo {

class Crypto {
 public:
  static constexpr size_t kKeySize = 32;
  static constexpr size_t kNonceSize = 12;
  static constexpr size_t kTagSize = 16;
  static constexpr size_t kSaltSize = 16;
  // VeraCrypt-inspired: high PBKDF2 work factor to slow offline brute-force
  static constexpr uint32_t kPbkdf2Iterations = 600000;

  static bool RandomBytes(uint8_t* out, size_t len);

  // PBKDF2-HMAC-SHA256 (iterations from vault header; default for new vaults)
  static bool DeriveKey(const std::wstring& password,
                        const uint8_t* salt, size_t saltLen,
                        SecureBytes& outKey,
                        uint32_t iterations = kPbkdf2Iterations);

  // AES-256-GCM encrypt: out = nonce(12) || ciphertext || tag(16)
  static bool Encrypt(const SecureBytes& key,
                      const uint8_t* plain, size_t plainLen,
                      std::vector<uint8_t>& outBlob);

  // Decrypt blob produced by Encrypt
  static bool Decrypt(const SecureBytes& key,
                      const uint8_t* blob, size_t blobLen,
                      SecureBytes& outPlain);

  // HMAC-SHA256 for password verifier
  static bool HmacSha256(const SecureBytes& key,
                         const uint8_t* data, size_t dataLen,
                         std::vector<uint8_t>& outMac);
};

// UTF-8 / UTF-16 helpers
std::string WideToUtf8(const std::wstring& w);
std::wstring Utf8ToWide(const std::string& u);

}  // namespace securememo
