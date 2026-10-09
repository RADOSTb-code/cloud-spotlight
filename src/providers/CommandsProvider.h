#pragma once
// Quick commands: shutdown/restart timers (shutdown.exe), sleep/hibernate/lock/sign-out/monitor off (optionally
// delayed), reminders ("напомни через 10 минут …"), recycle bin, folders ("открыть папку проектов"),
// user commands from cfg.commands, GUID / password / local IP. Parsing lives in logic/CommandParse.
// Delayed actions and reminders run on one lazily started timer thread, stopped in Shutdown().
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/Config.h"
#include "core/Types.h"
#include "logic/CommandParse.h"

namespace cs {

class CommandsProvider final : public IProvider {
 public:
  CommandsProvider() = default;
  ~CommandsProvider() override;
  const wchar_t* Id() const override { return L"commands"; }
  void Init(const Config& cfg, IHost& host) override;
  void Search(std::wstring_view query, std::vector<Result>& out) override;
  ExecResult Execute(const Result& r, Action a) override;
  void Shutdown() override;

 private:
  struct Folder {
    std::wstring title;
    std::wstring path;   // filesystem path or "shell:..." parsing name
    std::vector<std::wstring> names;
    bool shell = false;  // not a filesystem path
  };
  struct Pending {
    uint64_t id;
    std::chrono::steady_clock::time_point due;
    cmd::Kind kind;
    std::wstring label;
    std::wstring at;      // "15:42" for display
    int64_t seconds;      // original delay
    bool silent;          // immediate action deferred a moment (no notification)
  };

  void AddCommand(const cmd::Command& c, float score, std::vector<Result>& out);
  void AddFolders(std::wstring_view query, std::vector<Result>& out);
  void AddCustom(std::wstring_view query, std::vector<Result>& out);
  ExecResult RunCommand(cmd::Kind kind, int64_t delay, const std::wstring& label);
  void Schedule(cmd::Kind kind, int64_t delayMs, std::wstring label, bool silent);
  size_t CancelTimers();
  size_t ActiveTimers(std::wstring* nextAt);
  void TimerThread();
  static void Perform(cmd::Kind kind);
  std::wstring Password(int length);
  std::wstring LocalIps();

  IHost* host_ = nullptr;
  std::vector<Folder> folders_;
  std::vector<CustomCommand> custom_;
  std::vector<std::wstring> customIcons_;  // FilePath icon per custom command (empty = glyph)

  // Generated values stay stable while the query is unchanged (Search runs on every refresh).
  std::wstring genQuery_, genValue_;
  std::wstring ipCache_;
  uint64_t ipCacheTick_ = 0;
  int64_t shutdownAtUnix_ = 0;  // last shutdown/restart scheduled by us (for display)
  std::wstring shutdownAt_;

  std::mutex tmu_;
  std::condition_variable tcv_;
  std::vector<Pending> timers_;
  uint64_t nextId_ = 1;
  bool tstop_ = false;
  std::thread tthread_;
};

}  // namespace cs
