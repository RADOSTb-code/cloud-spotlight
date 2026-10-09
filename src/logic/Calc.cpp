#include "logic/Calc.h"

#include <charconv>
#include <cstdint>
#include <cmath>

#include "core/Str.h"

namespace cs::calc {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kE = 2.71828182845904523536;
constexpr int kMaxDepth = 64;
constexpr int kMaxArgs = 16;

enum class Fn : uint8_t {
  Const, Sqrt, Cbrt, Abs, Sin, Cos, Tan, Cot, Asin, Acos, Atan, Sinh, Cosh, Tanh, Ln, Log10, Log2, Exp,
  Floor, Ceil, Round, Sign, Fact, Min, Max, Pow, Hypot, Atan2,
};

struct Name {
  const wchar_t* name;
  Fn fn;
  double value;  // for Fn::Const
};

constexpr Name kNames[] = {
    {L"pi", Fn::Const, kPi},       {L"π", Fn::Const, kPi},       {L"пи", Fn::Const, kPi},
    {L"e", Fn::Const, kE},         {L"tau", Fn::Const, 2 * kPi}, {L"τ", Fn::Const, 2 * kPi},
    {L"sqrt", Fn::Sqrt, 0},        {L"корень", Fn::Sqrt, 0},     {L"cbrt", Fn::Cbrt, 0},
    {L"abs", Fn::Abs, 0},          {L"sin", Fn::Sin, 0},         {L"cos", Fn::Cos, 0},
    {L"tan", Fn::Tan, 0},          {L"tg", Fn::Tan, 0},          {L"cot", Fn::Cot, 0},
    {L"ctg", Fn::Cot, 0},          {L"asin", Fn::Asin, 0},       {L"arcsin", Fn::Asin, 0},
    {L"acos", Fn::Acos, 0},        {L"arccos", Fn::Acos, 0},     {L"atan", Fn::Atan, 0},
    {L"arctan", Fn::Atan, 0},      {L"arctg", Fn::Atan, 0},      {L"sinh", Fn::Sinh, 0},
    {L"cosh", Fn::Cosh, 0},        {L"tanh", Fn::Tanh, 0},       {L"ln", Fn::Ln, 0},
    {L"log", Fn::Log10, 0},        {L"lg", Fn::Log10, 0},        {L"log10", Fn::Log10, 0},
    {L"log2", Fn::Log2, 0},        {L"exp", Fn::Exp, 0},         {L"floor", Fn::Floor, 0},
    {L"ceil", Fn::Ceil, 0},        {L"round", Fn::Round, 0},     {L"sign", Fn::Sign, 0},
    {L"fact", Fn::Fact, 0},        {L"min", Fn::Min, 0},         {L"max", Fn::Max, 0},
    {L"pow", Fn::Pow, 0},          {L"hypot", Fn::Hypot, 0},     {L"atan2", Fn::Atan2, 0},
};

// 1 = unary, 2 = binary, -1 = variadic (>= 1)
int Arity(Fn f) {
  switch (f) {
    case Fn::Min: case Fn::Max: return -1;
    case Fn::Pow: case Fn::Hypot: case Fn::Atan2: return 2;
    default: return 1;
  }
}

bool Eq(std::wstring_view a, const wchar_t* b) { return a == std::wstring_view(b); }

const Name* Lookup(std::wstring_view lowered) {
  for (const auto& n : kNames)
    if (Eq(lowered, n.name)) return &n;
  return nullptr;
}

inline bool IsIdentChar(wchar_t c) { return str::IsLetter(c) || c == 0x3C0 /*π*/ || c == 0x3C4 /*τ*/; }
inline bool IsMinus(wchar_t c) { return c == L'-' || c == 0x2212 || c == 0x2013; }
inline bool IsGroupSpace(wchar_t c) { return c == L' ' || c == 0xA0 || c == 0x2009 || c == 0x202F; }

bool Factorial(double n, double& out) {
  if (n < 0 || n > 170) return false;
  if (n == std::floor(n)) {
    double r = 1;
    for (int k = 2; k <= int(n); ++k) r *= k;
    out = r;
    return true;
  }
  out = std::tgamma(n + 1);
  return std::isfinite(out);
}

double Snap(double v) { return std::fabs(v) < 1e-15 ? 0.0 : v; }

struct Val {
  double v = 0;
  bool pct = false;  // operand was "N%": a +/- N% means a * (1 +/- N/100)
};

class Parser {
 public:
  explicit Parser(std::wstring_view s) : s_(s) {}

  Result Run() {
    Result r;
    s_ = str::Trim(s_);
    bool forced = !s_.empty() && s_.front() == L'=';  // "=42", "=pi": always show a result
    if (forced) s_ = str::Trim(s_.substr(1));
    while (!s_.empty() && s_.back() == L'=') s_ = str::Trim(s_.substr(0, s_.size() - 1));
    if (s_.empty()) return r;
    Val v = Add();
    if (fail_ || Peek() != 0 || !std::isfinite(v.v)) return r;
    r.ok = true;
    r.value = v.v == 0 ? 0.0 : v.v;  // no "-0"
    // A bare constant ("e", "pi") is not an expression: it is usually the start of an app name.
    r.isExpression = forced || ops_ > consts_ || radix_;
    r.commaDecimal = comma_;
    r.radixLiteral = radix_;
    return r;
  }

 private:
  std::wstring_view s_;
  size_t i_ = 0;
  int depth_ = 0;
  int ops_ = 0;
  int consts_ = 0;
  bool fail_ = false;
  bool argMode_ = false;  // inside a multi-argument call: ',' separates arguments
  bool comma_ = false;
  bool radix_ = false;

  wchar_t At(size_t k) const { return k < s_.size() ? s_[k] : 0; }
  void SkipWs() {
    while (i_ < s_.size() && str::IsSpace(s_[i_])) ++i_;
  }
  wchar_t Peek() {
    SkipWs();
    return At(i_);
  }
  Val Fail() {
    fail_ = true;
    return {};
  }

  // Reads an identifier at i_ (without consuming) into `buf`, lowercased; returns its length in source chars.
  size_t ReadIdent(wchar_t* buf, size_t cap, size_t& outLen) const {
    size_t k = i_, n = 0;
    while (k < s_.size() && IsIdentChar(s_[k])) {
      if (n < cap) buf[n] = str::LowerChar(s_[k]);
      ++n, ++k;
    }
    // log2 / log10 / atan2: letters followed by digits form one name
    if (n && n < cap && k < s_.size() && str::IsDigit(s_[k])) {
      size_t k2 = k, n2 = n;
      while (k2 < s_.size() && str::IsDigit(s_[k2]) && n2 < cap) buf[n2++] = s_[k2++];
      if (Lookup(std::wstring_view(buf, n2))) {
        outLen = n2;
        return k2 - i_;
      }
    }
    outLen = n > cap ? cap + 1 : n;  // cap + 1 -> never matches a name
    return k - i_;
  }

  bool StartsOperand(size_t k) const {
    wchar_t c = At(k);
    return str::IsDigit(c) || (c == L'.' && str::IsDigit(At(k + 1))) || c == L'(' || c == 0x221A /*√*/ ||
           IsIdentChar(c);
  }

  // additive: term (('+'|'-') term)*, with mac-like percent semantics
  Val Add() {
    Val a = Mul();
    while (!fail_) {
      wchar_t c = Peek();
      double sign;
      if (c == L'+') sign = 1;
      else if (IsMinus(c)) sign = -1;
      else break;
      ++i_;
      ++ops_;
      Val b = Mul();
      if (fail_) break;
      a.v = b.pct ? a.v * (1 + sign * b.v) : a.v + sign * b.v;
      a.pct = false;
    }
    return a;
  }

  Val Mul() {
    Val a = Unary();
    while (!fail_) {
      wchar_t c = Peek();
      if (c == L'*' || c == 0xD7 || c == 0xB7 || c == 0x2715 || c == 0x22C5) {
        ++i_;
        ++ops_;
        Val b = Unary();
        a.v *= b.v;
      } else if (c == L'/' || c == 0xF7 || c == 0x2215) {
        ++i_;
        ++ops_;
        Val b = Unary();
        if (fail_) break;
        if (b.v == 0) return Fail();
        a.v /= b.v;
      } else if (c == L'%') {  // only reached when an operand follows (see Postfix): modulo
        ++i_;
        ++ops_;
        Val b = Unary();
        if (!Mod(a.v, b.v)) return Fail();
      } else if (IsIdentChar(c)) {
        wchar_t buf[16];
        size_t len = 0, adv = ReadIdent(buf, 15, len);
        std::wstring_view id(buf, len < 16 ? len : 0);
        if (Eq(id, L"mod")) {
          i_ += adv;
          ++ops_;
          Val b = Unary();
          if (!Mod(a.v, b.v)) return Fail();
        } else if (Eq(id, L"x") || Eq(id, L"х")) {  // "2 x 3", "2х3" (Latin / Cyrillic)
          i_ += adv;
          ++ops_;
          Val b = Unary();
          a.v *= b.v;
        } else {
          ++ops_;  // implicit: 2pi, 2 sqrt 9
          Val b = Power();
          a.v *= b.v;
        }
      } else if (c == L'(' || c == 0x221A) {
        ++ops_;  // implicit: 2(3+4), (1+2)(3+4)
        Val b = Power();
        a.v *= b.v;
      } else {
        break;
      }
      a.pct = false;
    }
    return a;
  }

  bool Mod(double& a, double b) {
    if (fail_ || b == 0) return false;
    double r = std::fmod(a, b);
    if (r != 0 && ((r < 0) != (b < 0))) r += b;  // mathematical modulo: -1 mod 3 = 2
    a = r;
    return true;
  }

  Val Unary() {
    if (++depth_ > kMaxDepth) return Fail();
    Val v;
    wchar_t c = Peek();
    if (c == L'+') {
      ++i_;
      v = Unary();
    } else if (IsMinus(c)) {
      ++i_;
      v = Unary();
      v.v = -v.v;
    } else {
      v = Power();
    }
    --depth_;
    return v;
  }

  Val Power() {
    Val b = Postfix();
    if (fail_) return b;
    wchar_t c = Peek();
    if (c == L'^' || (c == L'*' && At(i_ + 1) == L'*')) {
      i_ += c == L'^' ? 1 : 2;
      ++ops_;
      Val e = Unary();  // right-associative; allows 2^-1
      if (fail_) return e;
      b.v = std::pow(b.v, e.v);
      b.pct = false;
    }
    return b;
  }

  Val Postfix() {
    Val v = Primary();
    while (!fail_) {
      size_t save = i_;
      wchar_t c = Peek();
      if (c == L'!' && At(i_ + 1) != L'=') {
        ++i_;
        ++ops_;
        if (!Factorial(v.v, v.v)) return Fail();
        v.pct = false;
      } else if (c == L'%') {
        size_t k = i_ + 1;
        while (k < s_.size() && str::IsSpace(s_[k])) ++k;
        if (StartsOperand(k)) {
          i_ = save;  // "a % b" -> modulo, handled by Mul()
          break;
        }
        ++i_;
        ++ops_;
        v.v /= 100;
        v.pct = true;
      } else if (c == 0xB0) {  // °
        ++i_;
        ++ops_;
        v.v *= kPi / 180;
        v.pct = false;
      } else if (c == 0xB2 || c == 0xB3) {  // ² ³
        ++i_;
        ++ops_;
        v.v = std::pow(v.v, c == 0xB2 ? 2 : 3);
        v.pct = false;
      } else {
        i_ = save;
        break;
      }
    }
    return v;
  }

  Val Primary() {
    if (++depth_ > kMaxDepth) return Fail();
    Val v = PrimaryInner();
    --depth_;
    return v;
  }

  Val PrimaryInner() {
    wchar_t c = Peek();
    if (str::IsDigit(c) || (c == L'.' && str::IsDigit(At(i_ + 1)))) return Number();
    if (c == L'(') {
      ++i_;
      bool saved = argMode_;
      argMode_ = false;
      Val v = Add();
      argMode_ = saved;
      if (fail_) return v;
      wchar_t e = Peek();
      if (e == L')') ++i_;
      else if (e != 0) return Fail();  // a missing ')' at the very end is tolerated while typing
      v.pct = false;
      return v;
    }
    if (c == 0x221A) {  // √x
      ++i_;
      ++ops_;
      Val v = Power();
      if (fail_ || v.v < 0) return Fail();
      return {std::sqrt(v.v), false};
    }
    if (IsIdentChar(c)) {
      wchar_t buf[16];
      size_t len = 0, adv = ReadIdent(buf, 15, len);
      const Name* n = len < 16 ? Lookup(std::wstring_view(buf, len)) : nullptr;
      if (!n) return Fail();
      i_ += adv;
      ++ops_;
      if (n->fn == Fn::Const) {
        ++consts_;
        return {n->value, false};
      }
      return Call(n->fn);
    }
    return Fail();
  }

  Val Call(Fn fn) {
    double args[kMaxArgs];
    int n = 0, arity = Arity(fn);
    if (Peek() == L'(') {
      ++i_;
      bool saved = argMode_;
      argMode_ = arity != 1 && !SemicolonArgs();  // "max(2,5; 1)": ';' separates, ',' is decimal
      for (;;) {
        Val a = Add();
        if (fail_) return a;
        if (n == kMaxArgs) return Fail();
        args[n++] = a.v;
        wchar_t c = Peek();
        if ((c == L',' && argMode_) || c == L';') {
          ++i_;
          continue;
        }
        break;
      }
      argMode_ = saved;
      wchar_t e = Peek();
      if (e == L')') ++i_;
      else if (e != 0) return Fail();
    } else {
      if (arity != 1) return Fail();
      Val a = Unary();  // "sin 30°", "sqrt 16", "ln 2"
      if (fail_) return a;
      args[n++] = a.v;
    }
    if ((arity > 0 && n != arity) || n < 1) return Fail();
    double x = args[0], r = 0;
    switch (fn) {
      case Fn::Sqrt: if (x < 0) return Fail(); r = std::sqrt(x); break;
      case Fn::Cbrt: r = std::cbrt(x); break;
      case Fn::Abs: r = std::fabs(x); break;
      case Fn::Sin: r = Snap(std::sin(x)); break;
      case Fn::Cos: r = Snap(std::cos(x)); break;
      case Fn::Tan:
        if (std::fabs(std::cos(x)) < 1e-15) return Fail();
        r = Snap(std::tan(x));
        break;
      case Fn::Cot:
        if (std::fabs(std::sin(x)) < 1e-15) return Fail();
        r = Snap(std::cos(x) / std::sin(x));
        break;
      case Fn::Asin: r = std::asin(x); break;
      case Fn::Acos: r = std::acos(x); break;
      case Fn::Atan: r = std::atan(x); break;
      case Fn::Sinh: r = std::sinh(x); break;
      case Fn::Cosh: r = std::cosh(x); break;
      case Fn::Tanh: r = std::tanh(x); break;
      case Fn::Ln: if (x <= 0) return Fail(); r = std::log(x); break;
      case Fn::Log10: if (x <= 0) return Fail(); r = std::log10(x); break;
      case Fn::Log2: if (x <= 0) return Fail(); r = std::log2(x); break;
      case Fn::Exp: r = std::exp(x); break;
      case Fn::Floor: r = std::floor(x); break;
      case Fn::Ceil: r = std::ceil(x); break;
      case Fn::Round: r = std::round(x); break;
      case Fn::Sign: r = x > 0 ? 1 : x < 0 ? -1 : 0; break;
      case Fn::Fact: if (!Factorial(x, r)) return Fail(); break;
      case Fn::Min: r = x; for (int k = 1; k < n; ++k) r = args[k] < r ? args[k] : r; break;
      case Fn::Max: r = x; for (int k = 1; k < n; ++k) r = args[k] > r ? args[k] : r; break;
      case Fn::Pow: r = std::pow(x, args[1]); break;
      case Fn::Hypot: r = std::hypot(x, args[1]); break;
      case Fn::Atan2: r = std::atan2(x, args[1]); break;
      case Fn::Const: break;
    }
    if (!std::isfinite(r)) return Fail();
    return {r, false};
  }

  // True if the argument list starting at i_ uses ';' at its top level.
  bool SemicolonArgs() const {
    int depth = 0;
    for (size_t k = i_; k < s_.size(); ++k) {
      wchar_t c = s_[k];
      if (c == L'(') ++depth;
      else if (c == L')' && --depth < 0) return false;
      else if (c == L';' && depth == 0) return true;
    }
    return false;
  }

  Val Number() {
    // Radix literals: 0x / 0b / 0o (underscores allowed)
    if (At(i_) == L'0') {
      wchar_t p = str::LowerChar(At(i_ + 1));
      int base = p == L'x' ? 16 : p == L'b' ? 2 : p == L'o' ? 8 : 0;
      if (base && DigitVal(At(i_ + 2)) < base) {
        size_t k = i_ + 2;
        double v = 0;
        for (;; ++k) {
          wchar_t c = At(k);
          if (c == L'_' && DigitVal(At(k + 1)) < base) continue;
          int d = DigitVal(c);
          if (d >= base) break;
          v = v * base + d;
        }
        if (IsIdentChar(At(k)) || str::IsDigit(At(k))) return Fail();  // 0b102, 0xFFg
        i_ = k;
        radix_ = true;
        return {v, false};
      }
    }
    char buf[80];
    size_t n = 0;
    auto put = [&](wchar_t c) {
      if (n < sizeof(buf) - 1) buf[n++] = char(c);
      else fail_ = true;
    };
    size_t k = i_;
    size_t firstRun = 0;
    while (str::IsDigit(At(k)) || (At(k) == L'_' && str::IsDigit(At(k + 1)) && k > i_)) {
      if (At(k) != L'_') put(At(k)), ++firstRun;
      ++k;
    }
    // "1 000 000": groups of exactly 3 digits after a 1-3 digit head
    if (firstRun >= 1 && firstRun <= 3) {
      while (IsGroupSpace(At(k)) && str::IsDigit(At(k + 1)) && str::IsDigit(At(k + 2)) &&
             str::IsDigit(At(k + 3)) && !str::IsDigit(At(k + 4))) {
        put(At(k + 1)), put(At(k + 2)), put(At(k + 3));
        k += 4;
      }
      // "1,000,000" / "1,000.5": English thousands (only when unambiguous)
      if (!argMode_ && At(k) == L',') {
        size_t g = 0, kk = k;
        while (At(kk) == L',' && str::IsDigit(At(kk + 1)) && str::IsDigit(At(kk + 2)) &&
               str::IsDigit(At(kk + 3)) && !str::IsDigit(At(kk + 4)))
          kk += 4, ++g;
        if (g >= 2 || (g == 1 && At(kk) == L'.' && str::IsDigit(At(kk + 1)))) {
          for (size_t q = k; q < kk; ++q)
            if (At(q) != L',') put(At(q));
          k = kk;
        }
      }
    }
    wchar_t c = At(k);
    if ((c == L'.' || (c == L',' && !argMode_)) && str::IsDigit(At(k + 1))) {
      if (c == L',') comma_ = true;
      put(L'.');
      ++k;
      while (str::IsDigit(At(k))) put(At(k++));
    }
    c = At(k);
    if (c == L'e' || c == L'E') {
      size_t e = k + 1;
      if (At(e) == L'+' || At(e) == L'-') ++e;
      if (str::IsDigit(At(e))) {
        put(L'e');
        for (size_t q = k + 1; q < e; ++q) put(At(q));
        k = e;
        while (str::IsDigit(At(k))) put(At(k++));
      }
    }
    if (fail_ || n == 0) return Fail();
    double v = 0;
    auto res = std::from_chars(buf, buf + n, v);
    if (res.ec != std::errc() || res.ptr != buf + n) return Fail();
    i_ = k;
    return {v, false};
  }

  static int DigitVal(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    c = str::LowerChar(c);
    if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
    return 99;
  }
};

}  // namespace

Result Evaluate(std::wstring_view expr) { return Parser(expr).Run(); }

size_t ParseLeadingAmount(std::wstring_view text, Result& out) {
  out = Result{};
  size_t e = 0;
  for (; e < text.size(); ++e) {
    wchar_t c = text[e];
    bool numeric = str::IsDigit(c) || str::IsSpace(c) || c == L'.' || c == L',' || c == L'+' || IsMinus(c) ||
                   c == L'*' || c == 0xD7 || c == 0xF7 || c == L'/' || c == L'(' || c == L')' || c == L'^' ||
                   c == L'_' || c == 0xB7;
    if (!numeric) break;
  }
  // trailing operators/spaces belong to what follows ("10->mi", "100 / ")
  while (e > 0 && !str::IsDigit(text[e - 1]) && text[e - 1] != L')') --e;
  if (e == 0) return 0;
  Result r = Evaluate(text.substr(0, e));
  if (!r.ok) return 0;
  out = r;
  return e;
}

std::wstring Pretty(std::wstring_view expr) {
  std::wstring_view s = str::Trim(expr);
  if (!s.empty() && s.front() == L'=') s = str::Trim(s.substr(1));
  while (!s.empty() && s.back() == L'=') s = str::Trim(s.substr(0, s.size() - 1));
  std::wstring out;
  out.reserve(s.size());
  bool space = false;
  for (size_t i = 0; i < s.size(); ++i) {
    wchar_t c = s[i];
    if (str::IsSpace(c)) {
      space = true;
      continue;
    }
    if (space && !out.empty()) out.push_back(L' ');
    space = false;
    if (c == L'*' && i + 1 < s.size() && s[i + 1] == L'*') {
      out.push_back(L'^');
      ++i;
    } else if (c == L'*') {
      out.push_back(0xD7);
    } else if (c == L'/') {
      out.push_back(0xF7);
    } else {
      out.push_back(c);
    }
  }
  return out;
}

}  // namespace cs::calc
