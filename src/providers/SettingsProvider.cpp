#include "providers/SettingsProvider.h"

#include <string>

#include "core/Fuzzy.h"
#include "platform/Win.h"

namespace cs {
namespace {

constexpr const wchar_t* kCategory = L"Настройки";
constexpr float kWeight = 0.9f;
constexpr size_t kMaxResults = 6;

std::wstring KeyOf(const settings::Entry& e) {
  std::wstring k = L"set:";
  k += e.target;
  if (*e.args) k.append(L" ").append(e.args);
  return k;
}

const settings::Entry* FindEntry(std::wstring_view key) {
  for (const auto& e : settings::Catalog())
    if (key == KeyOf(e)) return &e;
  return nullptr;
}

}  // namespace

void SettingsProvider::Init(const Config&, IHost& host) {
  host_ = &host;
  // Warm up the lowercase cache off the first keystroke.
  matches_.reserve(16);
  settings::Search(fuzzy::Prepare(L"a"), matches_, 1);
  matches_.clear();
}

void SettingsProvider::Search(std::wstring_view query, std::vector<Result>& out) {
  matches_.clear();
  settings::Search(fuzzy::Prepare(query), matches_, kMaxResults);
  for (const auto& m : matches_) {
    const settings::Entry& e = *m.entry;
    Result r;
    r.title = e.ru;
    r.subtitle = e.IsPage() ? std::wstring(L"Параметры › ") + e.section : std::wstring(L"Системный инструмент");
    r.category = kCategory;
    r.iconKind = IconKind::Glyph;
    r.icon = e.glyph;
    r.score = m.score * kWeight;
    r.actions = uint8_t(kActOpen | kActCopy | (e.admin ? kActAdmin : 0));
    r.payload = e.target;
    r.key = KeyOf(e);
    r.copyText = *e.args ? r.payload + L" " + e.args : r.payload;
    out.push_back(std::move(r));
  }
}

ExecResult SettingsProvider::Execute(const Result& r, Action a) {
  // Look the entry up again so args come from the catalog (payload holds only the target).
  const settings::Entry* e = FindEntry(r.key);
  std::wstring_view args = e ? std::wstring_view(e->args) : std::wstring_view();
  switch (a) {
    case Action::Open:
    case Action::Reveal:
      win::ShellOpen(r.payload, args, false);
      break;
    case Action::RunAsAdmin:
      win::ShellOpen(r.payload, args, e && e->admin);
      break;
    case Action::Copy:
      if (host_) host_->CopyToClipboard(r.copyText);
      break;
  }
  return {};
}

}  // namespace cs
