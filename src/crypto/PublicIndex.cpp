#include "crypto/PublicIndex.h"

#include <fstream>
#include <windows.h>

namespace securememo {
namespace {

std::filesystem::path GetAppDataSecureMemo() {
  wchar_t path[MAX_PATH]{};
  DWORD n = GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    return std::filesystem::path(L".") / L"SecureMemoData";
  }
  return std::filesystem::path(path) / L"SecureMemo";
}

#pragma pack(push, 1)
struct FileV2 {
  char magic[4];  // SMP2
  uint32_t version;
  uint32_t noteCount;
  int32_t listX, listY, listW, listH;
  uint8_t hasList;
  uint8_t pad[3];
};
#pragma pack(pop)

}  // namespace

PublicIndexStore::PublicIndexStore() : dataDir_(GetAppDataSecureMemo()) {}

std::filesystem::path PublicIndexStore::Path() const {
  return dataDir_ / L"public.idx";
}

bool PublicIndexStore::Load(PublicIndex& out) const {
  out = PublicIndex{};
  std::ifstream in(Path(), std::ios::binary);
  if (!in) return false;
  char magic[4]{};
  in.read(magic, 4);
  if (!in) return false;

  // v1: SMP1 + version + noteCount
  if (magic[0] == 'S' && magic[1] == 'M' && magic[2] == 'P' && magic[3] == '1') {
    in.read(reinterpret_cast<char*>(&out.version), sizeof(out.version));
    in.read(reinterpret_cast<char*>(&out.noteCount), sizeof(out.noteCount));
    out.version = 1;
    return static_cast<bool>(in);
  }

  if (magic[0] == 'S' && magic[1] == 'M' && magic[2] == 'P' && magic[3] == '2') {
    FileV2 f{};
    memcpy(f.magic, magic, 4);
    in.read(reinterpret_cast<char*>(&f.version), sizeof(FileV2) - 4);
    if (!in) return false;
    out.version = f.version;
    out.noteCount = f.noteCount;
    out.listX = f.listX;
    out.listY = f.listY;
    out.listW = f.listW;
    out.listH = f.listH;
    out.hasListPlacement = f.hasList != 0;
    return true;
  }
  return false;
}

bool PublicIndexStore::Save(const PublicIndex& idx) const {
  std::error_code ec;
  std::filesystem::create_directories(dataDir_, ec);
  auto tmp = dataDir_ / L"public.idx.tmp";
  FileV2 f{};
  f.magic[0] = 'S'; f.magic[1] = 'M'; f.magic[2] = 'P'; f.magic[3] = '2';
  f.version = 2;
  f.noteCount = idx.noteCount;
  f.listX = idx.listX;
  f.listY = idx.listY;
  f.listW = idx.listW;
  f.listH = idx.listH;
  f.hasList = idx.hasListPlacement ? 1 : 0;
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(&f), sizeof(f));
    if (!out) return false;
  }
  std::filesystem::rename(tmp, Path(), ec);
  if (ec) {
    std::filesystem::remove(Path(), ec);
    std::filesystem::rename(tmp, Path(), ec);
  }
  return !ec || std::filesystem::exists(Path());
}

}  // namespace securememo
