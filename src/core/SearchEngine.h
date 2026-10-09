#pragma once
// Runs all providers synchronously for a query, merges and ranks results, learns from what the user picks.
// Platform-independent. Lives on the UI thread.
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Types.h"

namespace cs {

class SearchEngine {
 public:
  // usagePath: UTF-8-encoded text file where selection history is persisted (may not exist yet).
  explicit SearchEngine(std::wstring usagePath);
  ~SearchEngine();

  void Add(std::unique_ptr<IProvider> p);
  void InitAll(const Config& cfg, IHost& host);
  void ShutdownAll();

  // Runs every provider, returns results sorted by final score (desc), deduped by key, capped to maxResults.
  // Empty/whitespace query -> empty list. Reference stays valid until the next Query() call.
  const std::vector<Result>& Query(std::wstring_view text);
  const std::vector<Result>& Last() const { return results_; }
  const std::wstring& LastQuery() const { return lastQuery_; }

  // Executes results_[index] with its provider and records the choice. Index out of range -> {hide=false}.
  ExecResult Execute(size_t index, Action a);

  void SetMaxResults(int n) { maxResults_ = n > 0 ? n : 30; }

  // Exposed for tests: boost added to a result with `key` for lowercased `query`.
  float UsageBoost(const std::wstring& queryLower, const std::wstring& key) const;
  void RecordUse(const std::wstring& queryLower, const std::wstring& key);

 private:
  void Load();
  void Save() const;

  struct Usage {
    uint32_t count = 0;
    int64_t lastUsed = 0;  // unix seconds
  };

  std::vector<std::unique_ptr<IProvider>> providers_;
  std::vector<Result> results_;
  std::wstring lastQuery_;
  std::wstring usagePath_;
  int maxResults_ = 30;
  std::unordered_map<std::wstring, Usage> keyUsage_;                 // key -> global usage
  std::unordered_map<std::wstring, std::wstring> queryChoice_;       // query prefix (lower) -> chosen key
};

}  // namespace cs
