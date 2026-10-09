#pragma once
// Runtime configuration. Loaded/saved by app/ConfigStore (JSON at %APPDATA%\CloudSpotlight\config.json).
// All paths here are already environment-expanded (%USERPROFILE% etc. resolved) by the loader.
#include <string>
#include <vector>

namespace cs {

struct FolderAlias {
  std::wstring name;   // e.g. L"проекты" -> matched by "открыть папку проектов", "проекты", "projects"
  std::wstring path;
};

struct CustomCommand {
  std::vector<std::wstring> keywords;  // e.g. {L"терминал", L"terminal", L"wt"}
  std::wstring title;                  // shown in the list
  std::wstring target;                 // exe / file / url / ms-settings: ...
  std::wstring args;
  bool admin = false;
};

struct Config {
  // Hotkeys: "Alt+Space", "Ctrl+Space", "Win+Shift+S", "Ctrl+Alt+K"...
  std::wstring hotkey = L"Alt+Space";
  std::wstring fallbackHotkey = L"Ctrl+Space";

  std::wstring theme = L"system";  // system | light | dark
  std::wstring backdrop = L"acrylic";  // acrylic | mica | none
  bool autostart = true;
  int maxResults = 30;   // total results kept after ranking
  int visibleRows = 8;   // rows visible without scrolling

  // File index
  std::vector<std::wstring> fileRoots;     // default: Desktop, Documents, Downloads, Pictures, Videos, Music, %USERPROFILE%\source, Projects
  std::vector<std::wstring> fileExclude;   // directory names: node_modules, .git, $Recycle.Bin, AppData, ...
  int fileMaxDepth = 8;
  int fileReindexMinutes = 15;
  int fileMaxEntries = 400000;

  std::vector<FolderAlias> folders;        // quick "open folder X" aliases
  std::vector<CustomCommand> commands;     // user-defined commands

  std::wstring currencyBase = L"RUB";      // default target currency ("100 usd" -> RUB)
  std::wstring currencyApi = L"https://open.er-api.com/v6/latest/USD";
  int currencyCacheHours = 6;

  std::wstring webSearch = L"https://www.google.com/search?q={q}";

  // Filled by the loader, not read from JSON
  std::wstring configPath;
  std::wstring dataDir;   // %APPDATA%\CloudSpotlight
};

}  // namespace cs
