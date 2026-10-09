#pragma once
// Currency queries and rate tables. Portable, no Windows headers.
//   "100 usd", "100$", "$100", "€50", "100 долларов", "100 баксов в рублях", "100 евро в usd", "100 usd to eur",
//   "1к юаней в рубли" (к/k/тыс = 1e3, м/m/млн/кк = 1e6, млрд/bn = 1e9), "100*3 usd", "usd в рубли" (amount 1),
//   "100 usd в eur и gbp" (several targets).
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cs::currency {

// Exchange rates relative to `base` (1 base = rate units of currency). Codes are upper-case ISO 4217.
struct Rates {
  std::wstring base;
  int64_t updatedUnix = 0;  // when the provider last updated the rates (from the API), 0 = unknown
  std::vector<std::pair<std::wstring, double>> table;  // sorted by code

  const double* Find(std::wstring_view code) const;
  bool Has(std::wstring_view code) const { return Find(code) != nullptr; }
  // amount * rate(to) / rate(from). False if a code is unknown.
  bool Convert(double amount, std::wstring_view from, std::wstring_view to, double& out) const;
};

// Parses an API/cache response: {"base_code"|"base":"USD", "time_last_update_unix":N, "rates":{"EUR":0.9,...}}.
bool ParseRates(std::string_view utf8Json, Rates& out);

struct Query {
  double amount = 1;
  bool hasAmount = false;
  bool explicitTarget = false;
  bool commaDecimal = false;
  std::wstring from;             // ISO code
  std::vector<std::wstring> to;  // ISO codes, never contains `from`
};

// `base` is the default target (cfg.currencyBase). `rates` may be null: then 3-letter codes are recognised only
// if they are in the built-in dictionary. Returns false if the text is not a currency query (no currency token,
// or neither an amount nor an explicit target).
bool Parse(std::wstring_view text, std::wstring_view base, const Rates* rates, Query& out);

// Display symbol for a code ("₽", "$"...) or nullptr.
const wchar_t* Symbol(std::wstring_view code);

// "9 245,50" (2 decimals, thin-space groups); small values keep ~4 significant digits ("0,0108").
std::wstring FormatMoney(double v, wchar_t decimalSep = L',');

}  // namespace cs::currency
