#pragma once
// Files & folders: background index of cfg.fileRoots (logic/FileIndex) + synchronous path browsing ("C:\Us", "~\").
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/Types.h"
#include "logic/FileIndex.h"

namespace cs {

class FilesProvider final : public IProvider {
 public:
  FilesProvider();
  ~FilesProvider() override;
  const wchar_t* Id() const override { return L"files"; }
  void Init(const Config& cfg, IHost& host) override;
  void Search(std::wstring_view query, std::vector<Result>& out) override;
  ExecResult Execute(const Result& r, Action a) override;
  void Shutdown() override;

 private:
  struct DirChild {
    std::wstring name, lower;
    bool dir;
    bool hidden;
  };

  void Run();
  std::shared_ptr<const FileIndex> BuildIndex();
  bool IsExcluded(std::wstring_view name) const;
  void SearchInDirectory(std::wstring_view query, std::vector<Result>& out);
  std::wstring Pretty(std::wstring_view path) const;  // %USERPROFILE% -> "~"
  Result MakeResult(std::wstring fullPath, std::wstring_view name, std::wstring subtitle, bool dir, float score) const;

  IHost* host_ = nullptr;
  std::vector<std::wstring> roots_;
  std::vector<std::wstring> exclude_;  // lowercased dir/file names
  int maxDepth_ = 8;
  int reindexMinutes_ = 15;
  size_t maxEntries_ = 400000;
  std::wstring profile_;

  std::mutex mu_;  // guards index_ only (pointer copy)
  std::shared_ptr<const FileIndex> index_;
  std::atomic<bool> stop_{false};
  void* stopEvent_ = nullptr;  // HANDLE
  std::thread thread_;

  // UI-thread scratch / caches
  std::vector<FileIndex::Hit> hitFiles_, hitDirs_;
  std::wstring listedDir_;  // path-mode cache: last enumerated directory
  unsigned long long listedAt_ = 0;
  std::vector<DirChild> listed_;
};

}  // namespace cs
