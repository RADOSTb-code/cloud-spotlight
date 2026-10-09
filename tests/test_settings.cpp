#include <set>
#include <string>

#include "Test.h"
#include "core/Fuzzy.h"
#include "logic/SettingsCatalog.h"

using namespace cs;

namespace {
std::wstring Top(const wchar_t* query) {
  std::vector<settings::Match> m;
  settings::Search(fuzzy::Prepare(query), m);
  return m.empty() ? std::wstring() : std::wstring(m[0].entry->target);
}
}  // namespace

TEST(Settings_CatalogSanity) {
  auto cat = settings::Catalog();
  CHECK(cat.size() >= 90);
  std::set<std::wstring> keys;
  for (const auto& e : cat) {
    CHECK(e.target && *e.target);
    CHECK(e.args != nullptr);
    CHECK(e.ru && *e.ru);
    CHECK(e.en && *e.en);
    CHECK(e.keywords && *e.keywords);
    CHECK(e.glyph && *e.glyph);
    std::wstring t = e.target;
    CHECK_EQ(e.IsPage(), t.rfind(L"ms-settings:", 0) == 0);
    bool fresh = keys.insert(t + L" " + e.args).second;
    if (!fresh) std::printf("    duplicate: %s\n", str::WideToUtf8(t).c_str());
    CHECK(fresh);
  }
}

TEST(Settings_Queries) {
  CHECK_EQ(Top(L"яркость"), std::wstring(L"ms-settings:display"));
  CHECK_EQ(Top(L"дисплей"), std::wstring(L"ms-settings:display"));
  CHECK_EQ(Top(L"блютуз"), std::wstring(L"ms-settings:bluetooth"));
  CHECK_EQ(Top(L"bluetooth"), std::wstring(L"ms-settings:bluetooth"));
  CHECK_EQ(Top(L"автозагрузка"), std::wstring(L"ms-settings:startupapps"));
  CHECK_EQ(Top(L"wifi"), std::wstring(L"ms-settings:network-wifi"));
  CHECK_EQ(Top(L"вай фай"), std::wstring(L"ms-settings:network-wifi"));
  CHECK_EQ(Top(L"реестр"), std::wstring(L"regedit"));
  CHECK_EQ(Top(L"диспетчер устройств"), std::wstring(L"devmgmt.msc"));
  CHECK_EQ(Top(L"переменные среды"), std::wstring(L"rundll32.exe"));
  CHECK_EQ(Top(L"обои"), std::wstring(L"ms-settings:personalization-background"));
  CHECK_EQ(Top(L"Обновления"), std::wstring(L"ms-settings:windowsupdate"));
  CHECK_EQ(Top(L"night light"), std::wstring(L"ms-settings:nightlight"));
  CHECK_EQ(Top(L"vpn"), std::wstring(L"ms-settings:network-vpn"));
  CHECK_EQ(Top(L"мышь"), std::wstring(L"ms-settings:mousetouchpad"));
  // wrong keyboard layout: "dfq afq" typed for "вай фай"
  CHECK_EQ(Top(L"dfq afq"), std::wstring(L"ms-settings:network-wifi"));
  // nonsense finds nothing
  std::vector<settings::Match> m;
  settings::Search(fuzzy::Prepare(L"zzqxj"), m);
  CHECK(m.empty());
}

TEST(Settings_MaxResults) {
  std::vector<settings::Match> m;
  settings::Search(fuzzy::Prepare(L"с"), m, 5, 0.f);
  CHECK(m.size() <= 5);
  for (size_t i = 1; i < m.size(); ++i) CHECK(m[i - 1].score >= m[i].score);
}
