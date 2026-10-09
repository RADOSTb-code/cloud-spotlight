#include "logic/Units.h"

#include <cmath>
#include <cstdint>

#include "core/Str.h"
#include "logic/Calc.h"

namespace cs::units {
namespace {

enum class Cat : uint8_t { Length, Mass, Temperature, Data, Time, Speed, Area, Volume };

struct Unit {
  const char* id;
  Cat cat;
  double factor;  // value * factor + offset = base unit (m, kg, K, byte, s, m/s, m², L)
  double offset;
  const wchar_t* sym;
  const wchar_t* aliases;
  const char* defaults;  // space-separated ids shown when no target unit is given
};

constexpr double kKiB = 1024.0;

// clang-format off
const Unit kUnits[] = {
  // Length (m)
  {"nm", Cat::Length, 1e-9, 0, L"нм", L"nm|нм|нанометр*|nanomet*", "um"},
  {"um", Cat::Length, 1e-6, 0, L"мкм", L"um|µm|μm|мкм|микрон*|микрометр*|micron*|micromet*", "mm"},
  {"mm", Cat::Length, 1e-3, 0, L"мм", L"mm|мм|миллиметр*|millimet*", "in"},
  {"cm", Cat::Length, 1e-2, 0, L"см", L"cm|см|сантиметр*|centimet*", "in"},
  {"dm", Cat::Length, 0.1, 0, L"дм", L"dm|дм|дециметр*|decimet*", "cm"},
  {"m", Cat::Length, 1, 0, L"м", L"m|м|метр*|meter*|metre*", "ft"},
  {"km", Cat::Length, 1000, 0, L"км", L"km|км|километр*|kilomet*", "mi"},
  {"in", Cat::Length, 0.0254, 0, L"дюйм.", L"in|inch|inches|дюйм*|\"|″", "cm"},
  {"ft", Cat::Length, 0.3048, 0, L"фут.", L"ft|foot|feet|фут*|'|′", "m"},
  {"yd", Cat::Length, 0.9144, 0, L"ярд.", L"yd|yds|yard*|ярд*", "m"},
  {"mi", Cat::Length, 1609.344, 0, L"миль", L"mi|mile*|миль|мили|миля|милю|милях|милям|милей|милями", "km"},
  {"nmi", Cat::Length, 1852, 0, L"мор. миль", L"nmi|морск* мил*|морск* миль|nautical mile*", "km"},
  // Mass (kg)
  {"mg", Cat::Mass, 1e-6, 0, L"мг", L"mg|мг|миллиграмм*|milligram*", "g"},
  {"g", Cat::Mass, 1e-3, 0, L"г", L"g|г|гр|грамм*|грам|gram*", "oz"},
  {"kg", Cat::Mass, 1, 0, L"кг", L"kg|кг|кило|килограмм*|kilogram*|kilo|kilos", "lb"},
  {"t", Cat::Mass, 1000, 0, L"т", L"t|т|тонн*|tonne*|ton|tons", "kg"},
  {"lb", Cat::Mass, 0.45359237, 0, L"фунт.", L"lb|lbs|pound*|фунт*", "kg"},
  {"oz", Cat::Mass, 0.028349523125, 0, L"унц.", L"oz|ounce*|унци*|унц", "g"},
  {"ct", Cat::Mass, 0.0002, 0, L"кар.", L"ct|carat*|карат*", "g"},
  {"st", Cat::Mass, 6.35029318, 0, L"стоун", L"st|stone|stones|стоун*", "kg"},
  // Temperature (K)
  {"c", Cat::Temperature, 1, 273.15, L"°C",
   L"c|°c|°с|℃|цельси*|градус* цельси*|градус* по цельси*|по цельси*|celsius|deg c|degree* celsius", "f"},
  {"f", Cat::Temperature, 5.0 / 9, 459.67 * 5 / 9, L"°F",
   L"f|°f|℉|°ф|фаренгейт*|градус* фаренгейт*|градус* по фаренгейт*|по фаренгейт*|fahrenheit|deg f|"
   L"degree* fahrenheit", "c"},
  {"k", Cat::Temperature, 1, 0, L"K", L"k|°k|кельвин*|kelvin*", "c"},
  // Data (byte)
  {"bit", Cat::Data, 0.125, 0, L"бит", L"bit|bits|бит|бита|битов|битах", "b"},
  {"b", Cat::Data, 1, 0, L"Б", L"b|б|байт*|byte*", "kb"},
  {"kbit", Cat::Data, 125, 0, L"Кбит", L"kbit|kbits|кбит|килобит*|kilobit*", "kb"},
  {"mbit", Cat::Data, 125e3, 0, L"Мбит", L"mbit|mbits|мбит|мегабит*|megabit*", "mb"},
  {"gbit", Cat::Data, 125e6, 0, L"Гбит", L"gbit|gbits|гбит|гигабит*|gigabit*", "gb"},
  {"kb", Cat::Data, kKiB, 0, L"КБ", L"kb|кб|кбайт|килобайт*|kilobyte*", "mb"},
  {"mb", Cat::Data, kKiB * kKiB, 0, L"МБ", L"mb|мб|мбайт|мегабайт*|megabyte*|мег|мегов", "gb kb"},
  {"gb", Cat::Data, kKiB * kKiB * kKiB, 0, L"ГБ", L"gb|гб|гбайт|гигабайт*|gigabyte*|гиг|гига|гигов", "mb"},
  {"tb", Cat::Data, kKiB * kKiB * kKiB * kKiB, 0, L"ТБ", L"tb|тб|тбайт|терабайт*|terabyte*", "gb"},
  {"pb", Cat::Data, kKiB * kKiB * kKiB * kKiB * kKiB, 0, L"ПБ", L"pb|пб|петабайт*|petabyte*", "tb"},
  {"kib", Cat::Data, kKiB, 0, L"КиБ", L"kib|киб|кибибайт*|kibibyte*", "b"},
  {"mib", Cat::Data, kKiB * kKiB, 0, L"МиБ", L"mib|миб|мебибайт*|mebibyte*", "kib"},
  {"gib", Cat::Data, kKiB * kKiB * kKiB, 0, L"ГиБ", L"gib|гиб|гибибайт*|gibibyte*", "mib"},
  {"tib", Cat::Data, kKiB * kKiB * kKiB * kKiB, 0, L"ТиБ", L"tib|тиб|тебибайт*|tebibyte*", "gib"},
  // Time (s)
  {"ms", Cat::Time, 1e-3, 0, L"мс", L"ms|msec|мс|миллисекунд*|millisecond*", "s"},
  {"s", Cat::Time, 1, 0, L"с", L"s|с|сек|sec|secs|секунд*|second*", "min"},
  {"min", Cat::Time, 60, 0, L"мин", L"min|mins|мин|минут*|minute*", "h s"},
  {"h", Cat::Time, 3600, 0, L"ч", L"h|hr|hrs|ч|час*|hour*", "min"},
  {"d", Cat::Time, 86400, 0, L"дн.", L"d|day*|день|дня|дней|дни|днях|дням|сутки|суток|сут", "h"},
  {"wk", Cat::Time, 604800, 0, L"нед.", L"wk|week*|недел*|нед", "d"},
  {"mo", Cat::Time, 2629746, 0, L"мес.", L"month*|месяц*|мес", "d"},
  {"yr", Cat::Time, 31556952, 0, L"г.", L"y|yr|yrs|year*|год|года|годов|годы|году|лет", "d"},
  // Speed (m/s)
  {"mps", Cat::Speed, 1, 0, L"м/с", L"m/s|м/с|м/сек|mps|метр* в секунд*|метр* в сек|meter* per second", "kmh"},
  {"kmh", Cat::Speed, 1 / 3.6, 0, L"км/ч",
   L"km/h|км/ч|км/час|kmh|kph|км в час|километр* в час|kilomet* per hour", "mph mps"},
  {"mph", Cat::Speed, 0.44704, 0, L"миль/ч", L"mph|mi/h|миль/ч|миль в час|мили в час|миля в час|miles per hour",
   "kmh"},
  {"kn", Cat::Speed, 1852.0 / 3600, 0, L"уз.", L"kn|kt|knot*|узел|узла|узлов|узлы|узлах", "kmh"},
  {"fps", Cat::Speed, 0.3048, 0, L"фут/с", L"ft/s|fps|фут/с|фут* в секунд*", "mps"},
  // Area (m²)
  {"mm2", Cat::Area, 1e-6, 0, L"мм²", L"mm2|mm²|мм2|мм²|кв мм|квадратн* миллиметр*|sq mm", "cm2"},
  {"cm2", Cat::Area, 1e-4, 0, L"см²", L"cm2|cm²|см2|см²|кв см|квадратн* сантиметр*|sq cm", "in2"},
  {"m2", Cat::Area, 1, 0, L"м²", L"m2|m²|м2|м²|кв м|квм|кв метр*|квадратн* метр*|sq m|sqm|square met*", "ft2"},
  {"km2", Cat::Area, 1e6, 0, L"км²", L"km2|km²|км2|км²|кв км|квадратн* километр*|sq km", "mi2"},
  {"ha", Cat::Area, 1e4, 0, L"га", L"ha|га|гектар*|hectare*", "m2 acre"},
  {"are", Cat::Area, 100, 0, L"сот.", L"сотк*|соток|сотых|are|ares", "m2"},
  {"acre", Cat::Area, 4046.8564224, 0, L"акр.", L"ac|acre*|акр*", "ha"},
  {"ft2", Cat::Area, 0.09290304, 0, L"фут²", L"ft2|ft²|sq ft|sqft|кв фут*|квадратн* фут*|square f*", "m2"},
  {"in2", Cat::Area, 0.00064516, 0, L"дюйм²", L"in2|in²|sq in|кв дюйм*|квадратн* дюйм*", "cm2"},
  {"mi2", Cat::Area, 2589988.110336, 0, L"миль²", L"mi2|mi²|sq mi|кв мил*|квадратн* мил*", "km2"},
  // Volume (L)
  {"ml", Cat::Volume, 1e-3, 0, L"мл", L"ml|мл|миллилитр*|millilit*", "floz"},
  {"l", Cat::Volume, 1, 0, L"л", L"l|л|lt|литр*|liter*|litre*", "gal"},
  {"cm3", Cat::Volume, 1e-3, 0, L"см³", L"cm3|cm³|см3|см³|куб см|кубическ* сантиметр*|cc", "ml"},
  {"m3", Cat::Volume, 1000, 0, L"м³", L"m3|m³|м3|м³|куб м|кубометр*|кубическ* метр*|cubic met*", "l"},
  {"gal", Cat::Volume, 3.785411784, 0, L"гал.", L"gal|gals|gallon*|галлон*", "l"},
  {"qt", Cat::Volume, 0.946352946, 0, L"кварт.", L"qt|quart*|кварт|кварты|квартах", "l"},
  {"pt", Cat::Volume, 0.473176473, 0, L"пинт.", L"pt|pint*|пинт*", "ml"},
  {"cup", Cat::Volume, 0.2365882365, 0, L"чашк.", L"cup|cups|чашк*", "ml"},
  {"floz", Cat::Volume, 0.0295735295625, 0, L"жидк. унц.", L"fl oz|floz|жидк* унц*|fluid ounce*", "ml"},
  {"tbsp", Cat::Volume, 0.01478676478125, 0, L"ст. л.", L"tbsp|tablespoon*|ст л|столов* лож*", "ml"},
  {"tsp", Cat::Volume, 0.00492892159375, 0, L"ч. л.", L"tsp|teaspoon*|ч л|чайн* лож*", "ml"},
};
// clang-format on

const Unit* FindById(std::string_view id) {
  for (const auto& u : kUnits)
    if (id == u.id) return &u;
  return nullptr;
}

const Unit* MatchUnit(const std::vector<std::wstring_view>& words, size_t at, size_t& consumed, size_t& chars) {
  const Unit* best = nullptr;
  int bestQ = 0;
  consumed = 0;
  for (const auto& u : kUnits) {
    int q = 0;
    size_t ch = 0;
    size_t n = MatchAliases(u.aliases, words, at, q, &ch);
    if (n && q > bestQ) best = &u, bestQ = q, consumed = n, chars = ch;
  }
  return best;
}

bool IsSeparator(std::wstring_view w) {
  return w == L"в" || w == L"во" || w == L"to" || w == L"in" || w == L"into" || w == L"->" || w == L"→" ||
         w == L"=" || w == L"как" || w == L"as";
}

double ToBase(const Unit& u, double v) { return v * u.factor + u.offset; }
double FromBase(const Unit& u, double v) { return (v - u.offset) / u.factor; }

bool WordMatches(std::wstring_view pattern, std::wstring_view word, bool& exact) {
  if (!pattern.empty() && pattern.back() == L'*') {
    exact = false;
    return str::StartsWith(word, pattern.substr(0, pattern.size() - 1));
  }
  exact = true;
  return pattern == word;
}

}  // namespace

size_t MatchAliases(std::wstring_view aliases, const std::vector<std::wstring_view>& words, size_t at,
                    int& quality, size_t* matchedChars) {
  size_t bestN = 0;
  int bestQ = 0;
  size_t bestChars = 0;
  size_t p = 0;
  while (p <= aliases.size()) {
    size_t bar = aliases.find(L'|', p);
    if (bar == std::wstring_view::npos) bar = aliases.size();
    std::wstring_view pat = aliases.substr(p, bar - p);
    p = bar + 1;
    // match pattern words one by one
    size_t wi = at, q = 0, chars = 0;
    bool allExact = true, ok = !pat.empty();
    while (ok && q <= pat.size()) {
      size_t sp = pat.find(L' ', q);
      if (sp == std::wstring_view::npos) sp = pat.size();
      std::wstring_view pw = pat.substr(q, sp - q);
      q = sp + 1;
      bool exact = true;
      if (wi >= words.size() || !WordMatches(pw, words[wi], exact)) {
        ok = false;
        break;
      }
      allExact = allExact && exact;
      chars += pw.size();
      ++wi;
      if (sp == pat.size()) break;
    }
    if (!ok) continue;
    size_t n = wi - at;
    int qual = int(n) * 1000 + (allExact ? 500 : 0) + int(chars);
    if (qual > bestQ) bestQ = qual, bestN = n, bestChars = chars;
  }
  quality = bestQ;
  if (matchedChars) *matchedChars = bestChars;
  return bestN;
}

std::vector<std::wstring_view> Tokenize(std::wstring_view text, std::wstring& storage) {
  storage.clear();
  storage.reserve(text.size() + 8);
  for (size_t i = 0; i < text.size(); ++i) {
    wchar_t c = str::LowerChar(text[i]);
    if (c == L'-' && i + 1 < text.size() && text[i + 1] == L'>') {
      storage += L" -> ";
      ++i;
    } else if (c == L'→' || c == L'=') {
      storage.push_back(L' ');
      storage.push_back(c);
      storage.push_back(L' ');
    } else {
      storage.push_back(c);
    }
  }
  std::vector<std::wstring_view> words = str::SplitWords(storage);
  for (auto& w : words) {
    while (w.size() > 1 && (w.back() == L'.' || w.back() == L',' || w.back() == L'?' || w.back() == L'!'))
      w.remove_suffix(1);
  }
  return words;
}

bool Convert(std::wstring_view query, Answer& out) {
  out = Answer{};
  query = str::Trim(query);
  if (query.empty()) return false;
  wchar_t c0 = query[0];
  if (!str::IsDigit(c0) && c0 != L'-' && c0 != L'+' && c0 != L'.' && c0 != L'(') return false;

  calc::Result amount;
  size_t n = calc::ParseLeadingAmount(query, amount);
  if (!n) return false;
  std::wstring storage;
  auto words = Tokenize(query.substr(n), storage);
  if (words.empty()) return false;

  size_t used = 0, chars = 0;
  const Unit* from = MatchUnit(words, 0, used, chars);
  if (!from) return false;
  size_t idx = used;
  std::vector<const Unit*> targets;
  if (idx == words.size()) {
    if (chars <= 1) return false;  // "5 m", "2 c": too ambiguous without an explicit target
    std::string_view defs(from->defaults);
    size_t p = 0;
    while (p < defs.size()) {
      size_t sp = defs.find(' ', p);
      if (sp == std::string_view::npos) sp = defs.size();
      if (const Unit* u = FindById(defs.substr(p, sp - p))) targets.push_back(u);
      p = sp + 1;
    }
  } else {
    if (IsSeparator(words[idx]) && idx + 1 < words.size()) ++idx;
    size_t used2 = 0, chars2 = 0;
    const Unit* to = MatchUnit(words, idx, used2, chars2);
    if (!to || idx + used2 != words.size() || to->cat != from->cat) return false;
    targets.push_back(to);
  }

  double base = ToBase(*from, amount.value);
  for (const Unit* to : targets) {
    double v = FromBase(*to, base);
    if (!std::isfinite(v)) return false;
    out.items.push_back({amount.value, v, from->sym, to->sym});
  }
  out.commaDecimal = amount.commaDecimal;
  return !out.items.empty();
}

std::wstring FormatValue(double v, wchar_t decimalSep) {
  double av = std::fabs(v);
  int frac;
  if (av >= 1000) frac = 2;
  else if (av >= 1) frac = 4;
  else if (av == 0) frac = 0;
  else {
    frac = int(-std::floor(std::log10(av))) + 3;  // ~4 significant digits
    if (frac < 4) frac = 4;
    if (frac > 12) frac = 12;
  }
  return str::FormatNumber(v, frac, true, decimalSep);
}

}  // namespace cs::units
