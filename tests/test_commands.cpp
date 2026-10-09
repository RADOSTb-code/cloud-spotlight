#include "Test.h"
#include "logic/CommandParse.h"

using namespace cs;
using cmd::Kind;

namespace {

constexpr int kNoon = 12 * 3600;

struct C {
  bool ok;
  cmd::Command c;
};
C P(const wchar_t* q, int now = kNoon) {
  C r;
  r.ok = cmd::Parse(q, now, r.c);
  return r;
}
int64_t D(const wchar_t* text) { return cmd::ParseDurationText(text); }

}  // namespace

TEST(Cmd_Durations) {
  CHECK_EQ(D(L"30 минут"), int64_t(1800));
  CHECK_EQ(D(L"1 минуту"), int64_t(60));
  CHECK_EQ(D(L"час"), int64_t(3600));
  CHECK_EQ(D(L"1 ч 20 мин"), int64_t(4800));
  CHECK_EQ(D(L"1 час и 20 минут"), int64_t(4800));
  CHECK_EQ(D(L"полчаса"), int64_t(1800));
  CHECK_EQ(D(L"пол часа"), int64_t(1800));
  CHECK_EQ(D(L"полтора часа"), int64_t(5400));
  CHECK_EQ(D(L"полторы минуты"), int64_t(90));
  CHECK_EQ(D(L"четверть часа"), int64_t(900));
  CHECK_EQ(D(L"45 min"), int64_t(2700));
  CHECK_EQ(D(L"30m"), int64_t(1800));
  CHECK_EQ(D(L"1h20m"), int64_t(4800));
  CHECK_EQ(D(L"1ч20м"), int64_t(4800));
  CHECK_EQ(D(L"1,5 часа"), int64_t(5400));
  CHECK_EQ(D(L"1.5h"), int64_t(5400));
  CHECK_EQ(D(L"90 сек"), int64_t(90));
  CHECK_EQ(D(L"10 секунд"), int64_t(10));
  CHECK_EQ(D(L"двадцать пять минут"), int64_t(1500));
  CHECK_EQ(D(L"пять минут"), int64_t(300));
  CHECK_EQ(D(L"одну минуту"), int64_t(60));
  CHECK_EQ(D(L"два часа"), int64_t(7200));
  CHECK_EQ(D(L"an hour"), int64_t(3600));
  CHECK_EQ(D(L"half an hour"), int64_t(1800));
  CHECK_EQ(D(L"2 hours 15 minutes"), int64_t(8100));
  CHECK_EQ(D(L"1 день"), int64_t(86400));
  CHECK_EQ(D(L"15"), int64_t(900));  // bare number = minutes
  CHECK_EQ(D(L"минута"), int64_t(60));
  CHECK_EQ(D(L"abc"), int64_t(-1));
  CHECK_EQ(D(L"30 яблок"), int64_t(-1));
  CHECK_EQ(D(L"0 минут"), int64_t(-1));
}

TEST(Cmd_ClockAndFormat) {
  int h = 0, m = 0;
  CHECK(cmd::ParseClock(L"23:30", h, m) && h == 23 && m == 30);
  CHECK(cmd::ParseClock(L"7.05", h, m) && h == 7 && m == 5);
  CHECK(!cmd::ParseClock(L"24:00", h, m));
  CHECK(!cmd::ParseClock(L"12:60", h, m));
  CHECK(!cmd::ParseClock(L"12:5", h, m));
  CHECK(!cmd::ParseClock(L"1230", h, m));
  CHECK_EQ(cmd::SecondsUntil(23, 30, 23 * 3600), int64_t(1800));
  CHECK_EQ(cmd::SecondsUntil(1, 0, 23 * 3600), int64_t(7200));          // tomorrow
  CHECK_EQ(cmd::SecondsUntil(12, 0, 12 * 3600), int64_t(86400));        // exactly now -> tomorrow
  CHECK_EQ(cmd::FormatDuration(1800), std::wstring(L"30 минут"));
  CHECK_EQ(cmd::FormatDuration(60), std::wstring(L"1 минуту"));
  CHECK_EQ(cmd::FormatDuration(21 * 60), std::wstring(L"21 минуту"));
  CHECK_EQ(cmd::FormatDuration(22 * 60), std::wstring(L"22 минуты"));
  CHECK_EQ(cmd::FormatDuration(11 * 60), std::wstring(L"11 минут"));
  CHECK_EQ(cmd::FormatDuration(3600), std::wstring(L"1 час"));
  CHECK_EQ(cmd::FormatDuration(4800), std::wstring(L"1 час 20 минут"));
  CHECK_EQ(cmd::FormatDuration(2 * 3600 + 60), std::wstring(L"2 часа 1 минуту"));
  CHECK_EQ(cmd::FormatDuration(45), std::wstring(L"45 секунд"));
  CHECK_EQ(cmd::FormatDuration(90), std::wstring(L"1 минуту 30 секунд"));
  CHECK_EQ(cmd::FormatDuration(86400 + 5 * 3600), std::wstring(L"1 день 5 часов"));
  CHECK_EQ(cmd::FormatClock(7, 5), std::wstring(L"07:05"));
}

TEST(Cmd_Shutdown) {
  auto r = P(L"выключить через 30 минут");
  CHECK(r.ok && r.c.kind == Kind::Shutdown && r.c.delaySec == 1800);
  r = P(L"выключи комп через час");
  CHECK(r.ok && r.c.kind == Kind::Shutdown && r.c.delaySec == 3600);
  r = P(L"выключение через 1 ч 20 мин");
  CHECK(r.ok && r.c.kind == Kind::Shutdown && r.c.delaySec == 4800);
  r = P(L"выключить через полчаса");
  CHECK(r.ok && r.c.delaySec == 1800);
  r = P(L"Выключить компьютер через полтора часа");
  CHECK(r.ok && r.c.kind == Kind::Shutdown && r.c.delaySec == 5400);
  r = P(L"shutdown in 45 min");
  CHECK(r.ok && r.c.kind == Kind::Shutdown && r.c.delaySec == 2700);
  r = P(L"shutdown 30m");
  CHECK(r.ok && r.c.kind == Kind::Shutdown && r.c.delaySec == 1800);
  r = P(L"выключить через 15");
  CHECK(r.ok && r.c.delaySec == 900);
  r = P(L"выключить в 23:30", 23 * 3600);
  CHECK(r.ok && r.c.kind == Kind::Shutdown && r.c.atClock && r.c.delaySec == 1800);
  r = P(L"выключить в 11:00", kNoon);  // passed -> tomorrow
  CHECK(r.ok && r.c.delaySec == 23 * 3600);
  r = P(L"выключить");
  CHECK(r.ok && r.c.kind == Kind::Shutdown && r.c.delaySec == -1);
  r = P(L"перезагрузить через 10 минут");
  CHECK(r.ok && r.c.kind == Kind::Restart && r.c.delaySec == 600);
  r = P(L"reboot in 1h");
  CHECK(r.ok && r.c.kind == Kind::Restart && r.c.delaySec == 3600);
  r = P(L"отменить выключение");
  CHECK(r.ok && r.c.kind == Kind::CancelShutdown);
  r = P(L"cancel shutdown");
  CHECK(r.ok && r.c.kind == Kind::CancelShutdown);
  CHECK(!P(L"выключить через 4000 дней").ok);  // > shutdown.exe limit
  CHECK(!P(L"выключить через").ok);
  CHECK(!P(L"выключить чайник").ok);
}

TEST(Cmd_PowerAndSession) {
  CHECK(P(L"сон").ok && P(L"сон").c.kind == Kind::Sleep);
  CHECK(P(L"спящий режим").c.kind == Kind::Sleep);
  CHECK(P(L"sleep").c.kind == Kind::Sleep);
  auto r = P(L"сон через 20 минут");
  CHECK(r.ok && r.c.kind == Kind::Sleep && r.c.delaySec == 1200);
  CHECK(P(L"гибернация").c.kind == Kind::Hibernate);
  CHECK(P(L"hibernate").c.kind == Kind::Hibernate);
  CHECK(P(L"заблокировать").c.kind == Kind::Lock);
  CHECK(P(L"блокировка").c.kind == Kind::Lock);
  CHECK(P(L"lock").c.kind == Kind::Lock);
  CHECK(P(L"lock pc").c.kind == Kind::Lock);
  CHECK(P(L"выйти из системы").c.kind == Kind::SignOut);
  CHECK(P(L"log off").c.kind == Kind::SignOut);
  CHECK(P(L"sign out").c.kind == Kind::SignOut);
  CHECK(P(L"очистить корзину").c.kind == Kind::EmptyRecycleBin);
  CHECK(P(L"empty recycle bin").c.kind == Kind::EmptyRecycleBin);
  CHECK(P(L"выключить монитор").c.kind == Kind::MonitorOff);
  CHECK(P(L"monitor off").c.kind == Kind::MonitorOff);
  CHECK(P(L"заставка").c.kind == Kind::Screensaver);
  CHECK(!P(L"очистить корзину через 5 минут").ok);  // no delay for this one
}

TEST(Cmd_Timers) {
  auto r = P(L"таймер 5 минут");
  CHECK(r.ok && r.c.kind == Kind::Timer && r.c.delaySec == 300 && r.c.label.empty());
  r = P(L"напомни через 10 минут выключить чайник");
  CHECK(r.ok && r.c.kind == Kind::Timer && r.c.delaySec == 600);
  CHECK_EQ(r.c.label, std::wstring(L"выключить чайник"));
  r = P(L"напомни выключить чайник через 10 минут");
  CHECK(r.ok && r.c.kind == Kind::Timer && r.c.delaySec == 600);
  CHECK_EQ(r.c.label, std::wstring(L"выключить чайник"));
  r = P(L"timer 25m");
  CHECK(r.ok && r.c.kind == Kind::Timer && r.c.delaySec == 1500);
  r = P(L"remind me in 10 minutes to call mom");
  CHECK(r.ok && r.c.kind == Kind::Timer && r.c.delaySec == 600);
  CHECK_EQ(r.c.label, std::wstring(L"call mom"));
  r = P(L"таймер");
  CHECK(r.ok && r.c.kind == Kind::Timer && r.c.delaySec == -1);
  r = P(L"напомни в 18:00 позвонить маме", 17 * 3600);
  CHECK(r.ok && r.c.kind == Kind::Timer && r.c.delaySec == 3600);
  CHECK_EQ(r.c.label, std::wstring(L"позвонить маме"));
  CHECK(P(L"отменить таймер").c.kind == Kind::CancelTimer);
  CHECK(P(L"cancel timers").c.kind == Kind::CancelTimer);
}

TEST(Cmd_Utilities) {
  auto r = P(L"пароль");
  CHECK(r.ok && r.c.kind == Kind::Password && r.c.length == 0);
  r = P(L"password 20");
  CHECK(r.ok && r.c.kind == Kind::Password && r.c.length == 20);
  r = P(L"сгенерируй пароль 32");
  CHECK(r.ok && r.c.kind == Kind::Password && r.c.length == 32);
  CHECK(!P(L"пароль 2").ok);
  CHECK(P(L"uuid").c.kind == Kind::Uuid);
  CHECK(P(L"GUID").c.kind == Kind::Uuid);
  CHECK(P(L"мой ip").c.kind == Kind::Ip);
  CHECK(P(L"ip").c.kind == Kind::Ip);
}

TEST(Cmd_NotCommands) {
  CHECK(!P(L"chrome").ok);
  CHECK(!P(L"2+2").ok);
  CHECK(!P(L"100 usd").ok);
  CHECK(!P(L"открыть папку проектов").ok);
  CHECK(!P(L"выкл").ok);  // partial -> handled by Suggest
}

TEST(Cmd_Suggest) {
  std::vector<std::pair<Kind, float>> s;
  cmd::Suggest(L"выкл", s);
  bool shutdown = false;
  for (auto& [k, v] : s) shutdown = shutdown || (k == Kind::Shutdown && v > 0.8f);
  CHECK(shutdown);
  s.clear();
  cmd::Suggest(L"забл", s);
  CHECK(!s.empty() && s[0].first == Kind::Lock);
  s.clear();
  cmd::Suggest(L"slee", s);
  CHECK(!s.empty() && s[0].first == Kind::Sleep);
  s.clear();
  cmd::Suggest(L"ыдууз", s);  // "sleep" in the wrong layout
  CHECK(!s.empty() && s[0].first == Kind::Sleep);
  s.clear();
  cmd::Suggest(L"x", s);
  CHECK(s.empty());
  s.clear();
  cmd::Suggest(L"photoshop", s);
  CHECK(s.empty());
}

TEST(Cmd_Stemming) {
  CHECK_EQ(cmd::Stem(L"проектов"), std::wstring(L"проект"));
  CHECK_EQ(cmd::Stem(L"Проекты"), std::wstring(L"проект"));
  CHECK_EQ(cmd::Stem(L"проект"), std::wstring(L"проект"));
  CHECK_EQ(cmd::Stem(L"загрузки"), std::wstring(L"загрузк"));
  CHECK_EQ(cmd::Stem(L"downloads"), std::wstring(L"download"));
  CHECK(cmd::StemMatch(L"проектов", L"проекты"));
  CHECK(cmd::StemMatch(L"загрузок", L"загрузки"));
  CHECK(cmd::StemMatch(L"загрузку", L"загрузки"));
  CHECK(cmd::StemMatch(L"документов", L"документы"));
  CHECK(cmd::StemMatch(L"картинок", L"картинки"));
  CHECK(cmd::StemMatch(L"изображений", L"изображения"));
  CHECK(cmd::StemMatch(L"музыку", L"музыка"));
  CHECK(cmd::StemMatch(L"рабочего", L"рабочий"));
  CHECK(cmd::StemMatch(L"стола", L"стол"));
  CHECK(cmd::StemMatch(L"корзину", L"корзина"));
  CHECK(cmd::StemMatch(L"автозагрузку", L"автозагрузка"));
  CHECK(cmd::StemMatch(L"Downloads", L"download"));
  CHECK(!cmd::StemMatch(L"музей", L"музыка"));
  CHECK(!cmd::StemMatch(L"проектирование", L"проекты"));
  CHECK(!cmd::StemMatch(L"видео", L"виза"));
}

TEST(Cmd_FolderPhrases) {
  cmd::FolderPhrase f;
  CHECK(cmd::ParseFolderPhrase(L"открыть папку проектов", f) && f.explicitPhrase);
  CHECK_EQ(f.target, std::wstring(L"проектов"));
  CHECK(cmd::ParseFolderPhrase(L"открой папку загрузки", f) && f.explicitPhrase && f.target == L"загрузки");
  CHECK(cmd::ParseFolderPhrase(L"папка документы", f) && f.explicitPhrase && f.target == L"документы");
  CHECK(cmd::ParseFolderPhrase(L"проекты", f) && !f.explicitPhrase && f.target == L"проекты");
  CHECK(cmd::ParseFolderPhrase(L"open projects folder", f) && f.explicitPhrase && f.target == L"projects");
  CHECK(cmd::ParseFolderPhrase(L"open downloads", f) && f.explicitPhrase && f.target == L"downloads");
  CHECK(cmd::ParseFolderPhrase(L"перейти в загрузки", f) && f.explicitPhrase && f.target == L"загрузки");
  CHECK(cmd::ParseFolderPhrase(L"go to desktop", f) && f.target == L"desktop");
  CHECK(cmd::ParseFolderPhrase(L"открыть рабочий стол", f) && f.target == L"рабочий стол");
  CHECK(!cmd::ParseFolderPhrase(L"открыть папку", f));

  CHECK_EQ(cmd::FolderNameScore(L"проектов", L"проекты"), 1.f);
  CHECK_EQ(cmd::FolderNameScore(L"projects", L"Projects"), 1.f);
  CHECK_EQ(cmd::FolderNameScore(L"загрузок", L"загрузки"), 1.f);
  CHECK_EQ(cmd::FolderNameScore(L"рабочего стола", L"рабочий стол"), 1.f);
  CHECK_EQ(cmd::FolderNameScore(L"мои документы", L"документы"), 1.f);
  CHECK(cmd::FolderNameScore(L"проек", L"проекты") > 0.85f);  // typing in progress
  CHECK(cmd::FolderNameScore(L"музей", L"музыка") < 0.6f);
  CHECK(cmd::FolderNameScore(L"chrome", L"загрузки") == 0.f);

  // every built-in folder is reachable by its first name and by an inflected form of it
  for (const auto& b : cmd::BuiltinFolders()) {
    std::wstring first(b.names, std::wstring_view(b.names).find(L'|'));
    CHECK(cmd::FolderNameScore(first, first) == 1.f);
  }
}

TEST(Web_UrlDetection) {
  std::wstring u;
  CHECK(web::LooksLikeUrl(L"github.com", u) && u == L"https://github.com");
  CHECK(web::LooksLikeUrl(L"https://example.org/a?b=c", u) && u == L"https://example.org/a?b=c");
  CHECK(web::LooksLikeUrl(L"localhost:3000", u) && u == L"http://localhost:3000");
  CHECK(web::LooksLikeUrl(L"localhost:3000/api", u) && u == L"http://localhost:3000/api");
  CHECK(web::LooksLikeUrl(L"192.168.1.1", u) && u == L"http://192.168.1.1");
  CHECK(web::LooksLikeUrl(L"ya.ru/search", u));
  CHECK(web::LooksLikeUrl(L"www.something.weird", u));
  CHECK(web::LooksLikeUrl(L"президент.рф", u));
  CHECK(!web::LooksLikeUrl(L"readme.md", u));
  CHECK(!web::LooksLikeUrl(L"file.txt", u));
  CHECK(!web::LooksLikeUrl(L"1.5", u));
  CHECK(!web::LooksLikeUrl(L"2.5*3", u));
  CHECK(!web::LooksLikeUrl(L"hello world.com", u));
  CHECK(!web::LooksLikeUrl(L"me@mail.ru", u));
  CHECK(!web::LooksLikeUrl(L"chrome", u));
  CHECK(!web::LooksLikeUrl(L"1.2.3.256", u));
}

TEST(Web_Build) {
  CHECK_EQ(web::UrlEncode(L"a b&c"), std::wstring(L"a%20b%26c"));
  CHECK_EQ(web::UrlEncode(L"я"), std::wstring(L"%D1%8F"));
  auto s = web::Build(L"погода москва", L"https://www.google.com/search?q={q}");
  CHECK(s.score < 0.05f && !s.prefixed);
  CHECK_EQ(s.url, std::wstring(L"https://www.google.com/search?q=%D0%BF%D0%BE%D0%B3%D0%BE%D0%B4%D0%B0%20%D0%BC%D0%BE%D1%"
                               L"81%D0%BA%D0%B2%D0%B0"));
  CHECK_EQ(s.title, std::wstring(L"Искать «погода москва» в интернете"));
  s = web::Build(L"yt lofi", L"x{q}");
  CHECK(s.prefixed && s.score > 0.9f);
  CHECK_EQ(s.url, std::wstring(L"https://www.youtube.com/results?search_query=lofi"));
  CHECK_EQ(s.title, std::wstring(L"Искать «lofi» на YouTube"));
  s = web::Build(L"я котики", L"x{q}");
  CHECK(s.prefixed && s.url.find(L"yandex.ru") != std::wstring::npos);
  s = web::Build(L"gh cloud spotlight", L"x{q}");
  CHECK(s.url == L"https://github.com/search?q=cloud%20spotlight");
  s = web::Build(L"вики Москва", L"x{q}");
  CHECK(s.url.find(L"ru.wikipedia.org") != std::wstring::npos);
  s = web::Build(L"tr привет", L"x{q}");
  CHECK(s.url.find(L"tl=en") != std::wstring::npos);
  s = web::Build(L"перевод hello", L"x{q}");
  CHECK(s.url.find(L"tl=ru") != std::wstring::npos && s.title == L"Перевести «hello»");
  s = web::Build(L"github.com", L"x{q}");
  CHECK(s.isUrl && s.score > 0.96f && s.title == L"Открыть https://github.com");
  s = web::Build(L"g", L"https://s/?q={q}");  // prefix alone is a normal query
  CHECK(!s.prefixed && s.url == L"https://s/?q=g");
  s = web::Build(L"test", L"https://s/?q=");  // template without {q}
  CHECK_EQ(s.url, std::wstring(L"https://s/?q=test"));
}
