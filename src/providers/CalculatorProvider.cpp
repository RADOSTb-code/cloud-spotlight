#include "providers/CalculatorProvider.h"

#include <cmath>
#include <cstdint>

#include "core/Config.h"
#include "core/Str.h"
#include "logic/Calc.h"
#include "logic/Units.h"

namespace cs {
namespace {

constexpr const wchar_t* kGlyphCalc = L"";
constexpr const wchar_t* kGlyphConvert = L"";

std::wstring StripGroups(std::wstring s) {
  std::wstring r;
  r.reserve(s.size());
  for (wchar_t c : s)
    if (c != 0x2009) r.push_back(c);
  return r;
}

std::wstring Hex(double v) {
  bool neg = v < 0;
  uint64_t u = uint64_t(neg ? -v : v);
  wchar_t buf[24];
  int n = 0;
  do {
    unsigned d = unsigned(u & 15);
    buf[n++] = wchar_t(d < 10 ? L'0' + d : L'A' + d - 10);
    u >>= 4;
  } while (u);
  std::wstring r = neg ? L"-0x" : L"0x";
  while (n) r.push_back(buf[--n]);
  return r;
}

}  // namespace

void CalculatorProvider::Init(const Config& cfg, IHost& host) {
  (void)cfg;
  host_ = &host;
}

void CalculatorProvider::Search(std::wstring_view query, std::vector<Result>& out) {
  calc::Result r = calc::Evaluate(query);
  if (r.ok && r.isExpression) {
    wchar_t dec = r.commaDecimal ? L',' : L'.';
    Result res;
    res.title = str::FormatNumber(r.value, 10, true, dec);
    res.subtitle = L"= " + calc::Pretty(query);
    if (r.radixLiteral && r.value == std::floor(r.value) && std::fabs(r.value) < 9007199254740992.0)
      res.subtitle += L"  ·  " + Hex(r.value);
    res.category = L"Калькулятор";
    res.iconKind = IconKind::Glyph;
    res.icon = kGlyphCalc;
    res.score = 0.99f;
    res.actions = kActOpen | kActCopy;
    res.payload = StripGroups(res.title);
    res.copyText = res.payload;
    out.push_back(std::move(res));
  }

  units::Answer ans;
  if (units::Convert(query, ans)) {
    wchar_t dec = ans.commaDecimal ? L',' : L'.';
    float score = 0.98f;
    for (const auto& c : ans.items) {
      Result res;
      std::wstring value = units::FormatValue(c.to, dec);
      res.title = value + L" " + c.toSym;
      res.subtitle = units::FormatValue(c.from, dec) + L" " + c.fromSym + L" = " + res.title;
      res.category = L"Конвертер";
      res.iconKind = IconKind::Glyph;
      res.icon = kGlyphConvert;
      res.score = score;
      score -= 0.001f;
      res.actions = kActOpen | kActCopy;
      res.payload = StripGroups(value);
      res.copyText = res.payload;
      out.push_back(std::move(res));
    }
  }
}

ExecResult CalculatorProvider::Execute(const Result& r, Action a) {
  (void)a;  // Enter and Ctrl+C both copy the value
  if (host_ && !r.payload.empty()) host_->CopyToClipboard(r.payload);
  return ExecResult{};
}

}  // namespace cs
