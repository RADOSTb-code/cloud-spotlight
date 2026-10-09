#include "app/Autostart.h"

#include <string>

#include "core/Str.h"
#include "platform/Win.h"

namespace cs::autostart {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kApprovedKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t kValueName[] = L"CloudSpotlight";

std::wstring Command() { return L"\"" + win::ExePath() + L"\" --background"; }

bool ReadRunValue(std::wstring& out) {
  DWORD bytes = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                   nullptr, nullptr, &bytes) != ERROR_SUCCESS)
    return false;
  std::wstring buf(bytes / sizeof(wchar_t) + 1, L'\0');
  bytes = DWORD(buf.size() * sizeof(wchar_t));
  if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                   nullptr, buf.data(), &bytes) != ERROR_SUCCESS)
    return false;
  buf.resize(wcsnlen(buf.c_str(), buf.size()));
  out = std::move(buf);
  return true;
}

bool WriteRunValue() {
  std::wstring cmd = Command();
  return RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName, REG_SZ, cmd.c_str(),
                         DWORD((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

// Task Manager stores a 12-byte REG_BINARY: first byte 0x02/0x06 = enabled, odd values (0x03/0x07) = disabled.
bool DisabledInTaskManager() {
  BYTE data[16] = {};
  DWORD bytes = sizeof(data);
  if (RegGetValueW(HKEY_CURRENT_USER, kApprovedKey, kValueName, RRF_RT_REG_BINARY, nullptr, data, &bytes) !=
          ERROR_SUCCESS ||
      bytes == 0)
    return false;
  return (data[0] & 1) != 0;
}

void ClearApproval() { RegDeleteKeyValueW(HKEY_CURRENT_USER, kApprovedKey, kValueName); }

}  // namespace

bool IsEnabled() {
  std::wstring v;
  return ReadRunValue(v) && !DisabledInTaskManager();
}

bool Enable() {
  ClearApproval();  // absent StartupApproved entry == enabled
  return WriteRunValue();
}

bool Disable() {
  LSTATUS s = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValueName);
  ClearApproval();
  return s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND;
}

void Sync(bool wanted) {
  std::wstring current;
  bool present = ReadRunValue(current);
  if (wanted) {
    if (!present || !str::IEquals(current, Command())) WriteRunValue();
  } else if (present) {
    Disable();
  }
}

}  // namespace cs::autostart
