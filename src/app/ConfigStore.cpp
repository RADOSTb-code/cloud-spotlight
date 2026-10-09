#include "app/ConfigStore.h"

#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>

#include "core/Str.h"
#include "platform/Win.h"

namespace cs {
namespace {

std::wstring UserProfile() { return win::ExpandEnv(L"%USERPROFILE%"); }

// "C:\Users\me\Projects" -> "%USERPROFILE%\Projects". Other paths (e.g. OneDrive-redirected folders) stay absolute.
std::wstring Unexpand(const std::wstring& path) {
  std::wstring home = UserProfile();
  if (home.empty() || home[0] == L'%') return path;
  if (path.size() > home.size() && path[home.size()] == L'\\' && str::IStartsWith(path, home))
    return L"%USERPROFILE%" + path.substr(home.size());
  if (str::IEquals(path, home)) return L"%USERPROFILE%";
  return path;
}

void PushUnique(std::vector<std::wstring>& v, std::wstring p) {
  if (p.empty()) return;
  for (auto& e : v)
    if (str::IEquals(e, p)) return;
  v.push_back(std::move(p));
}

bool ReadFileUtf8(const std::wstring& path, std::string& out, DWORD* err) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    *err = GetLastError();
    return false;
  }
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (16 << 20);
  if (ok) {
    out.resize(size_t(size.QuadPart));
    DWORD read = 0;
    ok = out.empty() || (::ReadFile(h, out.data(), DWORD(out.size()), &read, nullptr) && read == out.size());
  }
  *err = ok ? 0 : GetLastError();
  CloseHandle(h);
  return ok;
}

std::wstring Str(const json::Value& v, const std::wstring& def) { return v.IsString() ? v.AsString() : def; }

std::vector<std::wstring> StrList(const json::Value& v) {
  std::vector<std::wstring> r;
  if (v.IsString() && !v.AsString().empty()) r.push_back(v.AsString());
  for (auto& it : v.Items())
    if (it.IsString() && !str::Trim(it.AsString()).empty()) r.emplace_back(str::Trim(it.AsString()));
  return r;
}

json::Value StrArray(const std::vector<std::wstring>& v) {
  json::Value a = json::Value::Array();
  for (auto& s : v) a.Push(json::Value::String(s));
  return a;
}

std::wstring Lower(const std::wstring& s) { return str::ToLower(str::Trim(s)); }

// Overlays values present in `j` onto `c`. Wrong-typed values are ignored (default kept).
void Overlay(const json::Value& j, Config& c) {
  auto num = [&](const char* k, int& dst, int lo, int hi) {
    if (j[k].IsNumber()) dst = std::clamp(j[k].AsInt(dst), lo, hi);
  };
  c.hotkey = Str(j["hotkey"], c.hotkey);
  c.fallbackHotkey = Str(j["fallbackHotkey"], c.fallbackHotkey);
  std::wstring theme = Lower(Str(j["theme"], c.theme));
  if (theme == L"system" || theme == L"light" || theme == L"dark") c.theme = theme;
  std::wstring backdrop = Lower(Str(j["backdrop"], c.backdrop));
  if (backdrop == L"acrylic" || backdrop == L"mica" || backdrop == L"none") c.backdrop = backdrop;
  if (j["autostart"].IsBool()) c.autostart = j["autostart"].AsBool();
  if (j["mascot"].IsBool()) c.mascot = j["mascot"].AsBool();
  num("maxResults", c.maxResults, 1, 200);
  num("visibleRows", c.visibleRows, 1, 30);

  if (j["fileRoots"].IsArray()) c.fileRoots = StrList(j["fileRoots"]);
  if (j["fileExclude"].IsArray()) c.fileExclude = StrList(j["fileExclude"]);
  num("fileMaxDepth", c.fileMaxDepth, 1, 64);
  num("fileReindexMinutes", c.fileReindexMinutes, 0, 24 * 60);
  num("fileMaxEntries", c.fileMaxEntries, 1000, 5000000);

  if (j["folders"].IsArray()) {
    c.folders.clear();
    for (auto& f : j["folders"].Items()) {
      FolderAlias a{Str(f["name"], L""), Str(f["path"], L"")};
      if (!str::Trim(a.name).empty() && !str::Trim(a.path).empty()) c.folders.push_back(std::move(a));
    }
  }
  if (j["commands"].IsArray()) {
    c.commands.clear();
    for (auto& cmd : j["commands"].Items()) {
      CustomCommand cc;
      cc.keywords = StrList(cmd["keywords"]);
      cc.title = Str(cmd["title"], L"");
      cc.target = Str(cmd["target"], L"");
      cc.args = Str(cmd["args"], L"");
      cc.admin = cmd["admin"].AsBool(false);
      if (cc.title.empty() && !cc.keywords.empty()) cc.title = cc.keywords.front();
      if (!cc.keywords.empty() && !cc.target.empty()) c.commands.push_back(std::move(cc));
    }
  }

  std::wstring base(str::Trim(Str(j["currencyBase"], c.currencyBase)));
  if (base.size() == 3) {
    for (auto& ch : base) ch = wchar_t(towupper(ch));
    c.currencyBase = base;
  }
  c.currencyApi = Str(j["currencyApi"], c.currencyApi);
  num("currencyCacheHours", c.currencyCacheHours, 1, 24 * 30);
  c.webSearch = Str(j["webSearch"], c.webSearch);
}

void ExpandPaths(Config& c) {
  for (auto& p : c.fileRoots) p = win::ExpandEnv(p);
  for (auto& f : c.folders) f.path = win::ExpandEnv(f.path);
  for (auto& cmd : c.commands) {
    cmd.target = win::ExpandEnv(cmd.target);
    cmd.args = win::ExpandEnv(cmd.args);
  }
}

}  // namespace

ConfigStore::ConfigStore() {
  std::wstring appData = win::KnownFolder(FOLDERID_RoamingAppData);
  if (appData.empty()) appData = win::ExpandEnv(L"%APPDATA%");
  dir_ = appData + L"\\CloudSpotlight";
  path_ = dir_ + L"\\config.json";
}

Config ConfigStore::Defaults() {
  Config c;
  const std::wstring home = UserProfile();
  const std::wstring desktop = win::KnownFolder(FOLDERID_Desktop);
  const std::wstring documents = win::KnownFolder(FOLDERID_Documents);
  const std::wstring downloads = win::KnownFolder(FOLDERID_Downloads);

  for (const std::wstring& p : {desktop, documents, downloads, win::KnownFolder(FOLDERID_Pictures),
                                win::KnownFolder(FOLDERID_Videos), win::KnownFolder(FOLDERID_Music)})
    PushUnique(c.fileRoots, Unexpand(p));
  for (const wchar_t* sub : {L"\\source", L"\\Projects", L"\\repos"})
    if (win::DirExists(home + sub)) PushUnique(c.fileRoots, L"%USERPROFILE%" + std::wstring(sub));

  c.fileExclude = {L"node_modules", L".git", L".svn",  L".hg",    L"$Recycle.Bin", L"AppData",
                   L"__pycache__",  L".venv", L"venv", L"bin",    L"obj",          L".vs",
                   L".idea",        L"dist",  L"build", L"target", L"packages"};

  std::wstring projects = L"%USERPROFILE%\\Projects";
  if (!win::DirExists(home + L"\\Projects")) {
    if (win::DirExists(home + L"\\source\\repos")) projects = L"%USERPROFILE%\\source\\repos";
    else if (win::DirExists(home + L"\\source")) projects = L"%USERPROFILE%\\source";
    else if (win::DirExists(home + L"\\repos")) projects = L"%USERPROFILE%\\repos";
  }
  c.folders.push_back({L"проекты", projects});
  if (!downloads.empty()) c.folders.push_back({L"загрузки", Unexpand(downloads)});
  if (!documents.empty()) c.folders.push_back({L"документы", Unexpand(documents)});
  if (!desktop.empty()) c.folders.push_back({L"рабочий стол", Unexpand(desktop)});
  return c;
}

json::Value ConfigStore::ToJson(const Config& c) {
  using json::Value;
  Value j = Value::Object();
  j.Set("hotkey", Value::String(c.hotkey));
  j.Set("fallbackHotkey", Value::String(c.fallbackHotkey));
  j.Set("theme", Value::String(c.theme));
  j.Set("backdrop", Value::String(c.backdrop));
  j.Set("autostart", Value::Bool(c.autostart));
  j.Set("mascot", Value::Bool(c.mascot));
  j.Set("maxResults", Value::Number(c.maxResults));
  j.Set("visibleRows", Value::Number(c.visibleRows));
  j.Set("fileRoots", StrArray(c.fileRoots));
  j.Set("fileExclude", StrArray(c.fileExclude));
  j.Set("fileMaxDepth", Value::Number(c.fileMaxDepth));
  j.Set("fileReindexMinutes", Value::Number(c.fileReindexMinutes));
  j.Set("fileMaxEntries", Value::Number(c.fileMaxEntries));
  Value folders = Value::Array();
  for (auto& f : c.folders) {
    Value o = Value::Object();
    o.Set("name", Value::String(f.name));
    o.Set("path", Value::String(f.path));
    folders.Push(std::move(o));
  }
  j.Set("folders", std::move(folders));
  Value commands = Value::Array();
  for (auto& cmd : c.commands) {
    Value o = Value::Object();
    o.Set("keywords", StrArray(cmd.keywords));
    o.Set("title", Value::String(cmd.title));
    o.Set("target", Value::String(cmd.target));
    o.Set("args", Value::String(cmd.args));
    o.Set("admin", Value::Bool(cmd.admin));
    commands.Push(std::move(o));
  }
  j.Set("commands", std::move(commands));
  j.Set("currencyBase", Value::String(c.currencyBase));
  j.Set("currencyApi", Value::String(c.currencyApi));
  j.Set("currencyCacheHours", Value::Number(c.currencyCacheHours));
  j.Set("webSearch", Value::String(c.webSearch));
  return j;
}

ConfigStore::Status ConfigStore::Load(Config& out) {
  error_.clear();
  Config c = Defaults();
  Status status = Status::Ok;

  std::string text;
  DWORD err = 0;
  if (!ReadFileUtf8(path_, text, &err)) {
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
      status = EnsureFile() ? Status::Created : Status::IoError;
      if (status == Status::IoError) error_ = L"Не удалось создать " + path_;
    } else {
      status = Status::IoError;
      error_ = L"Не удалось прочитать " + path_ + L" (ошибка " + std::to_wstring(err) + L")";
    }
  } else {
    json::Value j;
    std::string perr;
    if (!json::Parse(text, j, &perr) || !j.IsObject()) {
      status = Status::ParseError;
      std::wstring why = perr.empty() ? std::wstring(L"ожидался объект { … }") : str::Utf8ToWide(perr);
      error_ = L"Ошибка в config.json: " + why + L". Используются настройки по умолчанию.";
    } else {
      Overlay(j, c);
    }
  }
  stamp_ = ReadStamp();

  ExpandPaths(c);
  c.dataDir = dir_;
  c.configPath = path_;
  out = std::move(c);
  return status;
}

bool ConfigStore::EnsureFile() {
  if (win::FileExists(path_)) return true;
  SHCreateDirectoryExW(nullptr, dir_.c_str(), nullptr);
  std::string text = json::Serialize(ToJson(Defaults()), true);
  text += '\n';
  bool ok = WriteFile(text);
  stamp_ = ReadStamp();
  return ok;
}

bool ConfigStore::SetValue(const char* key, json::Value v) {
  std::string text;
  DWORD err = 0;
  json::Value j;
  if (!ReadFileUtf8(path_, text, &err)) {
    if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) return false;
    if (!EnsureFile() || !ReadFileUtf8(path_, text, &err)) return false;
  }
  if (!json::Parse(text, j) || !j.IsObject()) return false;
  j.Set(key, std::move(v));
  std::string out = json::Serialize(j, true);
  out += '\n';
  bool ok = WriteFile(out);
  stamp_ = ReadStamp();
  return ok;
}

bool ConfigStore::ChangedOnDisk() const { return !(ReadStamp() == stamp_); }

ConfigStore::Stamp ConfigStore::ReadStamp() const {
  Stamp s;
  WIN32_FILE_ATTRIBUTE_DATA a;
  if (GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &a)) {
    s.exists = true;
    s.mtime = a.ftLastWriteTime;
    s.size = (ULONGLONG(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
  }
  return s;
}

bool ConfigStore::WriteFile(const std::string& utf8) {
  // Write a temp file and swap it in, so editors and our own watcher never observe a half-written config.
  std::wstring tmp = path_ + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  bool ok = ::WriteFile(h, utf8.data(), DWORD(utf8.size()), &written, nullptr) && written == utf8.size();
  CloseHandle(h);
  if (ok) ok = MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  if (!ok) DeleteFileW(tmp.c_str());
  return ok;
}

}  // namespace cs
