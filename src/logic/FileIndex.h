#pragma once
// Compact, portable file-name index (no Windows headers; the Win32 walker lives in FilesProvider).
// Layout: two contiguous wchar_t pools (original names, lowercased names — same offsets) and 16-byte POD entries
// that link to their parent directory, so full paths are rebuilt only for the few results shown.
// Immutable after Finish(): build off-thread, then publish via shared_ptr<const FileIndex>.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Fuzzy.h"

namespace cs {

class FileIndex {
 public:
  static constexpr uint32_t kRootBit = 0x80000000u;  // parent ref pointing at roots_[ref & ~kRootBit]
  static constexpr uint32_t kInvalid = 0xFFFFFFFFu;
  static constexpr int kMaxDepth = 63;
  static constexpr size_t kMaxHits = 32;

  struct Entry {
    uint32_t nameOff;  // offset into names_/lower_
    uint32_t parent;   // entry index of the parent dir, or kRootBit | root index
    uint32_t mask;     // CharMask() of the lowercased name: cheap "can it match at all" prefilter
    uint16_t days;     // last write time, days since 2000-01-01 (0 = unknown)
    uint8_t nameLen;   // NTFS names are <= 255 UTF-16 units
    uint8_t flags;     // bit0: directory, bits 2..7: depth (1 = direct child of a root)
  };
  static_assert(sizeof(Entry) == 16);

  struct Hit {
    uint32_t index;
    float score;  // raw fuzzy score plus tiny depth/recency bonuses, <= 1
  };

  struct SearchOptions {
    size_t maxFiles = 8;
    size_t maxDirs = 4;
    uint16_t today = 0;      // DaysSince2000 of "now", enables the recency bonus
    float minScore = 0.38f;  // raw fuzzy threshold (drops loose subsequence matches)
  };

  explicit FileIndex(wchar_t sep = L'\\') : sep_(sep) {}

  // ---- building (single thread)
  uint32_t AddRoot(std::wstring_view path);
  // Returns a parent ref for children of this entry (entry index), or kInvalid if rejected (empty/too long).
  uint32_t Add(uint32_t parentRef, std::wstring_view name, bool dir, uint16_t days = 0);
  void Reserve(size_t entries, size_t chars);
  void Finish();  // releases slack capacity

  // ---- queries (thread-safe on a const index)
  size_t Size() const { return entries_.size(); }
  size_t MemoryBytes() const;
  // Appends hits sorted by score desc. Allocation-free except for growing `files`/`dirs`.
  void Search(const fuzzy::Query& q, const SearchOptions& opt, std::vector<Hit>& files,
              std::vector<Hit>& dirs) const;

  std::wstring_view Name(uint32_t i) const { return {names_.data() + entries_[i].nameOff, entries_[i].nameLen}; }
  bool IsDir(uint32_t i) const { return (entries_[i].flags & 1) != 0; }
  int Depth(uint32_t i) const { return entries_[i].flags >> 2; }
  uint16_t Days(uint32_t i) const { return entries_[i].days; }
  std::wstring FullPath(uint32_t i) const;
  std::wstring ParentPath(uint32_t i) const;
  // Content fingerprint (names + structure), to detect "nothing changed" after a reindex.
  uint64_t Fingerprint() const;

  static uint32_t CharMask(std::wstring_view lower);
  static uint16_t DaysFromFileTime(uint64_t fileTime100ns);  // Windows FILETIME (since 1601) -> days since 2000

 private:
  std::wstring PathOfRef(uint32_t ref) const;

  wchar_t sep_;
  std::vector<std::wstring> roots_;
  std::vector<Entry> entries_;
  std::vector<wchar_t> names_;
  std::vector<wchar_t> lower_;
};

namespace pathq {
// "C:\", "c:/x", "\\server\share", "%appdata%\..", "~\.." (also bare "~" / "%x%").
bool LooksLikePath(std::wstring_view q);
// Splits at the last '\' or '/': {"C:\Users\", "Doc"}. No separator -> {"", s}.
std::pair<std::wstring_view, std::wstring_view> SplitDirAndPrefix(std::wstring_view s);
}  // namespace pathq

}  // namespace cs
