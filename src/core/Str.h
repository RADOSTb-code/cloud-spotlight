#pragma once
// Portable string helpers (no Windows headers). wchar_t is UTF-16 on Windows, UTF-32 on Linux (tests).
#include <string>
#include <string_view>
#include <vector>

namespace cs::str {

// Fast lowercase for ASCII, Latin-1, Cyrillic. Also folds 'ё' -> 'е'.
wchar_t LowerChar(wchar_t c);
std::wstring ToLower(std::wstring_view s);
bool IsSpace(wchar_t c);
bool IsDigit(wchar_t c);
bool IsLetter(wchar_t c);  // ASCII/Latin-1/Cyrillic letters
bool IsUpper(wchar_t c);

std::wstring_view Trim(std::wstring_view s);
bool StartsWith(std::wstring_view s, std::wstring_view prefix);
bool EndsWith(std::wstring_view s, std::wstring_view suffix);
// Case-insensitive (via LowerChar) helpers
bool IEquals(std::wstring_view a, std::wstring_view b);
bool IStartsWith(std::wstring_view s, std::wstring_view prefix);

std::vector<std::wstring_view> Split(std::wstring_view s, wchar_t sep, bool skipEmpty = true);
std::vector<std::wstring_view> SplitWords(std::wstring_view s);  // on whitespace
std::wstring ReplaceAll(std::wstring s, std::wstring_view from, std::wstring_view to);

// Convert text typed in the wrong keyboard layout (EN <-> RU ЙЦУКЕН).
std::wstring SwapLayout(std::wstring_view s);

std::wstring Utf8ToWide(std::string_view s);
std::string WideToUtf8(std::wstring_view s);

// Number formatting for calculator/currency: up to `maxFrac` fraction digits, trailing zeros trimmed,
// thin-space thousands grouping if `group` (e.g. 1 234 567,89 uses `decimalSep`).
std::wstring FormatNumber(double v, int maxFrac = 10, bool group = false, wchar_t decimalSep = L'.');

}  // namespace cs::str
