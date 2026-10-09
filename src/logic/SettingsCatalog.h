#pragma once
// Static catalog of Windows 11 Settings pages (ms-settings:) and classic system tools, plus a portable search.
// No Windows headers: unit-tested on Linux. SettingsProvider is a thin Win32 wrapper around this.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/Fuzzy.h"

namespace cs::settings {

struct Entry {
  const wchar_t* target;    // "ms-settings:display" or an executable ("taskmgr", "devmgmt.msc", "rundll32.exe")
  const wchar_t* args;      // command-line arguments for tools (L"" for none)
  const wchar_t* ru;        // Russian title (shown)
  const wchar_t* en;        // English title (matched only)
  const wchar_t* section;   // Russian section for the subtitle, nullptr for classic tools
  const wchar_t* keywords;  // lowercase phrases separated by '|', ru + en
  const wchar_t* glyph;     // Segoe Fluent Icons glyph
  bool admin;               // "run as administrator" is meaningful (msc snap-ins, regedit, consoles)

  bool IsPage() const { return section != nullptr; }
};

std::span<const Entry> Catalog();

struct Match {
  const Entry* entry;
  float score;  // 0..1 raw match quality (provider applies its own category weight)
};

// Appends up to `maxResults` best matches (score desc) to `out`. Allocation-free apart from `out` growth
// (lowercase copies of the catalog are built once, on first use).
void Search(const fuzzy::Query& q, std::vector<Match>& out, size_t maxResults = 8, float minScore = 0.3f);

}  // namespace cs::settings
