#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>

#include "Test.h"
#include "core/Fuzzy.h"
#include "logic/FileIndex.h"

using namespace cs;

namespace {

FileIndex MakeSmall() {
  FileIndex idx(L'\\');
  uint32_t r = idx.AddRoot(L"C:\\Users\\me");
  uint32_t docs = idx.Add(r, L"Documents", true, 100);
  idx.Add(docs, L"README.md", false, 100);
  idx.Add(docs, L"Отчёт за май.docx", false, 100);
  uint32_t proj = idx.Add(docs, L"Projects", true, 100);
  uint32_t cs = idx.Add(proj, L"CloudSpotlight", true, 100);
  idx.Add(cs, L"readme_old.txt", false, 100);
  idx.Add(cs, L"main.cpp", false, 100);
  uint32_t r2 = idx.AddRoot(L"D:\\");
  idx.Add(r2, L"Games", true, 100);
  idx.Finish();
  return idx;
}

std::vector<FileIndex::Hit> Find(const FileIndex& idx, const wchar_t* q, std::vector<FileIndex::Hit>* dirs = nullptr) {
  std::vector<FileIndex::Hit> f, d;
  idx.Search(fuzzy::Prepare(q), {}, f, d);
  if (dirs) *dirs = d;
  return f;
}

}  // namespace

TEST(FileIndex_PathsAndLayout) {
  auto idx = MakeSmall();
  CHECK_EQ(idx.Size(), size_t(8));
  CHECK_EQ(idx.FullPath(5), std::wstring(L"C:\\Users\\me\\Documents\\Projects\\CloudSpotlight\\readme_old.txt"));
  CHECK_EQ(idx.ParentPath(1), std::wstring(L"C:\\Users\\me\\Documents"));
  CHECK_EQ(idx.FullPath(7), std::wstring(L"D:\\Games"));  // no doubled separator after "D:\"
  CHECK_EQ(idx.Depth(0), 1);
  CHECK_EQ(idx.Depth(5), 4);
  CHECK(idx.IsDir(3));
  CHECK(!idx.IsDir(1));
}

TEST(FileIndex_SearchBasics) {
  auto idx = MakeSmall();
  auto f = Find(idx, L"readme");
  CHECK(f.size() == 2);
  if (f.size() == 2) {
    CHECK_EQ(std::wstring(idx.Name(f[0].index)), std::wstring(L"README.md"));  // stem match = exact
    CHECK(f[0].score > f[1].score);
  }
  CHECK_EQ(Find(idx, L"отчет").size(), size_t(1));         // ё folded
  CHECK_EQ(Find(idx, L"jnxtn").size(), size_t(1));         // wrong layout
  CHECK_EQ(Find(idx, L"main.cpp").size(), size_t(1));      // query with '.' matches the full name
  std::vector<FileIndex::Hit> dirs;
  CHECK(Find(idx, L"cloudsp", &dirs).empty());
  CHECK_EQ(dirs.size(), size_t(1));
  CHECK(Find(idx, L"zzzz").empty());
}

TEST(FileIndex_PathQuery) {
  CHECK(pathq::LooksLikePath(L"C:\\"));
  CHECK(pathq::LooksLikePath(L"c:/users"));
  CHECK(pathq::LooksLikePath(L"\\\\server\\share"));
  CHECK(pathq::LooksLikePath(L"%appdata%\\Code"));
  CHECK(pathq::LooksLikePath(L"%TEMP%"));
  CHECK(pathq::LooksLikePath(L"~\\Downloads"));
  CHECK(pathq::LooksLikePath(L"~"));
  CHECK(!pathq::LooksLikePath(L"c:"));
  CHECK(!pathq::LooksLikePath(L"50% of 10"));
  CHECK(!pathq::LooksLikePath(L"~ab"));
  CHECK(!pathq::LooksLikePath(L"notepad"));
  auto [d, p] = pathq::SplitDirAndPrefix(L"C:\\Users\\Do");
  CHECK_EQ(std::wstring(d), std::wstring(L"C:\\Users\\"));
  CHECK_EQ(std::wstring(p), std::wstring(L"Do"));
}

TEST(FileIndex_Days) {
  // 2024-01-01 00:00 UTC as FILETIME = 133485408000000000
  CHECK_EQ(int(FileIndex::DaysFromFileTime(133485408000000000ull)), 8766);
  CHECK_EQ(int(FileIndex::DaysFromFileTime(0)), 0);
}

namespace {

// realistic = pseudo-words from random syllables (diverse names); otherwise 20 recurring words (worst case: most
// names pass the character prefilter and share prefixes).
FileIndex MakeSynthetic(bool realistic) {
  static const wchar_t* words[] = {L"report", L"Invoice", L"photo", L"IMG", L"backup", L"project", L"notes",
                                   L"Отчёт", L"договор", L"счёт", L"draft", L"final", L"setup", L"config",
                                   L"data", L"Screenshot", L"music", L"video", L"резюме", L"budget"};
  static const wchar_t* syll[] = {L"ka", L"lo", L"mi", L"ne", L"tor", L"vi", L"sa", L"pre", L"un", L"de", L"ex",
                                  L"ma", L"ко", L"ра", L"ти", L"ст", L"zu", L"qui", L"bel", L"gr", L"ost", L"ap",
                                  L"ion", L"ер", L"ном", L"fy", L"wh", L"ch", L"ing", L"ly"};
  static const wchar_t* exts[] = {L".docx", L".pdf", L".jpg", L".png", L".txt", L".xlsx", L".mp4", L".cpp", L".zip"};
  std::mt19937 rng(42);
  FileIndex idx;
  idx.Reserve(200000, 200000 * 24);
  uint32_t root = idx.AddRoot(L"C:\\Users\\me");
  std::vector<uint32_t> dirsRefs{root};
  std::wstring name;
  for (int i = 0; i < 200000; ++i) {
    name.clear();
    if (realistic) {
      int nw = 1 + int(rng() % 3);
      for (int w = 0; w < nw; ++w) {
        if (w) name += (rng() % 2) ? L' ' : L'_';
        int ns = 2 + int(rng() % 3);
        for (int s = 0; s < ns; ++s) name += syll[rng() % std::size(syll)];
        if (rng() % 3 == 0 && name.back() >= L'a' && name.back() <= L'z') name.back() = wchar_t(name.back() - 32);
      }
      if (rng() % 4 == 0) name += std::to_wstring(rng() % 1000);
    } else {
      name += words[rng() % std::size(words)];
      name += L'_';
      name += words[rng() % std::size(words)];
      name += std::to_wstring(rng() % 10000);
    }
    bool dir = rng() % 10 == 0;
    if (!dir) name += exts[rng() % std::size(exts)];
    uint32_t parent = dirsRefs[rng() % dirsRefs.size()];
    uint32_t ref = idx.Add(parent, name, dir, uint16_t(9000 + rng() % 400));
    if (dir && idx.Depth(ref) < 8) dirsRefs.push_back(ref);
  }
  idx.Finish();
  return idx;
}

void RunBenchmark(bool realistic, std::initializer_list<const wchar_t*> queries) {
  FileIndex idx = MakeSynthetic(realistic);
  std::printf("    %s index: %zu entries, %.1f MB (wchar_t = %zu bytes)\n", realistic ? "realistic" : "worst-case",
              idx.Size(), double(idx.MemoryBytes()) / (1024.0 * 1024.0), sizeof(wchar_t));
  FileIndex::SearchOptions opt;
  opt.today = 9400;
  for (const wchar_t* qs : queries) {
    auto q = fuzzy::Prepare(qs);
    std::vector<FileIndex::Hit> f, d;
    double bestMs = 1e9;
    for (int r = 0; r < 15; ++r) {
      f.clear();
      d.clear();
      auto t0 = std::chrono::steady_clock::now();
      idx.Search(q, opt, f, d);
      bestMs = std::min(bestMs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::printf("    query %-14s %6.3f ms  (%zu files, %zu dirs)\n", str::WideToUtf8(qs).c_str(), bestMs, f.size(),
                d.size());
    // Brute-force reference: the bounded scorer must not lose the best match, and every hit's score must equal
    // the reference fuzzy score * 0.96 + depth/recency bonus.
    auto stemOf = [&](uint32_t i) {
      std::wstring n(idx.Name(i));
      if (!idx.IsDir(i) && q.lower.find(L'.') == std::wstring::npos) {
        size_t dot = n.rfind(L'.');
        if (dot != std::wstring::npos && dot > 0) n.resize(dot);
      }
      return n;
    };
    float best = 0.f;
    for (uint32_t i = 0; i < idx.Size(); ++i) {
      if (idx.IsDir(i)) continue;
      float s = fuzzy::Score(q, stemOf(i));
      if (s >= opt.minScore && s > best) best = s;
    }
    CHECK_EQ(best > 0.f, !f.empty());
    if (!f.empty()) CHECK(f[0].score >= best * 0.96f - 1e-5f);
    for (auto* hits : {&f, &d})
      for (const auto& h : *hits) {
        float bonus = 0.002f * float(10 - std::min(idx.Depth(h.index), 10));
        int age = opt.today - idx.Days(h.index);
        if (age >= 0 && age < 30) bonus += 0.02f * float(30 - age) / 30.f;
        CHECK_NEAR(h.score, fuzzy::Score(q, stemOf(h.index)) * 0.96f + bonus, 1e-4);
      }
  }
}

}  // namespace

TEST(FileIndex_Benchmark200k) {
  RunBenchmark(true, {L"ka", L"pre", L"kalo", L"mine tor", L"кора", L"rjhf", L"vsq", L"xyz", L"whing"});
  RunBenchmark(false, {L"re", L"report", L"inv2024", L"отчет", L"ghjtrn", L"scrsh", L"budget_final", L"xyz"});
}
