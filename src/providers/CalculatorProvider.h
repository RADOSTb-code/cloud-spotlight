#pragma once
// Calculator ("2+2", "sqrt 16", "200 + 15%") and unit converter ("10 km in miles", "30°C в F").
// Thin wrapper over logic/Calc and logic/Units; Enter / Ctrl+C copies the plain value.
#include <vector>

#include "core/Types.h"

namespace cs {

class CalculatorProvider final : public IProvider {
 public:
  CalculatorProvider() = default;
  const wchar_t* Id() const override { return L"calc"; }
  void Init(const Config& cfg, IHost& host) override;
  void Search(std::wstring_view query, std::vector<Result>& out) override;
  ExecResult Execute(const Result& r, Action a) override;

 private:
  IHost* host_ = nullptr;
};

}  // namespace cs
