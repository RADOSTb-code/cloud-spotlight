#pragma once
// Currency converter ("100 usd", "€50 в рублях", "1к юаней в рубли"). Parsing: logic/CurrencyQuery.
// Rates: a background thread loads cfg.dataDir\rates.json, refreshes it from cfg.currencyApi via WinHTTP when
// older than cfg.currencyCacheHours and publishes an immutable snapshot (shared_ptr swap). Search() never blocks
// on the network; it only copies the snapshot pointer under a short mutex.
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/Types.h"
#include "logic/CurrencyQuery.h"

namespace cs {

class CurrencyProvider final : public IProvider {
 public:
  CurrencyProvider() = default;
  ~CurrencyProvider() override;
  const wchar_t* Id() const override { return L"currency"; }
  void Init(const Config& cfg, IHost& host) override;
  void Search(std::wstring_view query, std::vector<Result>& out) override;
  ExecResult Execute(const Result& r, Action a) override;
  void Shutdown() override;

 private:
  struct Snapshot {
    currency::Rates rates;
    std::wstring dateText;  // "09.10 14:00" (local time of the rates)
  };

  void Worker();
  bool Fetch(std::string& body);
  bool LoadCache();
  void Publish(currency::Rates&& r, int64_t fetchedUnix);
  std::shared_ptr<const Snapshot> Get() const;

  IHost* host_ = nullptr;
  std::wstring base_ = L"RUB";
  std::wstring api_;
  std::wstring cachePath_;
  int cacheHours_ = 6;

  mutable std::mutex mu_;
  std::shared_ptr<const Snapshot> snap_;
  std::atomic<int64_t> fetchedUnix_{0};  // when the cached/downloaded rates were obtained

  void* stopEvent_ = nullptr;   // HANDLE
  std::mutex netMu_;
  void* request_ = nullptr;     // HINTERNET of the in-flight request; closed by Shutdown() to cancel it
  std::atomic<bool> stopping_{false};
  std::thread thread_;
};

}  // namespace cs
