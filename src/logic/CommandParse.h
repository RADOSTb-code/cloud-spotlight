#pragma once
// Quick-command phrase parsing (Russian + English), folder-phrase matching and web-search helpers.
// Portable, no Windows headers; the Win32 side lives in providers/CommandsProvider and WebSearchProvider.
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cs::cmd {

// ---- Russian/English light stemming -------------------------------------------------------------------------
// Lowercases and strips one common inflectional ending (keeping >= 3 chars): "проектов" -> "проект",
// "загрузки" -> "загрузк", "downloads" -> "download".
std::wstring Stem(std::wstring_view word);
// Inflection-tolerant word equality: equal stems, or stems differing only in a fleeting vowel/last letter
// ("загрузки" ~ "загрузок", "картинки" ~ "картинок").
bool StemMatch(std::wstring_view a, std::wstring_view b);

// ---- Durations and clock times ------------------------------------------------------------------------------
// Parses a duration starting at words[start]: "30 минут", "1 ч 20 мин", "1 час и 20 минут", "полчаса",
// "полтора часа", "час", "45 min", "30m", "1h20m", "1,5 часа", "двадцать пять минут", "an hour".
// A bare number is accepted only as the last word (minutes). Returns seconds and the number of words used.
bool ParseDuration(const std::vector<std::wstring_view>& words, size_t start, int64_t& seconds, size_t& used);
// Whole text must be a duration. -1 if not.
int64_t ParseDurationText(std::wstring_view text);
// "23:30", "7.05" -> hour/minute.
bool ParseClock(std::wstring_view word, int& h, int& m);
// Seconds from `nowSecOfDay` (local) until h:m today, or tomorrow if it has passed (never 0).
int64_t SecondsUntil(int h, int m, int nowSecOfDay);
// Accusative, as used after "через": "1 час 20 минут", "21 минуту", "30 секунд", "2 дня 3 часа".
std::wstring FormatDuration(int64_t seconds);
// Russian plural: n=1 -> one, 2..4 -> few, else many (11..14 -> many).
const wchar_t* PluralRu(int64_t n, const wchar_t* one, const wchar_t* few, const wchar_t* many);
// "07:05"
std::wstring FormatClock(int h, int m);

// ---- Commands -----------------------------------------------------------------------------------------------
enum class Kind : uint8_t {
  None, Shutdown, Restart, CancelShutdown, Sleep, Hibernate, Lock, SignOut, EmptyRecycleBin, MonitorOff,
  Screensaver, Timer, CancelTimer, Uuid, Password, Ip,
};

struct Command {
  Kind kind = Kind::None;
  int64_t delaySec = -1;  // -1 = now / not specified
  bool atClock = false;   // delay came from "в 23:30"
  int clockH = 0, clockM = 0;
  std::wstring label;     // reminder text ("напомни через 10 минут выключить чайник" -> "выключить чайник")
  int length = 0;         // password length (0 = default)
};

// Recognises a complete command phrase ("выключить через 30 минут", "сон", "таймер 5 минут", "пароль 20").
// `nowSecOfDay` is the local time of day (for "в 23:30"). Returns false if the text is not a full phrase.
bool Parse(std::wstring_view query, int nowSecOfDay, Command& out);

// Fuzzy suggestions for partially typed command names ("выкл", "забл", "slee"). Score 0..1 (fuzzy).
void Suggest(std::wstring_view query, std::vector<std::pair<Kind, float>>& out);

// Russian UI title of a command without delay, e.g. L"Выключить компьютер".
const wchar_t* Title(Kind k);
// True if the command accepts a delay ("через N минут").
bool AllowsDelay(Kind k);

// ---- Folders ------------------------------------------------------------------------------------------------
enum class KnownFolder : uint8_t {
  Downloads, Documents, Desktop, Pictures, Music, Videos, AppData, LocalAppData, Temp, ProgramFiles,
  Startup, RecycleBin, ThisPC, UserProfile,
};
struct BuiltinFolder {
  KnownFolder id;
  const wchar_t* title;  // Russian display name
  const wchar_t* names;  // '|'-separated names matched by FolderNameScore
};
const std::vector<BuiltinFolder>& BuiltinFolders();

struct FolderPhrase {
  std::wstring target;   // folder name part, lowercased ("проектов")
  bool explicitPhrase = false;  // had a verb ("открой") or noun ("папка", "folder")
};
// "открыть папку проектов" -> {"проектов", true}; "проекты" -> {"проекты", false}.
bool ParseFolderPhrase(std::wstring_view query, FolderPhrase& out);
// 1.0 if every word matches by stem ("проектов" vs "проекты", "рабочего стола" vs "рабочий стол"), else the
// fuzzy score of `target` against `name` (0..1).
float FolderNameScore(std::wstring_view target, std::wstring_view name);

}  // namespace cs::cmd

namespace cs::web {

// Percent-encodes UTF-8 for a URL query component (spaces -> %20).
std::wstring UrlEncode(std::wstring_view s);

// "github.com", "https://x.y/z", "localhost:3000", "192.168.1.1:8080/a" -> normalised URL (with scheme).
bool LooksLikeUrl(std::wstring_view q, std::wstring& url);

struct Search {
  std::wstring url;
  std::wstring title;  // L"Искать «q» в Google"
  float score = 0.02f;
  bool prefixed = false;  // "g q", "yt q", ...
  bool isUrl = false;
};
// Builds the web-search result for `query`. `defaultTemplate` contains "{q}".
Search Build(std::wstring_view query, std::wstring_view defaultTemplate);

}  // namespace cs::web
