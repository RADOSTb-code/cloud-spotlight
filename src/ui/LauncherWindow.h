#pragma once
// The Spotlight-style launcher window: borderless acrylic popup with a search field and a results list.
// Everything is created up-front in Create() (hidden) so Show() is instant. UI thread only.
#include <windows.h>

#include <memory>

namespace cs {

class SearchEngine;
class IHost;
struct Config;

namespace ui {
class LauncherImpl;
}

class LauncherWindow {
 public:
  LauncherWindow(SearchEngine& engine, IHost& host);
  ~LauncherWindow();
  LauncherWindow(const LauncherWindow&) = delete;
  LauncherWindow& operator=(const LauncherWindow&) = delete;

  bool Create(HINSTANCE inst, const Config& cfg);  // creates the hidden window and D2D resources up-front
  void Toggle();                                   // hotkey
  void Show();
  void Hide();
  bool IsVisible() const;
  void Refresh();                       // UI thread: re-run the current query (host calls it on RequestRefresh)
  void ApplyConfig(const Config& cfg);  // theme / backdrop / visibleRows changed
  void OnSystemThemeChanged();          // WM_SETTINGCHANGE "ImmersiveColorSet"
  HWND Hwnd() const;

 private:
  std::unique_ptr<ui::LauncherImpl> impl_;
};

}  // namespace cs
