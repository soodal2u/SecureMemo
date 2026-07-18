#include "crypto/VaultStorage.h"

#include <fstream>
#include <windows.h>

namespace securememo {
namespace {

constexpr char kMagic[4] = {'S', 'M', 'V', '1'};

#pragma pack(push, 1)
struct VaultHeader {
  char magic[4];
  uint32_t version;       // 1
  uint32_t iterations;
  uint32_t saltLen;       // 16
  uint32_t verifierLen;   // 32
  uint32_t blobLen;       // nonce+cipher+tag
};
#pragma pack(pop)

std::filesystem::path GetAppDataSecureMemo() {
  wchar_t path[MAX_PATH]{};
  DWORD n = GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    return std::filesystem::path(L".") / L"SecureMemoData";
  }
  return std::filesystem::path(path) / L"SecureMemo";
}

}  // namespace

VaultStorage::VaultStorage() : dataDir_(GetAppDataSecureMemo()) {}

bool VaultStorage::Exists() const {
  std::error_code ec;
  return std::filesystem::exists(VaultPath(), ec);
}

bool VaultStorage::EnsureDataDir() const {
  std::error_code ec;
  std::filesystem::create_directories(dataDir_, ec);
  return !ec;
}

bool VaultStorage::LoadRaw(std::vector<uint8_t>& fileBytes) const {
  std::ifstream in(VaultPath(), std::ios::binary);
  if (!in) return false;
  in.seekg(0, std::ios::end);
  auto sz = in.tellg();
  if (sz <= 0) return false;
  in.seekg(0, std::ios::beg);
  fileBytes.resize(static_cast<size_t>(sz));
  in.read(reinterpret_cast<char*>(fileBytes.data()), sz);
  return static_cast<bool>(in);
}

bool VaultStorage::WriteAtomic(const std::vector<uint8_t>& fileBytes) const {
  if (!EnsureDataDir()) return false;
  auto tmp = dataDir_ / L"vault.dat.tmp";
  auto finalPath = VaultPath();
  auto bak = dataDir_ / L"vault.dat.bak";

  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(fileBytes.data()),
              static_cast<std::streamsize>(fileBytes.size()));
    if (!out) return false;
    out.flush();
  }

  std::error_code ec;
  // Keep previous vault as bak before replace (extra safety against partial write)
  if (std::filesystem::exists(finalPath, ec)) {
    std::filesystem::copy_file(finalPath, bak,
                               std::filesystem::copy_options::overwrite_existing, ec);
  }
  std::filesystem::rename(tmp, finalPath, ec);
  if (ec) {
    std::filesystem::remove(finalPath, ec);
    std::filesystem::rename(tmp, finalPath, ec);
  }
  return !ec || std::filesystem::exists(finalPath);
}

bool VaultStorage::ParseFile(const std::vector<uint8_t>& fileBytes, AuthMeta& meta,
                             std::vector<uint8_t>& cipherBlob) const {
  if (fileBytes.size() < sizeof(VaultHeader)) return false;
  VaultHeader hdr{};
  memcpy(&hdr, fileBytes.data(), sizeof(hdr));
  if (memcmp(hdr.magic, kMagic, 4) != 0) return false;
  if (hdr.version != 1) return false;
  if (hdr.saltLen != Crypto::kSaltSize || hdr.verifierLen != 32) return false;

  size_t offset = sizeof(VaultHeader);
  if (fileBytes.size() < offset + hdr.saltLen + hdr.verifierLen + hdr.blobLen) {
    return false;
  }

  meta.iterations = hdr.iterations;
  meta.salt.assign(fileBytes.begin() + static_cast<std::ptrdiff_t>(offset),
                   fileBytes.begin() + static_cast<std::ptrdiff_t>(offset + hdr.saltLen));
  offset += hdr.saltLen;
  meta.verifier.assign(fileBytes.begin() + static_cast<std::ptrdiff_t>(offset),
                       fileBytes.begin() + static_cast<std::ptrdiff_t>(offset + hdr.verifierLen));
  offset += hdr.verifierLen;
  cipherBlob.assign(fileBytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    fileBytes.begin() + static_cast<std::ptrdiff_t>(offset + hdr.blobLen));
  return true;
}

bool VaultStorage::BuildFile(const AuthMeta& meta, const std::vector<uint8_t>& cipherBlob,
                             std::vector<uint8_t>& out) const {
  VaultHeader hdr{};
  memcpy(hdr.magic, kMagic, 4);
  hdr.version = 1;
  hdr.iterations = meta.iterations ? meta.iterations : Crypto::kPbkdf2Iterations;
  hdr.saltLen = static_cast<uint32_t>(meta.salt.size());
  hdr.verifierLen = static_cast<uint32_t>(meta.verifier.size());
  hdr.blobLen = static_cast<uint32_t>(cipherBlob.size());

  out.clear();
  out.resize(sizeof(hdr) + meta.salt.size() + meta.verifier.size() + cipherBlob.size());
  size_t o = 0;
  memcpy(out.data() + o, &hdr, sizeof(hdr));
  o += sizeof(hdr);
  memcpy(out.data() + o, meta.salt.data(), meta.salt.size());
  o += meta.salt.size();
  memcpy(out.data() + o, meta.verifier.data(), meta.verifier.size());
  o += meta.verifier.size();
  if (!cipherBlob.empty()) {
    memcpy(out.data() + o, cipherBlob.data(), cipherBlob.size());
  }
  return true;
}

bool VaultStorage::CreateNew(const std::wstring& password, SecureBytes& outKey,
                             VaultData& outData) {
  AuthMeta meta;
  if (!Auth::Create(password, meta, outKey)) return false;

  outData = VaultData{};
  outData.version = 1;
  outData.settings.autoLockMinutes = 5;

  // One starter note (placement fixed on window create)
  Note n;
  n.id = NewNoteId();
  n.content = L"";
  n.color = L"yellow";
  n.x = -1;
  n.y = -1;
  n.width = 320;
  n.height = 320;
  n.alwaysOnTop = true;
  n.visible = true;
  n.updatedAt = NowTimestamp();
  outData.notes.push_back(n);

  return SaveWithNewKey(outKey, outData, meta);
}

bool VaultStorage::Unlock(const std::wstring& password, SecureBytes& outKey,
                          VaultData& outData) {
  std::vector<uint8_t> fileBytes;
  if (!LoadRaw(fileBytes)) return false;

  AuthMeta meta;
  std::vector<uint8_t> blob;
  if (!ParseFile(fileBytes, meta, blob)) return false;

  if (!Auth::VerifyAndDerive(password, meta, outKey)) return false;

  SecureBytes plain;
  if (!Crypto::Decrypt(outKey, blob.data(), blob.size(), plain)) {
    outKey.Wipe();
    return false;
  }

  std::string payload(reinterpret_cast<char*>(plain.data()), plain.size());
  plain.Wipe();
  bool ok = DeserializeVault(payload, outData);
  SecureWipeString(payload);
  if (!ok) {
    outKey.Wipe();
    return false;
  }
  return true;
}

bool VaultStorage::Save(const SecureBytes& key, const VaultData& data) {
  // Need existing meta (salt/verifier) from file — re-read header only
  std::vector<uint8_t> fileBytes;
  if (!LoadRaw(fileBytes)) return false;
  AuthMeta meta;
  std::vector<uint8_t> oldBlob;
  if (!ParseFile(fileBytes, meta, oldBlob)) return false;
  SecureWipeContainer(oldBlob);

  std::string payload = SerializeVault(data);
  std::vector<uint8_t> blob;
  bool ok = Crypto::Encrypt(key, reinterpret_cast<const uint8_t*>(payload.data()),
                            payload.size(), blob);
  SecureWipeString(payload);
  if (!ok) return false;

  std::vector<uint8_t> out;
  if (!BuildFile(meta, blob, out)) return false;
  return WriteAtomic(out);
}

bool VaultStorage::SaveWithNewKey(const SecureBytes& newKey, const VaultData& data,
                                  const AuthMeta& newMeta) {
  std::string payload = SerializeVault(data);
  std::vector<uint8_t> blob;
  bool ok = Crypto::Encrypt(newKey, reinterpret_cast<const uint8_t*>(payload.data()),
                            payload.size(), blob);
  SecureWipeString(payload);
  if (!ok) return false;

  std::vector<uint8_t> out;
  if (!BuildFile(newMeta, blob, out)) return false;
  return WriteAtomic(out);
}

}  // namespace securememo
