#include "core/SearchEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "core/Config.h"
#include "core/Str.h"

namespace cs {

namespace {
int64_t Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}
constexpr size_t kMaxQueryChoices = 4000;
}  // namespace

SearchEngine::SearchEngine(std::wstring usagePath) : usagePath_(std::move(usagePath)) { Load(); }

SearchEngine::~SearchEngine() = default;

void SearchEngine::Add(std::unique_ptr<IProvider> p) { providers_.push_back(std::move(p)); }

void SearchEngine::InitAll(const Config& cfg, IHost& host) {
  maxResults_ = cfg.maxResults > 0 ? cfg.maxResults : 30;
  for (auto& p : providers_) p->Init(cfg, host);
}

void SearchEngine::ShutdownAll() {
  for (auto& p : providers_) p->Shutdown();
}

float SearchEngine::UsageBoost(const std::wstring& q, const std::wstring& key) const {
  float boost = 0.f;
  if (auto it = keyUsage_.find(key); it != keyUsage_.end()) {
    const double days = double(Now() - it->second.lastUsed) / 86400.0;
    const float freq = std::min(0.12f, 0.03f * std::log2(1.f + float(it->second.count)));
    const float recency = days < 1 ? 0.04f : days < 7 ? 0.02f : 0.f;
    boost += freq + recency;
  }
  // Learned "this query -> this item" (also for any longer query starting with a learned prefix).
  for (size_t len = q.size(); len >= 1; --len) {
    auto it = queryChoice_.find(q.substr(0, len));
    if (it != queryChoice_.end()) {
      if (it->second == key) boost += len == q.size() ? 0.35f : 0.2f;
      break;
    }
  }
  return boost;
}

const std::vector<Result>& SearchEngine::Query(std::wstring_view text) {
  results_.clear();
  std::wstring_view q = str::Trim(text);
  lastQuery_.assign(q);
  if (q.empty()) return results_;

  for (size_t i = 0; i < providers_.size(); ++i) {
    size_t before = results_.size();
    providers_[i]->Search(q, results_);
    for (size_t k = before; k < results_.size(); ++k) results_[k].provider = int(i);
  }

  const std::wstring ql = str::ToLower(q);
  for (auto& r : results_)
    if (!r.key.empty()) r.score += UsageBoost(ql, r.key);

  std::stable_sort(results_.begin(), results_.end(),
                   [](const Result& a, const Result& b) { return a.score > b.score; });

  // Dedupe by key (keep highest), cap.
  std::unordered_set<std::wstring> seen;
  size_t w = 0;
  for (size_t r = 0; r < results_.size() && w < size_t(maxResults_); ++r) {
    if (!results_[r].key.empty() && !seen.insert(results_[r].key).second) continue;
    if (w != r) results_[w] = std::move(results_[r]);
    ++w;
  }
  results_.resize(w);
  return results_;
}

ExecResult SearchEngine::Execute(size_t index, Action a) {
  if (index >= results_.size()) return ExecResult{false, {}, {}};
  const Result& r = results_[index];
  if (r.provider < 0 || size_t(r.provider) >= providers_.size()) return ExecResult{false, {}, {}};
  ExecResult er = providers_[size_t(r.provider)]->Execute(r, a);
  if (!r.key.empty() && (a == Action::Open || a == Action::RunAsAdmin)) {
    RecordUse(str::ToLower(lastQuery_), r.key);
    Save();
  }
  return er;
}

void SearchEngine::RecordUse(const std::wstring& q, const std::wstring& key) {
  auto& u = keyUsage_[key];
  ++u.count;
  u.lastUsed = Now();
  if (!q.empty() && q.size() <= 64) {
    if (queryChoice_.size() >= kMaxQueryChoices) queryChoice_.clear();
    queryChoice_[q] = key;
  }
}

// File format (UTF-8, tab separated):
//   K\t<count>\t<lastUsed>\t<key>
//   Q\t<query>\t<key>
void SearchEngine::Load() {
  if (usagePath_.empty()) return;
  std::ifstream f(std::filesystem::path(usagePath_), std::ios::binary);
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::wstring w = str::Utf8ToWide(line);
    auto parts = str::Split(w, L'\t', false);
    if (parts.size() == 4 && parts[0] == L"K") {
      Usage u;
      u.count = uint32_t(std::wcstoul(std::wstring(parts[1]).c_str(), nullptr, 10));
      u.lastUsed = std::wcstoll(std::wstring(parts[2]).c_str(), nullptr, 10);
      keyUsage_[std::wstring(parts[3])] = u;
    } else if (parts.size() == 3 && parts[0] == L"Q") {
      queryChoice_[std::wstring(parts[1])] = std::wstring(parts[2]);
    }
  }
}

void SearchEngine::Save() const {
  if (usagePath_.empty()) return;
  std::error_code ec;
  std::filesystem::path p(usagePath_);
  std::filesystem::create_directories(p.parent_path(), ec);
  std::filesystem::path tmp = p;
  tmp += L".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return;
    for (auto& [k, u] : keyUsage_)
      f << "K\t" << u.count << '\t' << u.lastUsed << '\t' << str::WideToUtf8(k) << '\n';
    for (auto& [q, k] : queryChoice_) f << "Q\t" << str::WideToUtf8(q) << '\t' << str::WideToUtf8(k) << '\n';
  }
  std::filesystem::rename(tmp, p, ec);
}

}  // namespace cs
