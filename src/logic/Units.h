#pragma once
// Unit converter: "10 km in miles", "5 футов в см", "100 f to c", "30°C в F", "1.5 гб в мб", "2 часа в минуты",
// "60 mph в км/ч". Categories: length, mass, temperature, data (KB/MB/GB = 1024 like Windows; KiB… explicit;
// kbit/Mbit = 1000), time, speed, area, volume. Russian names in common inflections, English names/symbols.
// Separators: в, во, to, in, into, ->, →, =. Without a target unit 1-2 sensible conversions are returned
// (but not for ambiguous one-letter units like "5 m", "2 c"). Portable, no Windows headers.
#include <string>
#include <string_view>
#include <vector>

namespace cs::units {

struct Conversion {
  double from = 0;
  double to = 0;
  const wchar_t* fromSym = L"";  // display symbol (Russian), e.g. L"км"
  const wchar_t* toSym = L"";
};

struct Answer {
  std::vector<Conversion> items;
  bool commaDecimal = false;  // the amount was typed with ',' -> format with ','
};

// Returns false if `query` is not a unit conversion.
bool Convert(std::wstring_view query, Answer& out);

// Value formatting for converter results: ~4-6 significant fraction digits, thin-space grouping.
std::wstring FormatValue(double v, wchar_t decimalSep);

// --- Phrase matching shared with CurrencyQuery ---------------------------------------------------------------
// `aliases` is a '|'-separated list of patterns; a pattern is one or more space-separated words, a word ending in
// '*' matches by prefix ("градус* цельси*"). `words` must be lowercased. Returns the number of words consumed by
// the best pattern at `at` (0 = no match) and its quality (more words > exact > longer prefix) in `quality`.
size_t MatchAliases(std::wstring_view aliases, const std::vector<std::wstring_view>& words, size_t at,
                    int& quality, size_t* matchedChars = nullptr);

// Lowercases, pads "->", "→", "=" with spaces and splits into words with trailing '.' / ',' stripped.
// The views point into `storage`.
std::vector<std::wstring_view> Tokenize(std::wstring_view text, std::wstring& storage);

}  // namespace cs::units
