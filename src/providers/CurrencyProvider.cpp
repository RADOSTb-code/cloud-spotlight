#include "providers/CurrencyProvider.h"

#include "platform/Win.h"  // <windows.h> first

#include <winhttp.h>

#include <algorithm>
#include <cstdio>

#include "core/Config.h"
#include "core/Str.h"

namespace cs {
namespace {

constexpr const wchar_t* kGlyphMoney = L"";
constexpr DWORD kAutoProxy = 4;  // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY (Win 8.1+), not in older SDK headers
constexpr DWORD kTimeoutMs = 5000;
constexpr size_t kMaxBody = 2 * 1024 * 1024;
constexpr int64_t kFileTimeUnixEpoch = 116444736000000000LL;

int64_t FileTimeToUnix(const FILETIME& ft) {
  int64_t v = (int64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  return (v - kFileTimeUnixEpoch) / 10000000;
}

int64_t NowUnix() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  return FileTimeToUnix(ft);
}

std::wstring LocalDateText(int64_t unix) {
  if (unix <= 0) return {};
  int64_t v = unix * 10000000 + kFileTimeUnixEpoch;
  FILETIME ft{DWORD(v & 0xFFFFFFFF), DWORD(uint64_t(v) >> 32)};
  SYSTEMTIME utc, local;
  if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return {};
  wchar_t buf[32];
  swprintf(buf, 32, L"%02u.%02u %02u:%02u", unsigned(local.wDay), unsigned(local.wMonth), unsigned(local.wHour),
           unsigned(local.wMinute));
  return buf;
}

bool ReadFileBytes(const std::wstring& path, std::string& out, int64_t* mtimeUnix) {
  WIN32_FILE_ATTRIBUTE_DATA fad;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad) || fad.nFileSizeHigh ||
      fad.nFileSizeLow > kMaxBody)
    return false;
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  out.resize(fad.nFileSizeLow);
  DWORD got = 0;
  BOOL ok = out.empty() || ReadFile(h, out.data(), DWORD(out.size()), &got, nullptr);
  CloseHandle(h);
  if (!ok) return false;
  out.resize(got);
  if (mtimeUnix) *mtimeUnix = FileTimeToUnix(fad.ftLastWriteTime);
  return true;
}

bool WriteFileAtomic(const std::wstring& path, const std::string& data) {
  std::wstring tmp = path + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  BOOL ok = WriteFile(h, data.data(), DWORD(data.size()), &written, nullptr) && written == data.size();
  CloseHandle(h);
  if (ok) ok = MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  if (!ok) DeleteFileW(tmp.c_str());
  return ok != FALSE;
}

std::wstring StripGroups(const std::wstring& s) {
  std::wstring r;
  for (wchar_t c : s)
    if (c != 0x2009) r.push_back(c);
  return r;
}

std::wstring WithSymbol(const std::wstring& amount, const std::wstring& code) {
  const wchar_t* sym = currency::Symbol(code);
  return amount + L" " + (sym ? std::wstring(sym) : code);
}

}  // namespace

CurrencyProvider::~CurrencyProvider() { Shutdown(); }

void CurrencyProvider::Init(const Config& cfg, IHost& host) {
  host_ = &host;
  base_ = cfg.currencyBase.empty() ? L"RUB" : str::ToLower(cfg.currencyBase);
  for (auto& c : base_) c = (c >= L'a' && c <= L'z') ? wchar_t(c - 32) : c;
  api_ = cfg.currencyApi.empty() ? L"https://open.er-api.com/v6/latest/USD" : cfg.currencyApi;
  cacheHours_ = std::clamp(cfg.currencyCacheHours, 1, 720);
  if (!cfg.dataDir.empty()) {
    CreateDirectoryW(cfg.dataDir.c_str(), nullptr);
    cachePath_ = cfg.dataDir + L"\\rates.json";
  }
  LoadCache();  // small file, instant: results are available on the very first keystroke
  stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (stopEvent_) thread_ = std::thread([this] { Worker(); });
}

void CurrencyProvider::Shutdown() {
  stopping_ = true;
  if (stopEvent_) SetEvent(stopEvent_);
  {
    std::lock_guard lk(netMu_);
    if (request_) {  // cancels a blocking WinHTTP call on the worker thread
      WinHttpCloseHandle(request_);
      request_ = nullptr;
    }
  }
  if (thread_.joinable()) thread_.join();
  if (stopEvent_) {
    CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
  }
}

std::shared_ptr<const CurrencyProvider::Snapshot> CurrencyProvider::Get() const {
  std::lock_guard lk(mu_);
  return snap_;
}

void CurrencyProvider::Publish(currency::Rates&& r, int64_t fetchedUnix) {
  auto s = std::make_shared<Snapshot>();
  s->dateText = LocalDateText(r.updatedUnix > 0 ? r.updatedUnix : fetchedUnix);
  s->rates = std::move(r);
  fetchedUnix_ = fetchedUnix;
  std::lock_guard lk(mu_);
  snap_ = std::move(s);
}

bool CurrencyProvider::LoadCache() {
  if (cachePath_.empty()) return false;
  std::string body;
  int64_t mtime = 0;
  currency::Rates r;
  if (!ReadFileBytes(cachePath_, body, &mtime) || !currency::ParseRates(body, r)) return false;
  Publish(std::move(r), mtime);
  return true;
}

void CurrencyProvider::Worker() {
  int64_t retrySec = 30;
  while (!stopping_) {
    const int64_t ttl = int64_t(cacheHours_) * 3600;
    const int64_t now = NowUnix();
    int64_t waitSec;
    if (!Get() || now - fetchedUnix_ >= ttl || now < fetchedUnix_) {
      std::string body;
      currency::Rates r;
      if (Fetch(body) && currency::ParseRates(body, r)) {
        Publish(std::move(r), now);
        if (!cachePath_.empty()) WriteFileAtomic(cachePath_, body);
        if (host_ && !stopping_) host_->RequestRefresh();
        retrySec = 30;
        waitSec = ttl;
      } else {
        waitSec = retrySec;
        retrySec = std::min<int64_t>(retrySec * 2, 1800);
      }
    } else {
      waitSec = ttl - (now - fetchedUnix_) + 1;
    }
    // Waits don't advance while the PC sleeps, so re-check at least every 30 minutes.
    waitSec = std::clamp<int64_t>(waitSec, 1, 1800);
    if (WaitForSingleObject(stopEvent_, DWORD(waitSec * 1000)) != WAIT_TIMEOUT) break;
  }
}

bool CurrencyProvider::Fetch(std::string& body) {
  body.clear();
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  wchar_t host[256], path[2048], extra[2048];
  uc.lpszHostName = host;
  uc.dwHostNameLength = ARRAYSIZE(host);
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = ARRAYSIZE(path);
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = ARRAYSIZE(extra);
  if (!WinHttpCrackUrl(api_.c_str(), DWORD(api_.size()), 0, &uc)) return false;
  std::wstring object = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
  if (object.empty()) object = L"/";

  HINTERNET session = WinHttpOpen(L"CloudSpotlight/1.0", kAutoProxy, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session)  // before Windows 8.1
    session = WinHttpOpen(L"CloudSpotlight/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return false;
  WinHttpSetTimeouts(session, int(kTimeoutMs), int(kTimeoutMs), int(kTimeoutMs), int(kTimeoutMs));
  HINTERNET connect = WinHttpConnect(session, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
  HINTERNET req = connect ? WinHttpOpenRequest(connect, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                          : nullptr;
  bool ok = false;
  if (req) {
    {
      std::lock_guard lk(netMu_);
      if (stopping_) {
        WinHttpCloseHandle(req);
        req = nullptr;
      } else {
        request_ = req;
      }
    }
    if (req) {
      DWORD decompress = 3;  // WINHTTP_DECOMPRESSION_FLAG_ALL (Win 8.1+); harmless if unsupported
      WinHttpSetOption(req, 118 /*WINHTTP_OPTION_DECOMPRESSION*/, &decompress, sizeof(decompress));
      DWORD status = 0, size = sizeof(status);
      ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
           !stopping_ && WinHttpReceiveResponse(req, nullptr) && !stopping_ &&
           WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) &&
           status == 200;
      while (ok && !stopping_) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) { ok = false; break; }
        if (!avail) break;
        size_t old = body.size();
        if (old + avail > kMaxBody) { ok = false; break; }
        body.resize(old + avail);
        DWORD got = 0;
        if (!WinHttpReadData(req, body.data() + old, avail, &got)) { ok = false; break; }
        body.resize(old + got);
        if (!got) break;
      }
      std::lock_guard lk(netMu_);
      if (request_) {  // not already closed by Shutdown()
        WinHttpCloseHandle(request_);
        request_ = nullptr;
      }
    }
  }
  if (connect) WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return ok && !stopping_ && !body.empty();
}

void CurrencyProvider::Search(std::wstring_view query, std::vector<Result>& out) {
  if (query.size() > 120) return;
  auto snap = Get();
  currency::Query q;
  if (!currency::Parse(query, base_, snap ? &snap->rates : nullptr, q)) return;

  Result res;
  res.category = L"Конвертер";
  res.iconKind = IconKind::Glyph;
  res.icon = kGlyphMoney;
  if (!snap) {
    res.title = L"Загружаю курсы валют…";
    res.subtitle = L"Результат появится, как только курсы будут получены";
    res.score = 0.5f;
    res.actions = 0;
    out.push_back(std::move(res));
    return;
  }
  const currency::Rates& rates = snap->rates;
  if (!rates.Has(q.from)) return;
  std::wstring amountText = str::FormatNumber(q.amount, 2, true, L',');
  float score = 0.98f;
  for (const auto& to : q.to) {
    double v = 0;
    if (!rates.Convert(q.amount, q.from, to, v)) continue;
    std::wstring value = currency::FormatMoney(v);
    Result r = res;
    r.title = WithSymbol(value, to);
    r.subtitle = amountText + L" " + q.from + L" = " + value + L" " + to;
    if (!snap->dateText.empty()) r.subtitle += L" · курс от " + snap->dateText;
    r.score = score;
    score -= 0.001f;
    r.actions = kActOpen | kActCopy;
    r.payload = StripGroups(value);
    r.copyText = r.payload;
    out.push_back(std::move(r));
  }
}

ExecResult CurrencyProvider::Execute(const Result& r, Action a) {
  (void)a;
  if (r.payload.empty()) return ExecResult{false, {}, {}};  // "loading" placeholder: keep the launcher open
  if (host_) host_->CopyToClipboard(r.payload);
  return ExecResult{};
}

}  // namespace cs
