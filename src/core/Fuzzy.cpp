#include "core/Fuzzy.h"

#include "core/Str.h"

namespace cs::fuzzy {
namespace {

inline bool IsSep(wchar_t c) {
  return c == L' ' || c == L'-' || c == L'_' || c == L'.' || c == L'\\' || c == L'/' || c == L'(' ||
         c == L')' || c == L'[' || c == L']' || c == L',' || c == L'&' || c == L'+';
}

inline bool IsWordStart(std::wstring_view orig, size_t i) {
  if (i == 0) return true;
  wchar_t p = orig[i - 1], c = orig[i];
  if (IsSep(p) && !IsSep(c)) return true;
  if (!str::IsUpper(p) && str::IsLetter(p) && str::IsUpper(c)) return true;  // camelCase
  if (!str::IsDigit(p) && str::IsDigit(c) && !IsSep(p)) return true;
  return false;
}

float ScoreOne(std::wstring_view q, std::wstring_view orig, std::wstring_view t) {
  const size_t n = q.size(), m = t.size();
  if (n == 0 || m == 0) return 0.f;
  const float lenRatio = m ? float(n) / float(m) : 0.f;
  if (n <= m) {
    if (t.compare(0, n, q) == 0) return n == m ? 1.f : 0.9f + 0.08f * lenRatio;
    // word-start substring
    float best = 0.f;
    size_t pos = t.find(q);
    while (pos != std::wstring_view::npos) {
      if (IsWordStart(orig, pos)) return 0.8f + 0.08f * lenRatio;
      if (best == 0.f) best = 0.6f + 0.1f * lenRatio - float(pos < 20 ? pos : 20) * 0.003f;
      pos = t.find(q, pos + 1);
    }
    // acronym: every query char matches consecutive word starts ("vsc" -> Visual Studio Code)
    if (n >= 2) {
      size_t qi = 0;
      for (size_t i = 0; i < m && qi < n; ++i)
        if (IsWordStart(orig, i) && !IsSep(orig[i])) {
          if (t[i] == q[qi]) ++qi;
          else break;
        }
      if (qi == n) return best > 0.75f ? best : 0.75f;
    }
    if (best > 0.f) return best;
  }
  // subsequence with gap penalty; spaces in the query are ignored
  size_t ti = 0, first = size_t(-1), gaps = 0, matched = 0;
  for (size_t qi = 0; qi < n; ++qi) {
    wchar_t ch = q[qi];
    if (ch == L' ') continue;
    size_t f = t.find(ch, ti);
    if (f == std::wstring_view::npos) return 0.f;
    if (first == size_t(-1)) first = f;
    if (f > ti && matched) gaps += f - ti;
    ti = f + 1;
    ++matched;
  }
  if (!matched) return 0.f;
  float s = 0.45f - float(gaps) * 0.015f - float(first < 10 ? first : 10) * 0.01f;
  return s > 0.05f ? s : 0.f;
}

}  // namespace

Query Prepare(std::wstring_view raw) {
  Query q;
  q.lower = str::ToLower(str::Trim(raw));
  q.swapped = str::ToLower(str::SwapLayout(q.lower));
  if (q.swapped == q.lower) q.swapped.clear();
  return q;
}

float ScoreLowered(const Query& q, std::wstring_view original, std::wstring_view lower) {
  if (q.lower.empty() || lower.empty()) return 0.f;
  float s = ScoreOne(q.lower, original, lower);
  if (s < 1.f && !q.swapped.empty()) {
    float s2 = ScoreOne(q.swapped, original, lower) * 0.92f;
    if (s2 > s) s = s2;
  }
  return s;
}

float Score(const Query& q, std::wstring_view candidate) {
  if (candidate.empty() || q.lower.empty()) return 0.f;
  wchar_t stackBuf[256];
  if (candidate.size() <= 256) {
    for (size_t i = 0; i < candidate.size(); ++i) stackBuf[i] = str::LowerChar(candidate[i]);
    return ScoreLowered(q, candidate, std::wstring_view(stackBuf, candidate.size()));
  }
  std::wstring lower = str::ToLower(candidate);
  return ScoreLowered(q, candidate, lower);
}

}  // namespace cs::fuzzy
