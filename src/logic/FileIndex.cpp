#include "logic/FileIndex.h"

#include <algorithm>
#include <array>

#include "core/Str.h"

namespace cs {
namespace {

// Bit per physical key: Cyrillic letters share the bit of the Latin letter on the same key (ЙЦУКЕН), so a query and
// its wrong-layout twin produce the same mask and one check covers both. Superset filter: never rejects a match.
constexpr char kRuKey[32] = {'f', ',', 'd', 'u', 'l', 't', ';', 'p', 'b', 'q', 'r', 'k', 'v', 'y', 'j', 'g',
                             'h', 'c', 'n', 'e', 'a', '[', 'w', 'x', 'i', 'o', ']', 's', 'm', '\'', '.', 'z'};

inline uint32_t KeyBit(unsigned k) {
  if (k >= 'a' && k <= 'z') return 1u << (k - 'a');
  if (k >= '0' && k <= '9') return 1u << 26;
  if (k == ' ') return 0;  // the fuzzy subsequence step ignores query spaces
  if (k == '.' || k == ',') return 1u << 27;
  return 1u << (28 + k % 4);
}

inline uint32_t CharBit(wchar_t c) {
  if (c >= 0x430 && c <= 0x44F) return KeyBit(unsigned(kRuKey[c - 0x430]));
  return KeyBit(unsigned(c));
}

// Fixed-capacity min-heap of the best `cap` hits.
struct TopK {
  std::array<FileIndex::Hit, FileIndex::kMaxHits> h;
  size_t n = 0, cap = 0;
  static bool Greater(const FileIndex::Hit& a, const FileIndex::Hit& b) { return a.score > b.score; }
  float Floor() const { return n < cap ? -1.f : h[0].score; }
  void Push(FileIndex::Hit x) {
    if (n < cap) {
      h[n++] = x;
      std::push_heap(h.begin(), h.begin() + std::ptrdiff_t(n), Greater);
    } else if (cap && x.score > h[0].score) {
      std::pop_heap(h.begin(), h.begin() + std::ptrdiff_t(n), Greater);
      h[n - 1] = x;
      std::push_heap(h.begin(), h.begin() + std::ptrdiff_t(n), Greater);
    }
  }
  void Drain(std::vector<FileIndex::Hit>& out) {
    std::sort(h.begin(), h.begin() + std::ptrdiff_t(n), Greater);
    out.insert(out.end(), h.begin(), h.begin() + std::ptrdiff_t(n));
  }
};

}  // namespace

uint32_t FileIndex::CharMask(std::wstring_view lower) {
  uint32_t m = 0;
  for (wchar_t c : lower) m |= CharBit(c);
  return m;
}

uint16_t FileIndex::DaysFromFileTime(uint64_t ft) {
  constexpr uint64_t kPerDay = 864000000000ull;  // 100 ns ticks
  constexpr uint64_t k1601To2000 = 145731;       // days
  uint64_t d = ft / kPerDay;
  if (d <= k1601To2000) return 0;
  d -= k1601To2000;
  return d > 0xFFFF ? 0xFFFF : uint16_t(d);
}

uint32_t FileIndex::AddRoot(std::wstring_view path) {
  roots_.emplace_back(path);
  return kRootBit | uint32_t(roots_.size() - 1);
}

void FileIndex::Reserve(size_t entries, size_t chars) {
  entries_.reserve(entries);
  names_.reserve(chars);
  lower_.reserve(chars);
}

uint32_t FileIndex::Add(uint32_t parentRef, std::wstring_view name, bool dir, uint16_t days) {
  if (name.empty() || name.size() > 255 || entries_.size() >= kRootBit - 1) return kInvalid;
  int depth = 1;
  if (!(parentRef & kRootBit)) depth = std::min(kMaxDepth, (entries_[parentRef].flags >> 2) + 1);
  Entry e;
  e.nameOff = uint32_t(names_.size());
  e.parent = parentRef;
  e.days = days;
  e.nameLen = uint8_t(name.size());
  e.flags = uint8_t((dir ? 1 : 0) | (depth << 2));
  names_.insert(names_.end(), name.begin(), name.end());
  uint32_t mask = 0;
  for (wchar_t c : name) {
    wchar_t l = str::LowerChar(c);
    lower_.push_back(l);
    mask |= CharBit(l);
  }
  e.mask = mask;
  entries_.push_back(e);
  return uint32_t(entries_.size() - 1);
}

void FileIndex::Finish() {
  entries_.shrink_to_fit();
  names_.shrink_to_fit();
  lower_.shrink_to_fit();
}

size_t FileIndex::MemoryBytes() const {
  size_t b = entries_.capacity() * sizeof(Entry) + (names_.capacity() + lower_.capacity()) * sizeof(wchar_t);
  for (const auto& r : roots_) b += r.capacity() * sizeof(wchar_t);
  return b;
}

uint64_t FileIndex::Fingerprint() const {
  uint64_t h = 1469598103934665603ull;  // FNV-1a
  auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
  for (const auto& r : roots_)
    for (wchar_t c : r) mix(uint64_t(c));
  for (wchar_t c : names_) mix(uint64_t(c));
  for (const Entry& e : entries_) mix((uint64_t(e.parent) << 16) ^ e.flags ^ (uint64_t(e.days) << 48));
  return h;
}

std::wstring FileIndex::PathOfRef(uint32_t ref) const {
  std::vector<uint32_t> chain;  // only built for the handful of results shown
  while (!(ref & kRootBit)) {
    chain.push_back(ref);
    ref = entries_[ref].parent;
  }
  std::wstring p = roots_[ref & ~kRootBit];
  for (size_t i = chain.size(); i-- > 0;) {
    if (!p.empty() && p.back() != sep_ && p.back() != L'/') p += sep_;
    p += Name(chain[i]);
  }
  return p;
}

std::wstring FileIndex::FullPath(uint32_t i) const { return PathOfRef(i); }
std::wstring FileIndex::ParentPath(uint32_t i) const { return PathOfRef(entries_[i].parent); }

namespace {

// Bounded scoring. Mirrors the tiers of fuzzy::ScoreOne (core/Fuzzy.cpp): exact 1, prefix <= 0.98,
// word-start <= 0.88, acronym <= 0.75 (needs the first word start to match), plain substring <= 0.7,
// subsequence <= 0.45. A candidate that cannot reach `needRaw` is rejected with a compare/find, and the plain
// subsequence tier (the most common case for loose matches) is computed inline without the full scorer.
// Kept honest by the brute-force comparison in tests/test_fileindex.cpp.
inline bool IsFuzzySep(wchar_t c) {
  return c == L' ' || c == L'-' || c == L'_' || c == L'.' || c == L'\\' || c == L'/' || c == L'(' || c == L')' ||
         c == L'[' || c == L']' || c == L',' || c == L'&' || c == L'+';
}

// fuzzy's IsWordStart, using the lowered copy for the case test (str::IsUpper(c) == (LowerChar(c) != c)).
inline bool IsWordStartFast(std::wstring_view orig, std::wstring_view lo, size_t i) {
  if (i == 0) return true;
  const wchar_t p = orig[i - 1], c = orig[i];
  const bool pSep = IsFuzzySep(p);
  if (pSep && !IsFuzzySep(c)) return true;
  if (c != lo[i] && p == lo[i - 1] && str::IsLetter(p)) return true;  // camelCase
  if (c >= L'0' && c <= L'9' && !(p >= L'0' && p <= L'9') && !pSep) return true;
  return false;
}

// fuzzy's acronym tier: every query char matches consecutive word starts ("vsc" -> Visual Studio Code).
inline bool AcronymMatch(std::wstring_view q, std::wstring_view orig, std::wstring_view lo) {
  size_t qi = 0;
  for (size_t i = 0; i < lo.size() && qi < q.size(); ++i)
    if (IsWordStartFast(orig, lo, i) && !IsFuzzySep(orig[i])) {
      if (lo[i] == q[qi]) ++qi;
      else return false;
    }
  return qi == q.size();
}

// Same formula as the last step of fuzzy::ScoreOne (greedy leftmost subsequence, query spaces ignored).
// Hand-rolled loops: names are short, and libc wmemchr/wmemcmp call overhead dominates otherwise.
inline float SubsequenceScore(std::wstring_view q, std::wstring_view t) {
  size_t ti = 0, first = size_t(-1), gaps = 0, matched = 0;
  const size_t m = t.size();
  for (wchar_t ch : q) {
    if (ch == L' ') continue;
    size_t f = ti;
    while (f < m && t[f] != ch) ++f;
    if (f == m) return 0.f;
    if (first == size_t(-1)) first = f;
    if (f > ti && matched) gaps += f - ti;
    ti = f + 1;
    ++matched;
  }
  if (!matched) return 0.f;
  float s = 0.45f - float(gaps) * 0.015f - float(first < 10 ? first : 10) * 0.01f;
  return s > 0.05f ? s : 0.f;
}

inline bool StartsWithAt(std::wstring_view t, size_t pos, std::wstring_view q) {
  for (size_t k = 0; k < q.size(); ++k)
    if (t[pos + k] != q[k]) return false;
  return true;
}

inline bool Contains(std::wstring_view t, std::wstring_view q) {
  if (q.size() > t.size()) return false;
  const wchar_t c0 = q[0];
  for (size_t i = 0, last = t.size() - q.size(); i <= last; ++i)
    if (t[i] == c0 && StartsWithAt(t, i, q)) return true;
  return false;
}

inline float ScoreVariant(const fuzzy::Query& qv, float weight, float needRaw, std::wstring_view orig,
                          std::wstring_view lo) {
  const float need = needRaw / weight;
  if (need > 1.f) return 0.f;
  const std::wstring_view ql = qv.lower;
  const size_t n = ql.size(), m = lo.size();
  if (n <= m) {
    if (StartsWithAt(lo, 0, ql)) return weight * fuzzy::ScoreLowered(qv, orig, lo);  // exact / prefix
    if (need > 0.88f) return 0.f;
    if (need > 0.75f) return Contains(lo, ql) ? weight * fuzzy::ScoreLowered(qv, orig, lo) : 0.f;
  } else if (need > 0.45f) {
    return 0.f;
  }
  // Every remaining tier implies the (space-free) query is a subsequence: cheapest rejection first.
  const float sub = SubsequenceScore(ql, lo);
  if (sub == 0.f) return 0.f;
  if (n <= m) {
    if (Contains(lo, ql)) return weight * fuzzy::ScoreLowered(qv, orig, lo);
    // no substring: ScoreOne returns 0.75 for an acronym, else falls through to the subsequence tier
    if (n >= 2 && AcronymMatch(ql, orig, lo)) return weight * 0.75f;
  }
  return need > 0.45f ? 0.f : weight * sub;
}

}  // namespace

void FileIndex::Search(const fuzzy::Query& q, const SearchOptions& opt, std::vector<Hit>& files,
                       std::vector<Hit>& dirs) const {
  if (q.lower.empty()) return;
  // Score the two layouts separately so each can be bounded on its own (same 0.92 penalty as fuzzy::ScoreLowered).
  const fuzzy::Query qa{q.lower, {}};
  const fuzzy::Query qb{q.swapped, {}};
  const bool hasSwap = !q.swapped.empty();
  const uint32_t m1 = CharMask(q.lower);
  const uint32_t m2 = hasSwap ? CharMask(q.swapped) : m1;
  // Without a '.' in the query, files are matched by their stem: "readme" == "README.md".
  const bool useStem = q.lower.find(L'.') == std::wstring::npos;
  TopK topFiles, topDirs;
  topFiles.cap = std::min(opt.maxFiles, kMaxHits);
  topDirs.cap = std::min(opt.maxDirs, kMaxHits);

  const Entry* ents = entries_.data();
  const wchar_t* names = names_.data();
  const wchar_t* lower = lower_.data();
  const size_t count = entries_.size();
  for (size_t i = 0; i < count; ++i) {
    const Entry& e = ents[i];
    const bool pass1 = (e.mask & m1) == m1, pass2 = hasSwap && (e.mask & m2) == m2;
    if (!pass1 && !pass2) continue;
    const bool dir = (e.flags & 1) != 0;
    TopK& top = dir ? topDirs : topFiles;
    // final = raw * 0.96 + bonus (bonus <= 0.04)  =>  raw must reach (floor - 0.04) / 0.96
    const float needRaw = std::max(opt.minScore, (top.Floor() - 0.04f) / 0.96f - 1e-4f);
    size_t len = e.nameLen;
    const wchar_t* lo = lower + e.nameOff;
    if (needRaw > 0.88f) {
      // Steady state for short queries: only prefix matches can still enter the top-K. A query without '.' cannot
      // run into the extension, so the stem is not needed for this test.
      const bool p1 = pass1 && len >= qa.lower.size() && StartsWithAt({lo, len}, 0, qa.lower);
      const bool p2 = pass2 && needRaw <= 0.92f && len >= qb.lower.size() && StartsWithAt({lo, len}, 0, qb.lower);
      if (!p1 && !p2) continue;
    }
    if (!dir && useStem) {
      for (size_t k = len; k-- > 1;)
        if (lo[k] == L'.') { len = k; break; }
    }
    const std::wstring_view ov(names + e.nameOff, len), lv(lo, len);
    float s = pass1 ? ScoreVariant(qa, 1.f, needRaw, ov, lv) : 0.f;
    if (pass2 && s < 0.92f) s = std::max(s, ScoreVariant(qb, 0.92f, needRaw, ov, lv));
    if (s < needRaw) continue;
    // Tie-breakers worth at most 0.04: shallow paths and recently modified items first.
    const int depth = e.flags >> 2;
    s = s * 0.96f + 0.002f * float(10 - std::min(depth, 10));
    if (opt.today && e.days && opt.today >= e.days) {
      int age = opt.today - e.days;
      if (age < 30) s += 0.02f * float(30 - age) / 30.f;
    }
    if (s > top.Floor()) top.Push({uint32_t(i), s});
  }
  topFiles.Drain(files);
  topDirs.Drain(dirs);
}

namespace pathq {

bool LooksLikePath(std::wstring_view q) {
  auto isSep = [](wchar_t c) { return c == L'\\' || c == L'/'; };
  if (q.size() >= 3 && str::IsLetter(q[0]) && q[0] < 0x80 && q[1] == L':' && isSep(q[2])) return true;
  if (q.size() >= 3 && q[0] == L'\\' && q[1] == L'\\' && q[2] != L'\\') return true;
  if (!q.empty() && q[0] == L'~') return q.size() == 1 || isSep(q[1]);
  if (q.size() >= 3 && q[0] == L'%') {
    size_t close = q.find(L'%', 1);
    if (close == std::wstring_view::npos || close == 1) return false;
    for (size_t i = 1; i < close; ++i)
      if (str::IsSpace(q[i])) return false;
    return close + 1 == q.size() || isSep(q[close + 1]);
  }
  return false;
}

std::pair<std::wstring_view, std::wstring_view> SplitDirAndPrefix(std::wstring_view s) {
  size_t p = s.find_last_of(L"\\/");
  if (p == std::wstring_view::npos) return {{}, s};
  return {s.substr(0, p + 1), s.substr(p + 1)};
}

}  // namespace pathq
}  // namespace cs
