#include "crypto/Crypto.h"

#include <bcrypt.h>
#include <windows.h>

#include <cstring>

namespace securememo {
namespace {

struct AlgHandle {
  BCRYPT_ALG_HANDLE h = nullptr;
  ~AlgHandle() {
    if (h) BCryptCloseAlgorithmProvider(h, 0);
  }
};

struct KeyHandle {
  BCRYPT_KEY_HANDLE h = nullptr;
  ~KeyHandle() {
    if (h) BCryptDestroyKey(h);
  }
};

bool NtOk(NTSTATUS s) { return BCRYPT_SUCCESS(s); }

}  // namespace

bool Crypto::RandomBytes(uint8_t* out, size_t len) {
  if (!out || !len) return false;
  return NtOk(BCryptGenRandom(nullptr, out, static_cast<ULONG>(len),
                              BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

bool Crypto::DeriveKey(const std::wstring& password,
                       const uint8_t* salt, size_t saltLen,
                       SecureBytes& outKey,
                       uint32_t iterations) {
  if (!salt || saltLen == 0) return false;
  if (iterations < 10000) iterations = kPbkdf2Iterations;

  std::string passUtf8 = WideToUtf8(password);
  outKey.resize(kKeySize);

  AlgHandle alg;
  if (!NtOk(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_SHA256_ALGORITHM,
                                        nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
    SecureWipeString(passUtf8);
    return false;
  }

  NTSTATUS st = BCryptDeriveKeyPBKDF2(
      alg.h,
      reinterpret_cast<PUCHAR>(passUtf8.data()),
      static_cast<ULONG>(passUtf8.size()),
      const_cast<PUCHAR>(salt),
      static_cast<ULONG>(saltLen),
      iterations,
      outKey.data(),
      static_cast<ULONG>(outKey.size()),
      0);

  SecureWipeString(passUtf8);
  return NtOk(st);
}

bool Crypto::Encrypt(const SecureBytes& key,
                     const uint8_t* plain, size_t plainLen,
                     std::vector<uint8_t>& outBlob) {
  if (key.size() != kKeySize) return false;

  AlgHandle alg;
  if (!NtOk(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
    return false;
  }
  if (!NtOk(BCryptSetProperty(
          alg.h, BCRYPT_CHAINING_MODE,
          reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
          static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)),
          0))) {
    return false;
  }

  KeyHandle keyH;
  if (!NtOk(BCryptGenerateSymmetricKey(alg.h, &keyH.h, nullptr, 0,
                                       const_cast<PUCHAR>(key.data()),
                                       static_cast<ULONG>(key.size()), 0))) {
    return false;
  }

  uint8_t nonce[kNonceSize];
  if (!RandomBytes(nonce, sizeof(nonce))) return false;

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
  BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
  uint8_t tag[kTagSize]{};
  authInfo.pbNonce = nonce;
  authInfo.cbNonce = sizeof(nonce);
  authInfo.pbTag = tag;
  authInfo.cbTag = sizeof(tag);

  ULONG cbResult = 0;
  std::vector<uint8_t> cipher(plainLen ? plainLen : 1);

  NTSTATUS st = BCryptEncrypt(
      keyH.h,
      plainLen ? const_cast<PUCHAR>(plain) : nullptr,
      static_cast<ULONG>(plainLen),
      &authInfo,
      nullptr, 0,
      cipher.data(),
      static_cast<ULONG>(plainLen),
      &cbResult,
      0);

  if (!NtOk(st)) return false;
  cipher.resize(cbResult);

  outBlob.clear();
  outBlob.reserve(kNonceSize + cipher.size() + kTagSize);
  outBlob.insert(outBlob.end(), nonce, nonce + kNonceSize);
  outBlob.insert(outBlob.end(), cipher.begin(), cipher.end());
  outBlob.insert(outBlob.end(), tag, tag + kTagSize);

  SecureWipe(nonce, sizeof(nonce));
  SecureWipe(tag, sizeof(tag));
  return true;
}

bool Crypto::Decrypt(const SecureBytes& key,
                     const uint8_t* blob, size_t blobLen,
                     SecureBytes& outPlain) {
  if (key.size() != kKeySize) return false;
  if (!blob || blobLen < kNonceSize + kTagSize) return false;

  const size_t cipherLen = blobLen - kNonceSize - kTagSize;
  const uint8_t* nonce = blob;
  const uint8_t* cipher = blob + kNonceSize;
  const uint8_t* tag = blob + kNonceSize + cipherLen;

  AlgHandle alg;
  if (!NtOk(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
    return false;
  }
  if (!NtOk(BCryptSetProperty(
          alg.h, BCRYPT_CHAINING_MODE,
          reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
          static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)),
          0))) {
    return false;
  }

  KeyHandle keyH;
  if (!NtOk(BCryptGenerateSymmetricKey(alg.h, &keyH.h, nullptr, 0,
                                       const_cast<PUCHAR>(key.data()),
                                       static_cast<ULONG>(key.size()), 0))) {
    return false;
  }

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
  BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
  uint8_t tagCopy[kTagSize];
  memcpy(tagCopy, tag, kTagSize);
  uint8_t nonceCopy[kNonceSize];
  memcpy(nonceCopy, nonce, kNonceSize);
  authInfo.pbNonce = nonceCopy;
  authInfo.cbNonce = sizeof(nonceCopy);
  authInfo.pbTag = tagCopy;
  authInfo.cbTag = sizeof(tagCopy);

  outPlain.resize(cipherLen ? cipherLen : 1);
  ULONG cbResult = 0;

  NTSTATUS st = BCryptDecrypt(
      keyH.h,
      cipherLen ? const_cast<PUCHAR>(cipher) : nullptr,
      static_cast<ULONG>(cipherLen),
      &authInfo,
      nullptr, 0,
      outPlain.data(),
      static_cast<ULONG>(cipherLen),
      &cbResult,
      0);

  SecureWipe(nonceCopy, sizeof(nonceCopy));
  SecureWipe(tagCopy, sizeof(tagCopy));

  if (!NtOk(st)) {
    outPlain.Wipe();
    return false;
  }
  outPlain.resize(cbResult);
  return true;
}

bool Crypto::HmacSha256(const SecureBytes& key,
                        const uint8_t* data, size_t dataLen,
                        std::vector<uint8_t>& outMac) {
  AlgHandle alg;
  if (!NtOk(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_SHA256_ALGORITHM,
                                        nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
    return false;
  }

  BCRYPT_HASH_HANDLE hash = nullptr;
  if (!NtOk(BCryptCreateHash(alg.h, &hash, nullptr, 0,
                             const_cast<PUCHAR>(key.data()),
                             static_cast<ULONG>(key.size()), 0))) {
    return false;
  }

  if (dataLen && !NtOk(BCryptHashData(hash, const_cast<PUCHAR>(data),
                                      static_cast<ULONG>(dataLen), 0))) {
    BCryptDestroyHash(hash);
    return false;
  }

  outMac.resize(32);
  NTSTATUS st = BCryptFinishHash(hash, outMac.data(), 32, 0);
  BCryptDestroyHash(hash);
  return NtOk(st);
}

std::string WideToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                              nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                      out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring Utf8ToWide(const std::string& u) {
  if (u.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), static_cast<int>(u.size()),
                              nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, u.c_str(), static_cast<int>(u.size()),
                      out.data(), n);
  return out;
}

}  // namespace securememo
