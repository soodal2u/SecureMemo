#include "model/Note.h"

#include "crypto/Crypto.h"

#include <rpc.h>
#include <windows.h>

#include <cstdio>
#include <sstream>

namespace securememo {
namespace {

std::string Escape(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (unsigned char c : s) {
    if (c == '\\') o += "\\\\";
    else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else if (c == '|') o += "\\|";
    else o.push_back(static_cast<char>(c));
  }
  return o;
}

std::string Unescape(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[++i];
      if (n == 'n') o.push_back('\n');
      else if (n == 'r') o.push_back('\r');
      else if (n == '\\') o.push_back('\\');
      else if (n == '|') o.push_back('|');
      else o.push_back(n);
    } else {
      o.push_back(s[i]);
    }
  }
  return o;
}

}  // namespace

std::wstring NewNoteId() {
  UUID uuid{};
  if (UuidCreate(&uuid) != RPC_S_OK) {
    // fallback: random hex
    uint8_t b[16];
    Crypto::RandomBytes(b, sizeof(b));
    wchar_t buf[40];
    swprintf(buf, 40, L"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return buf;
  }
  RPC_WSTR str = nullptr;
  if (UuidToStringW(&uuid, &str) != RPC_S_OK) {
    return L"note";
  }
  std::wstring out(reinterpret_cast<wchar_t*>(str));
  RpcStringFreeW(&str);
  return out;
}

std::wstring NowTimestamp() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  wchar_t buf[64];
  swprintf(buf, 64, L"%04d-%02d-%02dT%02d:%02d:%02d",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  return buf;
}

std::wstring PlainFromRtf(const std::wstring& rtf) {
  if (rtf.empty()) return {};
  if (rtf.rfind(L"{\\rtf", 0) != 0 && rtf.rfind(L"{\\rtf", 0) != 0) {
    // not RTF
    if (rtf.size() >= 5 && (rtf[0] == L'{' && rtf.find(L"\\rtf") != std::wstring::npos)) {
      // fall through
    } else {
      return rtf;
    }
  }

  std::wstring out;
  out.reserve(rtf.size() / 4);
  const size_t n = rtf.size();
  int groupDepth = 0;
  bool inDestination = false;  // {\*\... destinations — skip
  int destDepth = -1;

  for (size_t i = 0; i < n; ++i) {
    wchar_t c = rtf[i];
    if (c == L'{') {
      ++groupDepth;
      // peek for {\* 
      if (i + 2 < n && rtf[i + 1] == L'\\' && rtf[i + 2] == L'*') {
        inDestination = true;
        destDepth = groupDepth;
      }
      continue;
    }
    if (c == L'}') {
      if (inDestination && groupDepth == destDepth) {
        inDestination = false;
        destDepth = -1;
      }
      if (groupDepth > 0) --groupDepth;
      continue;
    }
    if (inDestination) continue;

    if (c == L'\\') {
      if (i + 1 >= n) break;
      wchar_t n1 = rtf[++i];
      // \'hh hex char
      if (n1 == L'\'') {
        if (i + 2 < n) {
          auto hex = [](wchar_t h) -> int {
            if (h >= L'0' && h <= L'9') return h - L'0';
            if (h >= L'a' && h <= L'f') return h - L'a' + 10;
            if (h >= L'A' && h <= L'F') return h - L'A' + 10;
            return -1;
          };
          int hi = hex(rtf[i + 1]);
          int lo = hex(rtf[i + 2]);
          if (hi >= 0 && lo >= 0) {
            // ANSI byte — map roughly as Latin-1
            out.push_back(static_cast<wchar_t>((hi << 4) | lo));
            i += 2;
          }
        }
        continue;
      }
      // \uN? unicode
      if (n1 == L'u') {
        size_t j = i + 1;
        bool neg = false;
        if (j < n && rtf[j] == L'-') { neg = true; ++j; }
        long val = 0;
        bool any = false;
        while (j < n && rtf[j] >= L'0' && rtf[j] <= L'9') {
          any = true;
          val = val * 10 + (rtf[j] - L'0');
          ++j;
        }
        if (any) {
          if (neg) val = -val;
          // signed 16-bit style
          out.push_back(static_cast<wchar_t>(static_cast<unsigned short>(val)));
          i = j - 1;
          if (i + 1 < n && rtf[i + 1] == L'?') ++i;  // skip optional fallback
        }
        continue;
      }
      // escaped special
      if (n1 == L'\\' || n1 == L'{' || n1 == L'}') {
        out.push_back(n1);
        continue;
      }
      // paragraph / line
      if (n1 == L'p' && i + 3 < n && rtf.substr(i, 4) == L"par ") {
        out.push_back(L'\n');
        i += 3;
        continue;
      }
      if (n1 == L'p' && i + 2 < n && rtf.substr(i, 3) == L"par") {
        out.push_back(L'\n');
        i += 2;
        // skip space after control word
        if (i + 1 < n && rtf[i + 1] == L' ') ++i;
        continue;
      }
      if (n1 == L'l' && i + 4 < n && rtf.substr(i, 4) == L"line") {
        out.push_back(L'\n');
        i += 3;
        if (i + 1 < n && rtf[i + 1] == L' ') ++i;
        continue;
      }
      if (n1 == L't' && i + 3 < n && rtf.substr(i, 3) == L"tab") {
        out.push_back(L' ');
        i += 2;
        if (i + 1 < n && rtf[i + 1] == L' ') ++i;
        continue;
      }
      // skip control word: letters then optional digits then optional space
      size_t j = i;
      while (j < n && ((rtf[j] >= L'a' && rtf[j] <= L'z') ||
                       (rtf[j] >= L'A' && rtf[j] <= L'Z'))) {
        ++j;
      }
      if (j < n && rtf[j] == L'-') ++j;
      while (j < n && rtf[j] >= L'0' && rtf[j] <= L'9') ++j;
      if (j < n && rtf[j] == L' ') ++j;
      i = j - 1;
      continue;
    }

    // skip raw newlines in RTF source
    if (c == L'\r' || c == L'\n') continue;
    out.push_back(c);
  }

  // collapse whitespace
  std::wstring cleaned;
  cleaned.reserve(out.size());
  bool space = false;
  for (wchar_t c : out) {
    if (c == L'\t') c = L' ';
    if (c == L' ' || c == L'\n') {
      if (!space && !cleaned.empty()) {
        cleaned.push_back(c == L'\n' ? L' ' : L' ');
        space = true;
      }
      continue;
    }
    space = false;
    cleaned.push_back(c);
  }
  while (!cleaned.empty() && cleaned.front() == L' ') cleaned.erase(cleaned.begin());
  while (!cleaned.empty() && cleaned.back() == L' ') cleaned.pop_back();
  return cleaned;
}

std::string SerializeVault(const VaultData& data) {
  std::ostringstream oss;
  oss << "SM1\n";
  oss << "version=" << data.version << "\n";
  oss << "auto_lock_minutes=" << data.settings.autoLockMinutes << "\n";
  for (const auto& n : data.notes) {
    oss << "NOTE\n";
    oss << "id=" << Escape(WideToUtf8(n.id)) << "\n";
    oss << "color=" << Escape(WideToUtf8(n.color)) << "\n";
    oss << "x=" << n.x << "\n";
    oss << "y=" << n.y << "\n";
    oss << "w=" << n.width << "\n";
    oss << "h=" << n.height << "\n";
    oss << "top=" << (n.alwaysOnTop ? 1 : 0) << "\n";
    oss << "visible=" << (n.visible ? 1 : 0) << "\n";
    oss << "updated=" << Escape(WideToUtf8(n.updatedAt)) << "\n";
    oss << "content=" << Escape(WideToUtf8(n.content)) << "\n";
    if (!n.rtf.empty()) {
      oss << "rtf=" << Escape(WideToUtf8(n.rtf)) << "\n";
    }
    oss << "ENDNOTE\n";
  }
  oss << "END\n";
  return oss.str();
}

bool DeserializeVault(const std::string& payload, VaultData& out) {
  out = VaultData{};
  std::istringstream iss(payload);
  std::string line;
  if (!std::getline(iss, line)) return false;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line != "SM1") return false;

  Note current;
  bool inNote = false;

  auto parseKv = [](const std::string& l, std::string& k, std::string& v) -> bool {
    auto p = l.find('=');
    if (p == std::string::npos) return false;
    k = l.substr(0, p);
    v = l.substr(p + 1);
    return true;
  };

  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == "END") break;
    if (line == "NOTE") {
      current = Note{};
      inNote = true;
      continue;
    }
    if (line == "ENDNOTE") {
      if (inNote) {
        // Migrate old vaults that stored raw RTF in content=
        if (current.rtf.empty() && current.content.size() >= 5 &&
            current.content.rfind(L"{\\rtf", 0) == 0) {
          current.rtf = current.content;
          current.content = PlainFromRtf(current.rtf);
        }
        out.notes.push_back(current);
        inNote = false;
      }
      continue;
    }

    std::string k, v;
    if (!parseKv(line, k, v)) continue;

    if (!inNote) {
      if (k == "version") out.version = std::stoi(v);
      else if (k == "auto_lock_minutes") out.settings.autoLockMinutes = std::stoi(v);
      continue;
    }

    if (k == "id") current.id = Utf8ToWide(Unescape(v));
    else if (k == "color") current.color = Utf8ToWide(Unescape(v));
    else if (k == "x") current.x = std::stoi(v);
    else if (k == "y") current.y = std::stoi(v);
    else if (k == "w") current.width = std::stoi(v);
    else if (k == "h") current.height = std::stoi(v);
    else if (k == "top") current.alwaysOnTop = (v != "0");
    else if (k == "visible") current.visible = (v != "0");
    else if (k == "updated") current.updatedAt = Utf8ToWide(Unescape(v));
    else if (k == "content") current.content = Utf8ToWide(Unescape(v));
    else if (k == "rtf") current.rtf = Utf8ToWide(Unescape(v));
  }
  return true;
}

}  // namespace securememo
