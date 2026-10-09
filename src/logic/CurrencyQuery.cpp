#include "logic/CurrencyQuery.h"

#include <algorithm>
#include <cmath>

#include "core/Json.h"
#include "core/Str.h"
#include "logic/Calc.h"
#include "logic/Units.h"

namespace cs::currency {
namespace {

struct Entry {
  const wchar_t* code;
  const wchar_t* aliases;  // lowercased patterns, see units::MatchAliases
};

// clang-format off
const Entry kDict[] = {
  {L"USD", L"usd|доллар*|долар*|бакс*|dollar*|buck|bucks|американск* доллар*|us dollar*"},
  {L"EUR", L"eur|евро|euro|euros"},
  {L"RUB", L"rub|rur|рубл*|руб|р|ruble*|rouble*|российск* рубл*|russian ruble*"},
  {L"BYN", L"byn|белорусск* рубл*|бел руб*|belarusian ruble*"},
  {L"CNY", L"cny|rmb|юан*|yuan|renminbi|жэньминьби|китайск* юан*"},
  {L"JPY", L"jpy|иен*|йен*|yen|японск* иен*|японск* йен*"},
  {L"GBP", L"gbp|фунт*|pound*|quid|стерлинг*|британск* фунт*|фунт* стерлинг*|british pound*"},
  {L"CHF", L"chf|франк*|franc*|швейцарск* франк*|swiss franc*"},
  {L"KZT", L"kzt|тенге|tenge|тг"},
  {L"UAH", L"uah|гривн*|гривен|грн|hryvn*|hrivn*"},
  {L"PLN", L"pln|злот*|zloty|zlotys|zl|zł|польск* злот*"},
  {L"TRY", L"try|лир*|lira|liras|lire|турецк* лир*|turkish lira*"},
  {L"GEL", L"gel|лари|lari"},
  {L"AMD", L"amd|драм*|dram|drams"},
  {L"UZS", L"uzs|сум|сума|сумов|сумы|узбекск* сум*"},
  {L"KGS", L"kgs|сом|сома|сомов|сомы|киргизск* сом*|кыргызск* сом*"},
  {L"TJS", L"tjs|сомони"},
  {L"AZN", L"azn|манат*|manat*|азербайджанск* манат*"},
  {L"TMT", L"tmt|туркменск* манат*"},
  {L"INR", L"inr|рупи*|rupee*|индийск* рупи*"},
  {L"KRW", L"krw|вон|воны|вона|вонах|won|корейск* вон*"},
  {L"ILS", L"ils|шекел*|шекл*|shekel*|sheqel*"},
  {L"THB", L"thb|бат|бата|батов|баты|baht|тайск* бат*"},
  {L"AED", L"aed|дирхам*|dirham*"},
  {L"CZK", L"czk|крон*|krona|krone|koruna|чешск* крон*|czech koruna*"},
  {L"SEK", L"sek|шведск* крон*|swedish krona*"},
  {L"NOK", L"nok|норвежск* крон*|norwegian krone*"},
  {L"DKK", L"dkk|датск* крон*|danish krone*"},
  {L"CAD", L"cad|канадск* доллар*|canadian dollar*"},
  {L"AUD", L"aud|австралийск* доллар*|australian dollar*"},
  {L"NZD", L"nzd|новозеландск* доллар*"},
  {L"HKD", L"hkd|гонконгск* доллар*|hong kong dollar*"},
  {L"SGD", L"sgd|сингапурск* доллар*|singapore dollar*"},
  {L"HUF", L"huf|форинт*|forint*"},
  {L"BRL", L"brl|реал|реала|реалов|реалы|real|reais|бразильск* реал*"},
  {L"MXN", L"mxn|песо|peso|pesos|мексиканск* песо"},
  {L"VND", L"vnd|донг|донга|донгов|dong"},
  {L"MDL", L"mdl|молдавск* ле*|лей|леев|лея"},
  {L"RON", L"ron|румынск* ле*"},
  {L"BGN", L"bgn|болгарск* лев*|лев|лева|левов"},
  {L"RSD", L"rsd|динар*|dinar*|сербск* динар*"},
  {L"EGP", L"egp|египетск* фунт*"},
  {L"BTC", L"btc|биткоин*|биткойн*|bitcoin*"},
};

struct Sym { const wchar_t* sym; const wchar_t* code; };
// Input symbols. ¥ is read as CNY (more common than JPY for Russian users); "иена"/JPY works by name.
const Sym kInputSymbols[] = {
  {L"$", L"USD"}, {L"€", L"EUR"}, {L"₽", L"RUB"}, {L"£", L"GBP"}, {L"¥", L"CNY"}, {L"₴", L"UAH"},
  {L"₸", L"KZT"}, {L"₺", L"TRY"}, {L"₹", L"INR"}, {L"₩", L"KRW"}, {L"₪", L"ILS"}, {L"₾", L"GEL"},
  {L"₼", L"AZN"}, {L"zł", L"PLN"}, {L"₿", L"BTC"}, {L"฿", L"THB"}, {L"₫", L"VND"}, {L"₱", L"PHP"},
};
const Sym kDisplaySymbols[] = {
  {L"$", L"USD"}, {L"€", L"EUR"}, {L"₽", L"RUB"}, {L"£", L"GBP"}, {L"¥", L"CNY"}, {L"¥", L"JPY"},
  {L"₴", L"UAH"}, {L"₸", L"KZT"}, {L"₺", L"TRY"}, {L"₹", L"INR"}, {L"₩", L"KRW"}, {L"₪", L"ILS"},
  {L"₾", L"GEL"}, {L"₼", L"AZN"}, {L"zł", L"PLN"}, {L"₿", L"BTC"}, {L"฿", L"THB"}, {L"₫", L"VND"},
  {L"₱", L"PHP"},
};
// clang-format on

bool IsSymbolChar(wchar_t c) {
  for (const auto& s : kInputSymbols)
    if (s.sym[1] == 0 && s.sym[0] == c) return true;
  return false;
}

const wchar_t* SymbolCode(std::wstring_view w) {
  for (const auto& s : kInputSymbols)
    if (w == s.sym) return s.code;
  return nullptr;
}

bool InDict(std::wstring_view code) {
  for (const auto& e : kDict)
    if (code == e.code) return true;
  return false;
}

// Matches a currency at words[at]; returns words consumed (0 = none).
size_t MatchCurrency(const std::vector<std::wstring_view>& words, size_t at, const Rates* rates,
                     std::wstring& code) {
  if (at >= words.size()) return 0;
  if (const wchar_t* c = SymbolCode(words[at])) {
    code = c;
    return 1;
  }
  size_t bestN = 0;
  int bestQ = 0;
  for (const auto& e : kDict) {
    int q = 0;
    size_t n = units::MatchAliases(e.aliases, words, at, q);
    if (n && q > bestQ) bestQ = q, bestN = n, code = e.code;
  }
  if (bestN) return bestN;
  std::wstring_view w = words[at];
  if (w.size() == 3 && std::all_of(w.begin(), w.end(), [](wchar_t c) { return c >= L'a' && c <= L'z'; })) {
    std::wstring up(w);
    for (auto& c : up) c = wchar_t(c - 32);
    if (rates ? rates->Has(up) : InDict(up)) {
      code = std::move(up);
      return 1;
    }
  }
  return 0;
}

double Multiplier(std::wstring_view w) {
  static constexpr const wchar_t* k1e3 = L"к|k|тыс|тысяч*|тыщ*|thousand*";
  static constexpr const wchar_t* k1e6 = L"кк|м|m|млн|миллион*|million*|mln|mio";
  static constexpr const wchar_t* k1e9 = L"млрд|миллиард*|billion*|bn";
  std::vector<std::wstring_view> one{w};
  int q = 0;
  if (units::MatchAliases(k1e3, one, 0, q)) return 1e3;
  if (units::MatchAliases(k1e6, one, 0, q)) return 1e6;
  if (units::MatchAliases(k1e9, one, 0, q)) return 1e9;
  return 0;
}

bool IsSeparator(std::wstring_view w) {
  return w == L"в" || w == L"во" || w == L"to" || w == L"in" || w == L"into" || w == L"->" || w == L"→" ||
         w == L"=" || w == L"на";
}

bool IsConjunction(std::wstring_view w) { return w == L"и" || w == L"and" || w == L"," || w == L"&" || w == L"+"; }

}  // namespace

const double* Rates::Find(std::wstring_view code) const {
  auto it = std::lower_bound(table.begin(), table.end(), code,
                             [](const std::pair<std::wstring, double>& p, std::wstring_view c) { return p.first < c; });
  return it != table.end() && it->first == code ? &it->second : nullptr;
}

bool Rates::Convert(double amount, std::wstring_view from, std::wstring_view to, double& out) const {
  const double* f = Find(from);
  const double* t = Find(to);
  if (!f || !t || *f <= 0) return false;
  out = from == to ? amount : amount / *f * *t;
  return std::isfinite(out);
}

bool ParseRates(std::string_view utf8Json, Rates& out) {
  json::Value v;
  if (!json::Parse(utf8Json, v) || !v.IsObject()) return false;
  if (v.Has("result") && v["result"].AsString() != L"success") return false;
  const json::Value& rates = v["rates"];
  if (!rates.IsObject()) return false;
  Rates r;
  r.base = v["base_code"].AsString();
  if (r.base.empty()) r.base = v["base"].AsString();
  if (r.base.empty()) r.base = L"USD";
  for (auto& c : r.base) c = (c >= L'a' && c <= L'z') ? wchar_t(c - 32) : c;
  r.updatedUnix = int64_t(v["time_last_update_unix"].AsNumber(0));
  for (const auto& [k, val] : rates.Members()) {
    if (!val.IsNumber() || !(val.AsNumber() > 0)) continue;
    std::wstring code = str::Utf8ToWide(k);
    for (auto& c : code) c = (c >= L'a' && c <= L'z') ? wchar_t(c - 32) : c;
    r.table.emplace_back(std::move(code), val.AsNumber());
  }
  if (r.table.empty()) return false;
  std::sort(r.table.begin(), r.table.end());
  r.table.erase(std::unique(r.table.begin(), r.table.end(),
                            [](const auto& a, const auto& b) { return a.first == b.first; }),
                r.table.end());
  if (!r.Has(r.base)) {
    r.table.emplace_back(r.base, 1.0);
    std::sort(r.table.begin(), r.table.end());
  }
  out = std::move(r);
  return true;
}

bool Parse(std::wstring_view text, std::wstring_view base, const Rates* rates, Query& out) {
  out = Query{};
  std::wstring_view s = str::Trim(text);
  if (s.empty() || s.size() > 120) return false;

  // Leading symbol: "$100", "€ 50"
  for (const auto& sym : kInputSymbols) {
    if (str::IStartsWith(s, sym.sym)) {
      out.from = sym.code;
      s = str::Trim(s.substr(std::wstring_view(sym.sym).size()));
      break;
    }
  }
  calc::Result amount;
  if (size_t n = calc::ParseLeadingAmount(s, amount)) {
    out.amount = amount.value;
    out.hasAmount = true;
    out.commaDecimal = amount.commaDecimal;
    s = s.substr(n);
  } else if (!s.empty() && str::IsDigit(s[0])) {
    return false;  // starts with a number that is not a valid amount ("1.2.3 usd")
  }

  // separate glued symbols: "100$в рубли" -> "100 $ в рубли"
  std::wstring spaced;
  spaced.reserve(s.size() + 8);
  for (wchar_t c : s) {
    if (IsSymbolChar(c)) {
      spaced.push_back(L' ');
      spaced.push_back(c);
      spaced.push_back(L' ');
    } else {
      spaced.push_back(c);
    }
  }
  std::wstring storage;
  auto words = units::Tokenize(spaced, storage);
  size_t idx = 0;

  if (out.hasAmount && idx < words.size()) {
    if (double m = Multiplier(words[idx]); m > 0) {
      // "1 м" alone is ambiguous with units; a currency must follow anyway
      out.amount *= m;
      ++idx;
    }
  }
  if (out.from.empty()) {
    size_t n = MatchCurrency(words, idx, rates, out.from);
    if (!n) return false;
    idx += n;
  }
  if (idx < words.size()) {
    if (IsSeparator(words[idx])) ++idx;
    if (idx >= words.size()) return false;
    while (idx < words.size()) {
      std::wstring code;
      size_t n = MatchCurrency(words, idx, rates, code);
      if (!n) return false;
      idx += n;
      if (std::find(out.to.begin(), out.to.end(), code) == out.to.end() && code != out.from) out.to.push_back(code);
      if (idx < words.size() && IsConjunction(words[idx])) ++idx;
    }
    out.explicitTarget = true;
    if (out.to.empty()) return false;  // "100 usd в usd"
  }
  if (!out.hasAmount && !out.explicitTarget) return false;

  if (out.to.empty()) {
    std::wstring b(base);
    if (b.empty()) b = L"RUB";
    if (b == out.from) {
      for (const wchar_t* c : {L"USD", L"EUR"})
        if (out.from != c) out.to.push_back(c);
    } else {
      out.to.push_back(b);
    }
  }
  return true;
}

const wchar_t* Symbol(std::wstring_view code) {
  for (const auto& s : kDisplaySymbols)
    if (code == s.code) return s.sym;
  return nullptr;
}

std::wstring FormatMoney(double v, wchar_t decimalSep) {
  double av = std::fabs(v);
  if (av != 0 && av < 1) {
    int frac = int(-std::floor(std::log10(av))) + 3;
    if (frac < 2) frac = 2;
    if (frac > 10) frac = 10;
    return str::FormatNumber(v, frac, true, decimalSep);
  }
  double r = std::round(v * 100) / 100;
  std::wstring s = str::FormatNumber(r, 2, true, decimalSep);
  if (av >= 1e15) return s;
  size_t dot = s.find(decimalSep);
  if (dot == std::wstring::npos) s += std::wstring(1, decimalSep) + L"00";
  else if (s.size() - dot == 2) s.push_back(L'0');
  return s;
}

}  // namespace cs::currency
