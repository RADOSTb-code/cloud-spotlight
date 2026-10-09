#include "core/Str.h"

#include <cmath>
#include <cstdio>

namespace cs::str {

wchar_t LowerChar(wchar_t c) {
  if (c < 0x80) return (c >= L'A' && c <= L'Z') ? wchar_t(c + 32) : c;
  if (c >= 0x410 && c <= 0x42F) return wchar_t(c + 0x20);          // А-Я
  if (c >= 0x400 && c <= 0x40F) return c == 0x401 ? wchar_t(0x435) : wchar_t(c + 0x50);  // Ѐ-Џ, Ё->е
  if (c == 0x451) return wchar_t(0x435);                             // ё -> е
  if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return wchar_t(c + 0x20); // Latin-1
  return c;
}

bool IsSpace(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == 0xA0 || c == 0x202F || c == 0x2009; }
bool IsDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }
bool IsUpper(wchar_t c) { return LowerChar(c) != c; }
bool IsLetter(wchar_t c) {
  return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= 0x400 && c <= 0x4FF) ||
         (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7);
}

std::wstring ToLower(std::wstring_view s) {
  std::wstring r(s);
  for (auto& c : r) c = LowerChar(c);
  return r;
}

std::wstring_view Trim(std::wstring_view s) {
  size_t b = 0, e = s.size();
  while (b < e && IsSpace(s[b])) ++b;
  while (e > b && IsSpace(s[e - 1])) --e;
  return s.substr(b, e - b);
}

bool StartsWith(std::wstring_view s, std::wstring_view p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool EndsWith(std::wstring_view s, std::wstring_view p) {
  return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
bool IEquals(std::wstring_view a, std::wstring_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (LowerChar(a[i]) != LowerChar(b[i])) return false;
  return true;
}
bool IStartsWith(std::wstring_view s, std::wstring_view p) { return s.size() >= p.size() && IEquals(s.substr(0, p.size()), p); }

std::vector<std::wstring_view> Split(std::wstring_view s, wchar_t sep, bool skipEmpty) {
  std::vector<std::wstring_view> out;
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == sep) {
      if (!skipEmpty || i > start) out.push_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

std::vector<std::wstring_view> SplitWords(std::wstring_view s) {
  std::vector<std::wstring_view> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && IsSpace(s[i])) ++i;
    size_t b = i;
    while (i < s.size() && !IsSpace(s[i])) ++i;
    if (i > b) out.push_back(s.substr(b, i - b));
  }
  return out;
}

std::wstring ReplaceAll(std::wstring s, std::wstring_view from, std::wstring_view to) {
  if (from.empty()) return s;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::wstring::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

namespace {
constexpr wchar_t kEn[] = L"qwertyuiop[]asdfghjkl;'zxcvbnm,.`QWERTYUIOP{}ASDFGHJKL:\"ZXCVBNM<>~";
constexpr wchar_t kRu[] = L"йцукенгшщзхъфывапролджэячсмитьбюёЙЦУКЕНГШЩЗХЪФЫВАПРОЛДЖЭЯЧСМИТЬБЮЁ";
static_assert(sizeof(kEn) == sizeof(kRu));
}  // namespace

std::wstring SwapLayout(std::wstring_view s) {
  std::wstring r;
  r.reserve(s.size());
  constexpr size_t n = sizeof(kEn) / sizeof(wchar_t) - 1;
  for (wchar_t c : s) {
    wchar_t m = c;
    for (size_t i = 0; i < n; ++i) {
      if (kEn[i] == c) { m = kRu[i]; break; }
      if (kRu[i] == c) { m = kEn[i]; break; }
    }
    r.push_back(m);
  }
  return r;
}

std::wstring Utf8ToWide(std::string_view s) {
  std::wstring r;
  r.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    char32_t cp;
    int extra;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c >> 5) == 6) { cp = c & 0x1F; extra = 1; }
    else if ((c >> 4) == 14) { cp = c & 0x0F; extra = 2; }
    else if ((c >> 3) == 30) { cp = c & 0x07; extra = 3; }
    else { r.push_back(0xFFFD); ++i; continue; }
    if (i + extra >= s.size()) { r.push_back(0xFFFD); break; }
    bool ok = true;
    for (int k = 1; k <= extra; ++k) {
      unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc >> 6) != 2) { ok = false; break; }
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (!ok) { r.push_back(0xFFFD); ++i; continue; }
    i += 1 + extra;
    if constexpr (sizeof(wchar_t) == 2) {
      if (cp >= 0x10000) {
        cp -= 0x10000;
        r.push_back(wchar_t(0xD800 + (cp >> 10)));
        r.push_back(wchar_t(0xDC00 + (cp & 0x3FF)));
        continue;
      }
    }
    r.push_back(wchar_t(cp));
  }
  return r;
}

std::string WideToUtf8(std::wstring_view s) {
  std::string r;
  r.reserve(s.size() * 2);
  for (size_t i = 0; i < s.size(); ++i) {
    char32_t cp = static_cast<char32_t>(s[i]);
    if constexpr (sizeof(wchar_t) == 2) {
      if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size()) {
        char32_t lo = static_cast<char32_t>(s[i + 1]);
        if (lo >= 0xDC00 && lo <= 0xDFFF) {
          cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          ++i;
        }
      }
    }
    if (cp < 0x80) r.push_back(char(cp));
    else if (cp < 0x800) { r.push_back(char(0xC0 | (cp >> 6))); r.push_back(char(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
      r.push_back(char(0xE0 | (cp >> 12)));
      r.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
      r.push_back(char(0x80 | (cp & 0x3F)));
    } else {
      r.push_back(char(0xF0 | (cp >> 18)));
      r.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
      r.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
      r.push_back(char(0x80 | (cp & 0x3F)));
    }
  }
  return r;
}

std::wstring FormatNumber(double v, int maxFrac, bool group, wchar_t decimalSep) {
  if (std::isnan(v)) return L"NaN";
  if (std::isinf(v)) return v > 0 ? L"∞" : L"-∞";
  double av = std::fabs(v);
  char buf[64];
  if (av != 0 && (av >= 1e15 || av < 1e-9)) {
    std::snprintf(buf, sizeof buf, "%.*g", maxFrac > 0 ? maxFrac : 1, v);
    return Utf8ToWide(buf);
  }
  // Limit to ~15 significant digits to hide binary noise (0.1+0.2).
  int intDigits = av >= 1 ? int(std::floor(std::log10(av))) + 1 : 1;
  int frac = maxFrac;
  if (intDigits + frac > 15) frac = 15 - intDigits;
  if (frac < 0) frac = 0;
  std::snprintf(buf, sizeof buf, "%.*f", frac, v);
  std::string s(buf);
  if (auto dot = s.find('.'); dot != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  if (s == "-0") s = "0";
  std::wstring w = Utf8ToWide(s);
  size_t dot = w.find(L'.');
  if (dot != std::wstring::npos) w[dot] = decimalSep;
  if (group) {
    size_t intEnd = dot == std::wstring::npos ? w.size() : dot;
    size_t intBegin = (!w.empty() && w[0] == L'-') ? 1 : 0;
    std::wstring out = w.substr(0, intBegin);
    size_t len = intEnd - intBegin;
    for (size_t i = 0; i < len; ++i) {
      if (i && (len - i) % 3 == 0) out.push_back(L' ');
      out.push_back(w[intBegin + i]);
    }
    out += w.substr(intEnd);
    w = std::move(out);
  }
  return w;
}

}  // namespace cs::str
