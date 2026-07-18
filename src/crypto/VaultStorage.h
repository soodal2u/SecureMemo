#pragma once

#include "crypto/Auth.h"
#include "model/Note.h"

#include <filesystem>
#include <string>

namespace securememo {

class VaultStorage {
 public:
  VaultStorage();

  std::filesystem::path DataDir() const { return dataDir_; }
  std::filesystem::path VaultPath() const { return dataDir_ / L"vault.dat"; }

  bool Exists() const;
  bool EnsureDataDir() const;

  // Create new vault with password (empty notes)
  bool CreateNew(const std::wstring& password, SecureBytes& outKey, VaultData& outData);

  // Unlock existing vault
  bool Unlock(const std::wstring& password, SecureBytes& outKey, VaultData& outData);

  // Encrypt and atomically save
  bool Save(const SecureBytes& key, const VaultData& data);

  // Re-key vault after password change
  bool SaveWithNewKey(const SecureBytes& newKey, const VaultData& data,
                      const AuthMeta& newMeta);

 private:
  std::filesystem::path dataDir_;

  bool LoadRaw(std::vector<uint8_t>& fileBytes) const;
  bool WriteAtomic(const std::vector<uint8_t>& fileBytes) const;
  bool ParseFile(const std::vector<uint8_t>& fileBytes, AuthMeta& meta,
                 std::vector<uint8_t>& cipherBlob) const;
  bool BuildFile(const AuthMeta& meta, const std::vector<uint8_t>& cipherBlob,
                 std::vector<uint8_t>& out) const;
};

}  // namespace securememo
