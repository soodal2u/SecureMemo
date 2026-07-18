#include "crypto/Auth.h"

namespace securememo {
namespace {
constexpr uint8_t kVerifierMsg[] = "SecureMemo-v1-password-verifier";
}

bool Auth::Create(const std::wstring& password, AuthMeta& meta, SecureBytes& outKey) {
  if (password.empty()) return false;

  meta.salt.resize(Crypto::kSaltSize);
  if (!Crypto::RandomBytes(meta.salt.data(), meta.salt.size())) return false;
  meta.iterations = Crypto::kPbkdf2Iterations;

  if (!Crypto::DeriveKey(password, meta.salt.data(), meta.salt.size(), outKey,
                         meta.iterations)) {
    return false;
  }

  if (!Crypto::HmacSha256(outKey, kVerifierMsg, sizeof(kVerifierMsg) - 1, meta.verifier)) {
    outKey.Wipe();
    return false;
  }
  return true;
}

bool Auth::VerifyAndDerive(const std::wstring& password, const AuthMeta& meta,
                           SecureBytes& outKey) {
  if (password.empty() || meta.salt.size() != Crypto::kSaltSize ||
      meta.verifier.size() != 32) {
    return false;
  }

  const uint32_t iters =
      meta.iterations ? meta.iterations : Crypto::kPbkdf2Iterations;
  if (!Crypto::DeriveKey(password, meta.salt.data(), meta.salt.size(), outKey, iters)) {
    return false;
  }

  std::vector<uint8_t> mac;
  if (!Crypto::HmacSha256(outKey, kVerifierMsg, sizeof(kVerifierMsg) - 1, mac)) {
    outKey.Wipe();
    return false;
  }

  uint8_t diff = 0;
  for (size_t i = 0; i < 32; ++i) {
    diff |= static_cast<uint8_t>(mac[i] ^ meta.verifier[i]);
  }
  SecureWipeContainer(mac);

  if (diff != 0) {
    outKey.Wipe();
    return false;
  }
  return true;
}

bool Auth::ChangePassword(const std::wstring& oldPassword,
                          const std::wstring& newPassword,
                          AuthMeta& meta,
                          SecureBytes& outNewKey) {
  SecureBytes oldKey;
  if (!VerifyAndDerive(oldPassword, meta, oldKey)) {
    return false;
  }
  oldKey.Wipe();
  return Create(newPassword, meta, outNewKey);
}

}  // namespace securememo
