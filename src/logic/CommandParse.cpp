#include "logic/CommandParse.h"

#include <cmath>

#include "core/Fuzzy.h"
#include "core/Str.h"
#include "logic/Units.h"

namespace cs::cmd {
namespace {

using Words = std::vector<std::wstring_view>;

bool IsCyrillic(wchar_t c) { return c >= 0x400 && c <= 0x4FF; }

// Matches the whole of `words[from, to)` against an alias list (see units::MatchAliases).
bool MatchAll(const wchar_t* aliases, const Words& words, size_t from, size_t to, int* quality = nullptr) {
  if (from >= to) return false;
  Words sub(words.begin() + from, words.begin() + to);
  int q = 0;
  size_t n = units::MatchAliases(aliases, sub, 0, q);
  if (quality) *quality = q;
  return n == sub.size();
}

bool MatchOne(const wchar_t* aliases, std::wstring_view w) {
  Words one{w};
  int q = 0;
  return units::MatchAliases(aliases, one, 0, q) == 1;
}

// ---- numbers ------------------------------------------------------------------------------------------------

struct NumWord { const wchar_t* w; int v; };
const NumWord kNumWords[] = {
    {L"один", 1}, {L"одна", 1}, {L"одну", 1}, {L"одной", 1}, {L"два", 2}, {L"две", 2}, {L"три", 3},
    {L"четыре", 4}, {L"пять", 5}, {L"шесть", 6}, {L"семь", 7}, {L"восемь", 8}, {L"девять", 9},
    {L"десять", 10}, {L"одиннадцать", 11}, {L"двенадцать", 12}, {L"тринадцать", 13}, {L"четырнадцать", 14},
    {L"пятнадцать", 15}, {L"шестнадцать", 16}, {L"семнадцать", 17}, {L"восемнадцать", 18},
    {L"девятнадцать", 19}, {L"двадцать", 20}, {L"тридцать", 30}, {L"сорок", 40}, {L"пятьдесят", 50},
    {L"шестьдесят", 60}, {L"семьдесят", 70}, {L"восемьдесят", 80}, {L"девяносто", 90}, {L"сто", 100},
    {L"a", 1}, {L"an", 1}, {L"one", 1}, {L"two", 2}, {L"three", 3}, {L"four", 4}, {L"five", 5}, {L"six", 6},
    {L"seven", 7}, {L"eight", 8}, {L"nine", 9}, {L"ten", 10}, {L"eleven", 11}, {L"twelve", 12},
    {L"fifteen", 15}, {L"twenty", 20}, {L"thirty", 30}, {L"forty", 40}, {L"fifty", 50}, {L"sixty", 60},
    {L"ninety", 90},
};

int NumberWord(std::wstring_view w) {
  for (const auto& n : kNumWords)
    if (w == n.w) return n.v;
  return -1;
}

// "30", "1,5", "2.25" -> value; -1 if not a plain number.
double DigitsNumber(std::wstring_view w, size_t* len = nullptr) {
  size_t i = 0;
  double v = 0;
  while (i < w.size() && str::IsDigit(w[i])) v = v * 10 + (w[i++] - L'0');
  if (i == 0 || i > 9) return -1;
  if (i + 1 < w.size() && (w[i] == L'.' || w[i] == L',') && str::IsDigit(w[i + 1])) {
    ++i;
    double scale = 0.1;
    while (i < w.size() && str::IsDigit(w[i])) v += (w[i++] - L'0') * scale, scale /= 10;
  }
  if (len) *len = i;
  else if (i != w.size()) return -1;
  return v;
}

constexpr const wchar_t* kSecWords = L"с|сек|секунд*|секундоч*|s|sec|secs|second*";
constexpr const wchar_t* kMinWords = L"м|мин|минут*|минутк*|m|min|mins|minute*";
constexpr const wchar_t* kHourWords = L"ч|час*|h|hr|hrs|hour*";
constexpr const wchar_t* kDayWords = L"д|дн|день|дня|дней|сут*|d|day*";

int64_t UnitSeconds(std::wstring_view w) {
  if (MatchOne(kSecWords, w)) return 1;
  if (MatchOne(kMinWords, w)) return 60;
  if (MatchOne(kHourWords, w)) return 3600;
  if (MatchOne(kDayWords, w)) return 86400;
  return 0;
}

// "30m", "1h20m", "1ч20мин", "1,5ч", "90сек"
bool CompactDuration(std::wstring_view w, int64_t& secs) {
  if (w.empty() || !str::IsDigit(w[0])) return false;
  double total = 0;
  size_t i = 0;
  while (i < w.size()) {
    size_t len = 0;
    double v = DigitsNumber(w.substr(i), &len);
    if (v < 0) return false;
    i += len;
    size_t b = i;
    while (i < w.size() && str::IsLetter(w[i])) ++i;
    if (i == b) return false;
    int64_t u = UnitSeconds(w.substr(b, i - b));
    if (!u) return false;
    total += v * double(u);
  }
  secs = int64_t(std::llround(total));
  return secs > 0;
}

bool IsSingleLetterUnit(std::wstring_view w) { return w.size() == 1; }

}  // namespace

// ---- stemming -----------------------------------------------------------------------------------------------

std::wstring Stem(std::wstring_view word) {
  std::wstring w = str::ToLower(word);
  if (w.empty()) return w;
  bool cyr = false;
  for (wchar_t c : w) cyr = cyr || IsCyrillic(c);
  if (!cyr) {
    if (w.size() > 3 && w.back() == L's' && w[w.size() - 2] != L's') w.pop_back();
    return w;
  }
  static const wchar_t* const kEndings[] = {
      L"иями", L"ями", L"ами", L"ией", L"иях", L"его", L"ого", L"ему", L"ому", L"ыми", L"ими", L"ях", L"ах",
      L"ов", L"ев", L"ей", L"ам", L"ям", L"ой", L"ий", L"ый", L"ая", L"яя", L"ое", L"ее", L"ие", L"ые",
      L"ом", L"ем", L"ую", L"юю", L"ою", L"ею", L"их", L"ых", L"ия", L"ье", L"ья", L"а", L"я", L"ы",
      L"и", L"у", L"ю", L"е", L"о", L"ь", L"й",
  };
  for (const wchar_t* e : kEndings) {
    std::wstring_view ev(e);
    if (w.size() >= ev.size() + 3 && str::EndsWith(w, ev)) {
      w.resize(w.size() - ev.size());
      break;
    }
  }
  return w;
}

bool StemMatch(std::wstring_view a, std::wstring_view b) {
  std::wstring sa = Stem(a), sb = Stem(b);
  if (sa == sb) return true;
  size_t la = sa.size(), lb = sb.size();
  size_t mn = la < lb ? la : lb;
  if (mn < 4 || (la > lb ? la - lb : lb - la) > 2) return false;
  size_t cp = 0;
  while (cp < mn && sa[cp] == sb[cp]) ++cp;
  return cp >= 4 && cp + 1 >= mn;
}

// ---- durations ----------------------------------------------------------------------------------------------

bool ParseDuration(const std::vector<std::wstring_view>& words, size_t start, int64_t& seconds, size_t& used) {
  const size_t n = words.size();
  size_t i = start;
  double total = 0;
  bool any = false;
  while (i < n) {
    std::wstring_view w = words[i];
    size_t before = i;
    if (any && (w == L"и" || w == L"and")) ++i, w = i < n ? words[i] : std::wstring_view();
    if (i >= n) { i = before; break; }
    int64_t u;
    if (w == L"полчаса" || w == L"полчасика") {
      total += 1800, ++i, any = true;
      continue;
    }
    if ((w == L"пол" || w == L"half") && i + 1 < n) {
      size_t j = i + 1;
      if (w == L"half" && (words[j] == L"an" || words[j] == L"a") && j + 1 < n) ++j;
      if ((u = UnitSeconds(words[j])) != 0 && !IsSingleLetterUnit(words[j])) {
        total += double(u) / 2, i = j + 1, any = true;
        continue;
      }
    }
    if ((w == L"полтора" || w == L"полторы") && i + 1 < n && (u = UnitSeconds(words[i + 1])) != 0) {
      total += double(u) * 1.5, i += 2, any = true;
      continue;
    }
    if (w == L"четверть" && i + 1 < n && UnitSeconds(words[i + 1]) == 3600) {
      total += 900, i += 2, any = true;
      continue;
    }
    int64_t compact = 0;
    if (CompactDuration(w, compact)) {
      total += double(compact), ++i, any = true;
      continue;
    }
    double v = DigitsNumber(w);
    size_t j = i + 1;
    if (v < 0) {
      int nw = NumberWord(w);
      if (nw >= 0) {
        v = nw;
        if (nw >= 20 && nw % 10 == 0 && nw < 100 && j < n) {  // "двадцать пять"
          int u2 = NumberWord(words[j]);
          if (u2 >= 1 && u2 <= 9) v += u2, ++j;
        }
      }
    }
    if (v >= 0) {
      if (j < n && (u = UnitSeconds(words[j])) != 0) {
        total += v * double(u), i = j + 1, any = true;
        continue;
      }
      if (j == n && !any && v > 0 && DigitsNumber(w) >= 0) {  // bare trailing number -> minutes
        total += v * 60, i = j, any = true;
        break;
      }
      i = before;
      break;
    }
    if ((u = UnitSeconds(w)) != 0 && !IsSingleLetterUnit(w)) {  // "через час", "через минуту"
      total += double(u), ++i, any = true;
      continue;
    }
    i = before;
    break;
  }
  if (!any || total < 1) return false;
  seconds = int64_t(std::llround(total));
  used = i - start;
  return true;
}

int64_t ParseDurationText(std::wstring_view text) {
  std::wstring storage;
  auto words = units::Tokenize(text, storage);
  int64_t s = 0;
  size_t used = 0;
  if (!ParseDuration(words, 0, s, used) || used != words.size()) return -1;
  return s;
}

bool ParseClock(std::wstring_view w, int& h, int& m) {
  size_t i = 0;
  int hh = 0;
  while (i < w.size() && str::IsDigit(w[i]) && i < 2) hh = hh * 10 + (w[i++] - L'0');
  if (i == 0 || i >= w.size() || (w[i] != L':' && w[i] != L'.')) return false;
  ++i;
  if (w.size() - i != 2 || !str::IsDigit(w[i]) || !str::IsDigit(w[i + 1])) return false;
  int mm = (w[i] - L'0') * 10 + (w[i + 1] - L'0');
  if (hh > 23 || mm > 59) return false;
  h = hh, m = mm;
  return true;
}

int64_t SecondsUntil(int h, int m, int nowSecOfDay) {
  int64_t t = int64_t(h) * 3600 + int64_t(m) * 60 - nowSecOfDay;
  if (t <= 0) t += 86400;
  return t;
}

const wchar_t* PluralRu(int64_t n, const wchar_t* one, const wchar_t* few, const wchar_t* many) {
  n = n < 0 ? -n : n;
  int64_t m100 = n % 100, m10 = n % 10;
  if (m100 >= 11 && m100 <= 14) return many;
  if (m10 == 1) return one;
  if (m10 >= 2 && m10 <= 4) return few;
  return many;
}

std::wstring FormatDuration(int64_t s) {
  if (s < 0) s = 0;
  int64_t d = s / 86400, h = s % 86400 / 3600, m = s % 3600 / 60, sec = s % 60;
  std::wstring out;
  auto add = [&](int64_t v, const wchar_t* one, const wchar_t* few, const wchar_t* many) {
    if (!out.empty()) out.push_back(L' ');
    out += std::to_wstring(v);
    out.push_back(L' ');
    out += PluralRu(v, one, few, many);
  };
  if (d) add(d, L"день", L"дня", L"дней");
  if (h) add(h, L"час", L"часа", L"часов");
  if (m) add(m, L"минуту", L"минуты", L"минут");
  if (sec && !d && !h) add(sec, L"секунду", L"секунды", L"секунд");
  if (out.empty()) add(0, L"секунду", L"секунды", L"секунд");
  return out;
}

std::wstring FormatClock(int h, int m) {
  wchar_t buf[8] = {wchar_t(L'0' + h / 10 % 10), wchar_t(L'0' + h % 10), L':', wchar_t(L'0' + m / 10 % 10),
                    wchar_t(L'0' + m % 10), 0};
  return buf;
}

// ---- commands -----------------------------------------------------------------------------------------------

namespace {

struct Phrase {
  Kind kind;
  const wchar_t* aliases;
};

// clang-format off
const Phrase kPhrases[] = {
  {Kind::Shutdown, L"выключ*|выруб*|отключ*|заверш* работ*|shutdown|shut down|power off|turn off|poweroff"},
  {Kind::Restart, L"перезагруз*|перезапуст*|ребут*|рестарт*|restart|reboot"},
  {Kind::CancelShutdown, L"отмен* выключ*|отмен* перезагруз*|отмен* shutdown|cancel shutdown|cancel restart|"
                         L"cancel reboot|abort shutdown|shutdown -a|shutdown /a|не выключай*"},
  {Kind::Sleep, L"сон|спать|спящ* режим*|режим* сна|усыпи*|sleep|suspend|в сон|уснуть|засыпай"},
  {Kind::Hibernate, L"гибернац*|hibernate|hibernation|в гибернацию"},
  {Kind::Lock, L"заблокир*|блокир*|блокировк*|lock|lock screen|заблокир* экран*|блокировк* экран*"},
  {Kind::SignOut, L"выйти из систем*|выход из систем*|выйди из систем*|выйти из учетн* запис*|log off|log out|"
                  L"logoff|logout|sign out|sign off|разлогин*"},
  {Kind::EmptyRecycleBin, L"очист* корзин*|опустош* корзин*|empty recycle bin|empty bin|empty trash|"
                          L"clear recycle bin"},
  {Kind::MonitorOff, L"выключ* монитор*|выключ* экран*|выключ* дисплей*|отключ* монитор*|отключ* экран*|"
                     L"погас* экран*|monitor off|screen off|display off|turn off monitor|turn off screen|"
                     L"turn off display"},
  {Kind::Screensaver, L"заставк*|включ* заставк*|screensaver|screen saver|start screensaver"},
  {Kind::Timer, L"таймер*|timer|напомни*|напоминан*|remind*|remind me|засеки*|постав* таймер*|set timer|"
                L"set a timer|start timer"},
  {Kind::CancelTimer, L"отмен* таймер*|отмен* напоминан*|удал* таймер*|стоп таймер*|останов* таймер*|"
                      L"cancel timer*|stop timer*|cancel reminder*"},
  {Kind::Uuid, L"uuid|guid|гуид|сгенерир* uuid|сгенерир* guid|new guid|new uuid|generate uuid|generate guid"},
  {Kind::Password, L"пароль|password|сгенерир* пароль|генерир* пароль|созд* пароль|придума* пароль|"
                   L"generate password|new password|random password|случайн* пароль"},
  {Kind::Ip, L"ip|мой ip|my ip|айпи|ip адрес*|мой ip адрес*|ip address|my ip address|what is my ip"},
};

struct Keywords { Kind kind; const wchar_t* words[4]; };
const Keywords kSuggest[] = {
  {Kind::Shutdown, {L"выключить компьютер", L"завершение работы", L"shutdown", nullptr}},
  {Kind::Restart, {L"перезагрузить", L"перезагрузка", L"restart", L"reboot"}},
  {Kind::CancelShutdown, {L"отменить выключение", L"cancel shutdown", nullptr, nullptr}},
  {Kind::Sleep, {L"сон", L"спящий режим", L"sleep", nullptr}},
  {Kind::Hibernate, {L"гибернация", L"hibernate", nullptr, nullptr}},
  {Kind::Lock, {L"заблокировать", L"блокировка", L"lock", nullptr}},
  {Kind::SignOut, {L"выйти из системы", L"sign out", L"log off", nullptr}},
  {Kind::EmptyRecycleBin, {L"очистить корзину", L"empty recycle bin", nullptr, nullptr}},
  {Kind::MonitorOff, {L"выключить монитор", L"monitor off", nullptr, nullptr}},
  {Kind::Screensaver, {L"заставка", L"screensaver", nullptr, nullptr}},
  {Kind::Timer, {L"таймер", L"напомнить", L"timer", L"remind"}},
  {Kind::CancelTimer, {L"отменить таймер", L"cancel timer", nullptr, nullptr}},
  {Kind::Uuid, {L"uuid", L"guid", nullptr, nullptr}},
  {Kind::Password, {L"пароль", L"password", nullptr, nullptr}},
  {Kind::Ip, {L"ip адрес", L"my ip", nullptr, nullptr}},
};
// clang-format on

const wchar_t* AliasesOf(Kind k) {
  for (const auto& p : kPhrases)
    if (p.kind == k) return p.aliases;
  return L"";
}

constexpr const wchar_t* kNoise =
    L"компьютер*|комп|компа|пк|pc|computer|ноут|ноутбук*|laptop|пожалуйста|плиз|please|the|now|сейчас";

Kind MatchHead(const Words& head, size_t& timerUsed) {
  timerUsed = 0;
  Kind best = Kind::None;
  int bestQ = 0;
  for (const auto& p : kPhrases) {
    int q = 0;
    if (MatchAll(p.aliases, head, 0, head.size(), &q) && q > bestQ) best = p.kind, bestQ = q;
  }
  if (best != Kind::None) return best;
  // "напомни выключить чайник": timer phrase followed by the reminder text
  for (const auto& p : kPhrases) {
    if (p.kind != Kind::Timer) continue;
    int q = 0;
    size_t n = units::MatchAliases(p.aliases, head, 0, q);
    if (n) {
      timerUsed = n;
      return Kind::Timer;
    }
  }
  return Kind::None;
}

std::wstring JoinWords(const Words& w, size_t from, size_t to) {
  std::wstring r;
  for (size_t i = from; i < to && i < w.size(); ++i) {
    if (!r.empty()) r.push_back(L' ');
    r += w[i];
  }
  return r;
}

bool IsLabelFiller(std::wstring_view w) {
  return w == L"to" || w == L"that" || w == L"about" || w == L"чтобы" || w == L"что" || w == L"про" ||
         w == L"о" || w == L"об" || w == L"и";
}

}  // namespace

bool AllowsDelay(Kind k) {
  switch (k) {
    case Kind::Shutdown: case Kind::Restart: case Kind::Sleep: case Kind::Hibernate: case Kind::Lock:
    case Kind::SignOut: case Kind::MonitorOff: case Kind::Timer:
      return true;
    default:
      return false;
  }
}

const wchar_t* Title(Kind k) {
  switch (k) {
    case Kind::Shutdown: return L"Выключить компьютер";
    case Kind::Restart: return L"Перезагрузить компьютер";
    case Kind::CancelShutdown: return L"Отменить запланированное выключение";
    case Kind::Sleep: return L"Спящий режим";
    case Kind::Hibernate: return L"Гибернация";
    case Kind::Lock: return L"Заблокировать компьютер";
    case Kind::SignOut: return L"Выйти из системы";
    case Kind::EmptyRecycleBin: return L"Очистить корзину";
    case Kind::MonitorOff: return L"Выключить монитор";
    case Kind::Screensaver: return L"Запустить заставку";
    case Kind::Timer: return L"Таймер";
    case Kind::CancelTimer: return L"Отменить таймеры";
    case Kind::Uuid: return L"Новый GUID";
    case Kind::Password: return L"Случайный пароль";
    case Kind::Ip: return L"IP-адрес";
    case Kind::None: break;
  }
  return L"";
}

bool Parse(std::wstring_view query, int nowSecOfDay, Command& out) {
  out = Command{};
  std::wstring storage;
  Words all = units::Tokenize(query, storage);
  Words words;
  words.reserve(all.size());
  for (auto w : all)
    if (!MatchOne(kNoise, w)) words.push_back(w);
  const size_t n = words.size();
  if (n == 0 || n > 24) return false;

  // "пароль 20", "password 32"
  if (n >= 2) {
    double len = DigitsNumber(words[n - 1]);
    if (len >= 1 && len == std::floor(len) && MatchAll(AliasesOf(Kind::Password), words, 0, n - 1)) {
      if (len < 4 || len > 256) return false;
      out.kind = Kind::Password;
      out.length = int(len);
      return true;
    }
  }

  size_t headEnd = n, restBegin = n;
  for (size_t k = 0; k < n; ++k) {
    std::wstring_view w = words[k];
    int64_t secs = 0;
    size_t used = 0;
    int h = 0, m = 0;
    if ((w == L"через" || w == L"in" || w == L"after" || w == L"спустя") && k + 1 < n &&
        ParseDuration(words, k + 1, secs, used)) {
      headEnd = k, restBegin = k + 1 + used, out.delaySec = secs;
      break;
    }
    if ((w == L"в" || w == L"во" || w == L"at" || w == L"на") && k + 1 < n && ParseClock(words[k + 1], h, m)) {
      headEnd = k, restBegin = k + 2;
      out.atClock = true, out.clockH = h, out.clockM = m;
      out.delaySec = SecondsUntil(h, m, nowSecOfDay);
      break;
    }
    bool numeric = str::IsDigit(w[0]) || (w.size() > 2 && NumberWord(w) > 0) || str::StartsWith(w, L"пол");
    if (k > 0 && numeric && ParseDuration(words, k, secs, used)) {  // "таймер 5 минут", "shutdown 30m"
      headEnd = k, restBegin = k + used, out.delaySec = secs;
      break;
    }
  }
  if (headEnd == 0) return false;
  Words head(words.begin(), words.begin() + headEnd);
  size_t timerUsed = 0;
  Kind kind = MatchHead(head, timerUsed);
  if (kind == Kind::None) return false;
  if (kind == Kind::Timer) {
    size_t lb = timerUsed ? timerUsed : headEnd;
    std::wstring label = JoinWords(words, lb, headEnd);
    size_t rb = restBegin;
    while (rb < n && IsLabelFiller(words[rb])) ++rb;
    std::wstring rest = JoinWords(words, rb, n);
    if (!label.empty() && !rest.empty()) label.push_back(L' ');
    out.label = label + rest;
  } else if (restBegin != n) {
    return false;
  }
  if (out.delaySec >= 0 && !AllowsDelay(kind)) return false;
  if ((kind == Kind::Shutdown || kind == Kind::Restart) && out.delaySec > 315360000) return false;
  out.kind = kind;
  return true;
}

void Suggest(std::wstring_view query, std::vector<std::pair<Kind, float>>& out) {
  auto q = fuzzy::Prepare(query);
  if (q.lower.size() < 2) return;
  for (const auto& k : kSuggest) {
    float best = 0;
    for (const wchar_t* w : k.words) {
      if (!w) break;
      float s = fuzzy::Score(q, w);
      best = s > best ? s : best;
    }
    if (best >= 0.7f) out.emplace_back(k.kind, best);
  }
}

// ---- folders ------------------------------------------------------------------------------------------------

const std::vector<BuiltinFolder>& BuiltinFolders() {
  static const std::vector<BuiltinFolder> kFolders = {
      {KnownFolder::Downloads, L"Загрузки", L"загрузки|закачки|downloads"},
      {KnownFolder::Documents, L"Документы", L"документы|доки|documents|docs"},
      {KnownFolder::Desktop, L"Рабочий стол", L"рабочий стол|десктоп|desktop"},
      {KnownFolder::Pictures, L"Изображения", L"изображения|картинки|фото|фотографии|pictures|images|photos"},
      {KnownFolder::Music, L"Музыка", L"музыка|music"},
      {KnownFolder::Videos, L"Видео", L"видео|videos"},
      {KnownFolder::AppData, L"AppData (Roaming)", L"appdata|roaming|аппдата"},
      {KnownFolder::LocalAppData, L"AppData (Local)", L"localappdata|local appdata|appdata local"},
      {KnownFolder::Temp, L"Временные файлы (Temp)", L"temp|tmp|темп|временные файлы"},
      {KnownFolder::ProgramFiles, L"Program Files", L"program files|программы"},
      {KnownFolder::Startup, L"Автозагрузка", L"автозагрузка|автозапуск|startup|autostart"},
      {KnownFolder::RecycleBin, L"Корзина", L"корзина|recycle bin|trash"},
      {KnownFolder::ThisPC, L"Этот компьютер", L"этот компьютер|мой компьютер|компьютер|this pc|my computer"},
      {KnownFolder::UserProfile, L"Папка пользователя", L"папка пользователя|домашняя папка|профиль|home|user"},
  };
  return kFolders;
}

bool ParseFolderPhrase(std::wstring_view query, FolderPhrase& out) {
  out = FolderPhrase{};
  std::wstring storage;
  Words w = units::Tokenize(query, storage);
  size_t b = 0, e = w.size();
  static constexpr const wchar_t* kVerbs =
      L"откр*|open|покажи|показать|show|перейти|перейди|go|goto|зайти|зайди|browse";
  static constexpr const wchar_t* kNouns =
      L"папк*|папочк*|folder|folders|dir|directory|каталог*|директори*";
  if (b < e && MatchOne(kVerbs, w[b])) {
    out.explicitPhrase = true;
    ++b;
    if (b < e && (w[b] == L"в" || w[b] == L"во" || w[b] == L"to" || w[b] == L"in" || w[b] == L"into" ||
                  w[b] == L"the" || w[b] == L"my"))
      ++b;
  }
  if (b < e && MatchOne(kNouns, w[b])) out.explicitPhrase = true, ++b;
  if (e > b + 1 && MatchOne(kNouns, w[e - 1])) out.explicitPhrase = true, --e;
  if (b >= e) return false;
  out.target = JoinWords(w, b, e);
  return true;
}

float FolderNameScore(std::wstring_view target, std::wstring_view name) {
  std::wstring ts, ns;
  Words tw0 = units::Tokenize(target, ts);
  Words nw = units::Tokenize(name, ns);
  Words tw;
  for (auto x : tw0)
    if (x != L"мои" && x != L"мой" && x != L"моя" && x != L"my" && x != L"the") tw.push_back(x);
  if (!tw.empty() && tw.size() == nw.size()) {
    bool all = true;
    for (size_t i = 0; i < tw.size() && all; ++i) all = StemMatch(tw[i], nw[i]);
    if (all) return 1.f;
  }
  auto q = fuzzy::Prepare(target);
  return fuzzy::Score(q, name);
}

}  // namespace cs::cmd

// ---- web ----------------------------------------------------------------------------------------------------

namespace cs::web {
namespace {

bool IsCyrillic(wchar_t c) { return c >= 0x400 && c <= 0x4FF; }
bool IsAsciiAlnum(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || str::IsDigit(c); }

const wchar_t* const kTlds[] = {
    L"com", L"net", L"org", L"ru", L"su", L"рф", L"io", L"dev", L"app", L"ai", L"me", L"info", L"biz", L"co",
    L"uk", L"de", L"fr", L"it", L"es", L"nl", L"pl", L"ua", L"by", L"kz", L"uz", L"am", L"ge", L"eu", L"us",
    L"ca", L"jp", L"cn", L"tv", L"xyz", L"site", L"online", L"tech", L"store", L"blog", L"cloud", L"page",
    L"gg", L"edu", L"gov", L"pro", L"moscow", L"tj", L"kg", L"az", L"lv", L"lt", L"ee", L"fi", L"se", L"no",
    L"dk", L"ch", L"at", L"be", L"cz", L"sk", L"hu", L"ro", L"bg", L"gr", L"tr", L"il", L"in", L"kr", L"br",
    L"mx", L"ar", L"au", L"nz", L"news", L"live", L"space", L"website", L"art", L"shop", L"club", L"top",
    L"link", L"world", L"tools", L"games", L"ms", L"fm", L"tech", L"stream", L"email", L"local",
};

bool IsIPv4(std::wstring_view h) {
  int parts = 0;
  size_t i = 0;
  while (i <= h.size()) {
    size_t b = i;
    int v = 0;
    while (i < h.size() && str::IsDigit(h[i]) && i - b < 3) v = v * 10 + (h[i++] - L'0');
    if (i == b || v > 255) return false;
    ++parts;
    if (i == h.size()) break;
    if (h[i] != L'.') return false;
    ++i;
  }
  return parts == 4;
}

}  // namespace

std::wstring UrlEncode(std::wstring_view s) {
  std::string u = str::WideToUtf8(s);
  static const char* hex = "0123456789ABCDEF";
  std::wstring out;
  out.reserve(u.size() * 3);
  for (unsigned char c : u) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out.push_back(wchar_t(c));
    } else {
      out.push_back(L'%');
      out.push_back(wchar_t(hex[c >> 4]));
      out.push_back(wchar_t(hex[c & 15]));
    }
  }
  return out;
}

bool LooksLikeUrl(std::wstring_view q, std::wstring& url) {
  q = str::Trim(q);
  if (q.size() < 4 || q.size() > 2048) return false;
  for (wchar_t c : q)
    if (str::IsSpace(c)) return false;
  if (str::IStartsWith(q, L"http://") || str::IStartsWith(q, L"https://")) {
    if (q.size() <= 8 || (q.find(L'.') == std::wstring_view::npos && q.find(L"localhost") == std::wstring_view::npos))
      return false;
    url = q;
    return true;
  }
  size_t hostEnd = q.find_first_of(L"/?#");
  std::wstring_view host = q.substr(0, hostEnd == std::wstring_view::npos ? q.size() : hostEnd);
  if (host.find(L'@') != std::wstring_view::npos) return false;
  if (size_t colon = host.rfind(L':'); colon != std::wstring_view::npos) {
    std::wstring_view port = host.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    for (wchar_t c : port)
      if (!str::IsDigit(c)) return false;
    host = host.substr(0, colon);
  }
  std::wstring lowHost = str::ToLower(host);
  if (lowHost == L"localhost" || IsIPv4(lowHost)) {
    url = L"http://" + std::wstring(q);
    return true;
  }
  // domain.tld: labels of letters/digits/'-', TLD from a whitelist (avoids file names like "readme.md")
  auto labels = str::Split(lowHost, L'.', false);
  if (labels.size() < 2) return false;
  for (auto l : labels) {
    if (l.empty() || l.front() == L'-' || l.back() == L'-') return false;
    for (wchar_t c : l)
      if (!IsAsciiAlnum(c) && c != L'-' && !IsCyrillic(c)) return false;
  }
  std::wstring_view tld = labels.back();
  bool known = false;
  for (const wchar_t* t : kTlds) known = known || tld == t;
  bool www = labels.size() >= 3 && labels.front() == L"www";
  if (!known && !www) return false;
  url = L"https://" + std::wstring(q);
  return true;
}

Search Build(std::wstring_view query, std::wstring_view defaultTemplate) {
  Search r;
  std::wstring_view q = str::Trim(query);
  if (LooksLikeUrl(q, r.url)) {
    r.title = L"Открыть " + r.url;
    r.score = 0.97f;
    r.isUrl = true;
    return r;
  }
  struct Engine { const wchar_t* prefixes; const wchar_t* tmpl; const wchar_t* where; };
  static const Engine kEngines[] = {
      {L"g", L"https://www.google.com/search?q={q}", L"в Google"},
      {L"y|я", L"https://yandex.ru/search/?text={q}", L"в Яндексе"},
      {L"yt", L"https://www.youtube.com/results?search_query={q}", L"на YouTube"},
      {L"gh", L"https://github.com/search?q={q}", L"на GitHub"},
      {L"w|вики|wiki", L"https://ru.wikipedia.org/w/index.php?search={q}", L"в Википедии"},
      {L"tr|перевод|translate", nullptr, nullptr},
  };
  std::wstring tmpl(defaultTemplate);
  std::wstring where = L"в интернете";
  std::wstring_view text = q;
  size_t sp = q.find(L' ');
  if (sp != std::wstring_view::npos) {
    std::wstring first = str::ToLower(q.substr(0, sp));
    std::wstring_view rest = str::Trim(q.substr(sp + 1));
    for (const auto& e : kEngines) {
      bool hit = false;
      for (auto p : str::Split(e.prefixes, L'|')) hit = hit || first == p;
      if (!hit || rest.empty()) continue;
      text = rest;
      r.prefixed = true;
      r.score = 0.95f;
      if (!e.tmpl) {
        bool cyr = false;
        for (wchar_t c : rest) cyr = cyr || IsCyrillic(c);
        tmpl = std::wstring(L"https://translate.google.com/?sl=auto&tl=") + (cyr ? L"en" : L"ru") +
               L"&text={q}&op=translate";
        r.title = L"Перевести «" + std::wstring(rest) + L"»";
      } else {
        tmpl = e.tmpl;
        where = e.where;
      }
      break;
    }
  }
  std::wstring enc = UrlEncode(text);
  size_t pos = tmpl.find(L"{q}");
  r.url = pos == std::wstring::npos ? tmpl + enc : tmpl.replace(pos, 3, enc);
  if (r.title.empty()) r.title = L"Искать «" + std::wstring(text) + L"» " + where;
  return r;
}

}  // namespace cs::web
