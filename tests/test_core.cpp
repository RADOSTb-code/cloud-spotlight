#include <memory>

#include "Test.h"
#include "core/Config.h"
#include "core/Fuzzy.h"
#include "core/Json.h"
#include "core/SearchEngine.h"
#include "core/Str.h"

using namespace cs;

TEST(Str_LowerCyrillic) {
  CHECK_EQ(str::ToLower(L"ПРИВЕТ Ёж World"), std::wstring(L"привет еж world"));
}

TEST(Str_Utf8RoundTrip) {
  std::wstring w = L"Привет, мир! € 😀";
  CHECK_EQ(str::Utf8ToWide(str::WideToUtf8(w)), w);
}

TEST(Str_SwapLayout) {
  CHECK_EQ(str::SwapLayout(L"ntktuhfv"), std::wstring(L"телеграм"));
  CHECK_EQ(str::SwapLayout(L"руддщ"), std::wstring(L"hello"));
}

TEST(Str_FormatNumber) {
  CHECK_EQ(str::FormatNumber(0.1 + 0.2), std::wstring(L"0.3"));
  CHECK_EQ(str::FormatNumber(1234567.5, 2, true, L','), std::wstring(L"1 234 567,5"));
  CHECK_EQ(str::FormatNumber(-42), std::wstring(L"-42"));
}

TEST(Fuzzy_Ranking) {
  auto q = fuzzy::Prepare(L"code");
  float exact = fuzzy::Score(q, L"Code");
  float prefix = fuzzy::Score(q, L"Codex Tool");
  float word = fuzzy::Score(q, L"Visual Studio Code");
  float sub = fuzzy::Score(q, L"Unicoder");
  float seq = fuzzy::Score(q, L"Cobalt Dense");
  CHECK(exact > prefix);
  CHECK(prefix > word);
  CHECK(word > sub);
  CHECK(sub > seq);
  CHECK(seq > 0);
  CHECK_EQ(fuzzy::Score(q, L"Notepad"), 0.f);
}

TEST(Fuzzy_AcronymAndLayout) {
  CHECK(fuzzy::Score(fuzzy::Prepare(L"vsc"), L"Visual Studio Code") >= 0.75f);
  CHECK(fuzzy::Score(fuzzy::Prepare(L"ntktuhfv"), L"Телеграм") > 0.85f);
  CHECK(fuzzy::Score(fuzzy::Prepare(L"сркщьу"), L"Chrome") > 0.85f);
  CHECK(fuzzy::Score(fuzzy::Prepare(L"ps"), L"PowerShell") >= 0.75f);
}

TEST(Json_ParseSerialize) {
  json::Value v;
  CHECK(json::Parse(R"({ // comment
    "a": 1.5, "s": "Привет", "arr": [1, 2, 3,], "o": {"t": true, "n": null}, })", v));
  CHECK_NEAR(v["a"].AsNumber(), 1.5, 1e-12);
  CHECK_EQ(v["s"].AsString(), std::wstring(L"Привет"));
  CHECK_EQ(v["arr"].Size(), size_t(3));
  CHECK(v["o"]["t"].AsBool());
  CHECK(v["o"]["n"].IsNull());
  CHECK(v["missing"]["deep"].IsNull());
  json::Value back;
  CHECK(json::Parse(json::Serialize(v), back));
  CHECK_EQ(back["s"].AsString(), std::wstring(L"Привет"));
  json::Value bad;
  CHECK(!json::Parse("{\"a\": }", bad));
}

namespace {
struct FakeProvider : IProvider {
  const wchar_t* Id() const override { return L"fake"; }
  void Search(std::wstring_view, std::vector<Result>& out) override {
    for (int i = 0; i < 3; ++i) {
      Result r;
      r.title = L"item" + std::to_wstring(i);
      r.key = L"k" + std::to_wstring(i);
      r.score = 0.5f - 0.01f * float(i);
      out.push_back(r);
    }
    Result dup;
    dup.key = L"k0";
    dup.score = 0.1f;
    out.push_back(dup);
  }
  ExecResult Execute(const Result&, Action) override { return {}; }
};
}  // namespace

TEST(Engine_RankDedupeLearn) {
  SearchEngine e(L"");
  e.Add(std::make_unique<FakeProvider>());
  auto& r = e.Query(L"  it ");
  CHECK_EQ(r.size(), size_t(3));
  CHECK_EQ(r[0].key, std::wstring(L"k0"));
  CHECK_EQ(r[0].provider, 0);
  // choose the third item for "it" -> it must become first
  e.Execute(2, Action::Open);
  auto& r2 = e.Query(L"it");
  CHECK_EQ(r2[0].key, std::wstring(L"k2"));
  CHECK(e.Query(L"   ").empty());
}
