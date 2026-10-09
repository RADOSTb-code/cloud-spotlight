#pragma once
// Start-menu applications (Win32 + UWP/MSIX) from the shell's AppsFolder, enumerated on a background thread and
// refreshed when the Start Menu\Programs folders change (debounced) or every few minutes.
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/Types.h"

namespace cs {

class AppsProvider final : public IProvider {
 public:
  struct App {
    std::wstring title, titleLower;
    std::wstring parsingName;            // AUMID or "{KNOWNFOLDERID}\path\app.exe"; "shell:AppsFolder\" + this opens it
    std::wstring target;                 // filesystem target if known (Win32 apps), else empty
    std::vector<std::wstring> keywords;  // lowercase: exe stem, ru/en aliases
    bool uwp = false;
  };
  using AppList = std::vector<App>;

  AppsProvider();
  ~AppsProvider() override;
  const wchar_t* Id() const override { return L"apps"; }
  void Init(const Config& cfg, IHost& host) override;
  void Search(std::wstring_view query, std::vector<Result>& out) override;
  ExecResult Execute(const Result& r, Action a) override;
  void Shutdown() override;

 private:
  void Run();
  std::shared_ptr<AppList> Scan();  // null if stopped or the shell call failed

  IHost* host_ = nullptr;
  std::mutex mu_;  // guards apps_ only (pointer copy)
  std::shared_ptr<const AppList> apps_;
  std::atomic<bool> stop_{false};
  void* stopEvent_ = nullptr;  // HANDLE
  std::thread thread_;

  struct Cand {
    uint32_t index;
    float score;
  };
  std::vector<Cand> cands_;  // UI-thread scratch
};

}  // namespace cs
