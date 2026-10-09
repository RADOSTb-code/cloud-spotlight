#pragma once
// Loads/saves cs::Config from %APPDATA%\CloudSpotlight\config.json.
// The file keeps paths in their natural unexpanded form ("%USERPROFILE%\\Projects"); the in-memory Config is
// always fully expanded.
#include <windows.h>

#include <string>
#include <string_view>

#include "core/Config.h"
#include "core/Json.h"

namespace cs {

class ConfigStore {
 public:
  enum class Status { Ok, Created, ParseError, IoError };

  ConfigStore();  // resolves dir/path only, no I/O

  // Fills `out` with defaults overlaid by the file. Creates the directory and a default file if missing.
  // On ParseError/IoError `out` holds pure defaults and LastError() describes the problem.
  Status Load(Config& out);
  // Read-modify-write of one top-level key, keeping the user's other values (comments are not preserved).
  // Fails without touching the file if it does not parse.
  bool SetValue(const char* key, json::Value v);
  // Recreates the default file if it was deleted. true if the file exists afterwards.
  bool EnsureFile();
  // config.json differs (mtime/size) from what Load/SetValue last saw.
  bool ChangedOnDisk() const;

  const std::wstring& Path() const { return path_; }
  const std::wstring& Dir() const { return dir_; }
  const std::wstring& LastError() const { return error_; }

  // Defaults with unexpanded paths, as written to a fresh file.
  static Config Defaults();
  static json::Value ToJson(const Config& c);

 private:
  struct Stamp {
    FILETIME mtime{};
    ULONGLONG size = 0;
    bool exists = false;
    bool operator==(const Stamp& o) const {
      return exists == o.exists && size == o.size && CompareFileTime(&mtime, &o.mtime) == 0;
    }
  };
  Stamp ReadStamp() const;
  bool WriteFile(const std::string& utf8);

  std::wstring dir_;
  std::wstring path_;
  std::wstring error_;
  Stamp stamp_;
};

}  // namespace cs
