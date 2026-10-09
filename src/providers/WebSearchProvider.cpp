#include "providers/WebSearchProvider.h"

#include "core/Config.h"
#include "logic/CommandParse.h"
#include "platform/Win.h"

namespace cs {

void WebSearchProvider::Init(const Config& cfg, IHost& host) {
  host_ = &host;
  template_ = cfg.webSearch.empty() ? L"https://www.google.com/search?q={q}" : cfg.webSearch;
}

void WebSearchProvider::Search(std::wstring_view query, std::vector<Result>& out) {
  web::Search s = web::Build(query, template_);
  Result r;
  r.title = std::move(s.title);
  r.subtitle = s.url;
  r.category = L"Интернет";
  r.iconKind = IconKind::Glyph;
  r.icon = s.isUrl ? L"" : L"";
  r.score = s.score;
  r.actions = kActOpen | kActCopy;
  r.copyText = s.url;
  r.payload = std::move(s.url);
  out.push_back(std::move(r));
}

ExecResult WebSearchProvider::Execute(const Result& r, Action a) {
  if (a == Action::Copy) {
    if (host_) host_->CopyToClipboard(r.payload);
    return {};
  }
  win::ShellOpen(r.payload);
  return {};
}

}  // namespace cs
