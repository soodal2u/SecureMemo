// Standalone smoke test: create vault, unlock, save round-trip
#include "crypto/VaultStorage.h"
#include "crypto/Auth.h"
#include "crypto/Crypto.h"

#include <iostream>

using namespace securememo;

int main() {
  // Isolate test data dir by temporarily not using real appdata overwrite carefully:
  // We test crypto primitives + auth only to avoid clobbering user vault.

  std::wstring password = L"test-password-1234";
  AuthMeta meta;
  SecureBytes key;
  if (!Auth::Create(password, meta, key)) {
    std::cerr << "Auth::Create failed\n";
    return 1;
  }

  SecureBytes key2;
  if (!Auth::VerifyAndDerive(password, meta, key2)) {
    std::cerr << "Verify good password failed\n";
    return 1;
  }

  SecureBytes bad;
  if (Auth::VerifyAndDerive(L"wrong", meta, bad)) {
    std::cerr << "Verify bad password unexpectedly succeeded\n";
    return 1;
  }

  const char* msg = "hello-secure-memo-한글";
  std::vector<uint8_t> blob;
  if (!Crypto::Encrypt(key, reinterpret_cast<const uint8_t*>(msg), strlen(msg), blob)) {
    std::cerr << "Encrypt failed\n";
    return 1;
  }

  SecureBytes plain;
  if (!Crypto::Decrypt(key2, blob.data(), blob.size(), plain)) {
    std::cerr << "Decrypt failed\n";
    return 1;
  }
  std::string out(reinterpret_cast<char*>(plain.data()), plain.size());
  if (out != msg) {
    std::cerr << "Roundtrip mismatch\n";
    return 1;
  }

  VaultData data;
  data.version = 1;
  data.settings.autoLockMinutes = 3;
  Note n;
  n.id = NewNoteId();
  n.content = L"테스트 메모 내용";
  n.color = L"blue";
  n.x = 10; n.y = 20; n.width = 300; n.height = 250;
  n.alwaysOnTop = true;
  n.updatedAt = NowTimestamp();
  data.notes.push_back(n);

  std::string ser = SerializeVault(data);
  VaultData data2;
  if (!DeserializeVault(ser, data2)) {
    std::cerr << "Deserialize failed\n";
    return 1;
  }
  if (data2.notes.size() != 1 || data2.notes[0].content != L"테스트 메모 내용") {
    std::cerr << "Serialize roundtrip failed\n";
    return 1;
  }

  std::cout << "OK crypto+auth+serialize smoke test passed\n";
  key.Wipe();
  key2.Wipe();
  plain.Wipe();
  return 0;
}
