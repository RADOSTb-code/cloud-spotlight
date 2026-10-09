#pragma once
// Windows Settings pages (ms-settings:) and classic system tools. Thin wrapper over logic/SettingsCatalog.
#include <vector>

#include "core/Types.h"
#include "logic/SettingsCatalog.h"

namespace cs {

class SettingsProvider final : public IProvider {
 public:
  const wchar_t* Id() const override { return L"settings"; }
  void Init(const Config& cfg, IHost& host) override;
  void Search(std::wstring_view query, std::vector<Result>& out) override;
  ExecResult Execute(const Result& r, Action a) override;

 private:
  IHost* host_ = nullptr;
  std::vector<settings::Match> matches_;  // scratch, reused across keystrokes
};

}  // namespace cs
