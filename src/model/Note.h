#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace securememo {

struct Note {
  std::wstring id;
  // Plain text for list preview / search (never RTF control words)
  std::wstring content;
  // RichEdit RTF body (optional; if empty, content is loaded as plain text)
  std::wstring rtf;
  std::wstring color = L"yellow";  // yellow, pink, blue, green, purple
  int x = -1;   // -1 => place with DefaultNotePlacement on create
  int y = -1;
  int width = 320;
  int height = 320;
  bool alwaysOnTop = true;
  std::wstring updatedAt;  // ISO-ish local string
  bool visible = true;
};

struct AppSettings {
  int autoLockMinutes = 5;
};

struct VaultData {
  int version = 1;
  AppSettings settings;
  std::vector<Note> notes;
};

std::wstring NewNoteId();
std::wstring NowTimestamp();

// Extract readable plain text from RTF (for list preview / migration)
std::wstring PlainFromRtf(const std::wstring& rtf);

// Simple custom serialization (not full JSON) — UTF-8 text payload
std::string SerializeVault(const VaultData& data);
bool DeserializeVault(const std::string& payload, VaultData& out);

}  // namespace securememo
