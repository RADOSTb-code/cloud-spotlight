#pragma once
// Fallback web search (always last), engine prefixes ("g", "y"/"я", "yt", "gh", "w"/"вики", "tr"/"перевод")
// and "open this URL" for things that look like a domain/URL. Logic lives in logic/CommandParse (cs::web).
#include <string>
#include <vector>

#include "core/Types.h"

namespace cs {

class WebSearchProvider final : public IProvider {
 public:
  WebSearchProvider() = default;
  const wchar_t* Id() const override { return L"web"; }
  void Init(const Config& cfg, IHost& host) override;
  void Search(std::wstring_view query, std::vector<Result>& out) override;
  ExecResult Execute(const Result& r, Action a) override;

 private:
  IHost* host_ = nullptr;
  std::wstring template_;
};

}  // namespace cs
