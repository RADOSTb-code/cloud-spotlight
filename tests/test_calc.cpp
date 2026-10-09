#include <chrono>

#include "Test.h"
#include "logic/Calc.h"
#include "logic/Units.h"

using namespace cs;

namespace {
double Ev(const wchar_t* s) {
  auto r = calc::Evaluate(s);
  return r.ok ? r.value : -999999.0;
}
bool Ok(const wchar_t* s) { return calc::Evaluate(s).ok; }
bool IsExpr(const wchar_t* s) {
  auto r = calc::Evaluate(s);
  return r.ok && r.isExpression;
}
}  // namespace

TEST(Calc_Basic) {
  CHECK_NEAR(Ev(L"2+2"), 4, 1e-12);
  CHECK_NEAR(Ev(L"2 + 3 * 4"), 14, 1e-12);
  CHECK_NEAR(Ev(L"(2 + 3) * 4"), 20, 1e-12);
  CHECK_NEAR(Ev(L"10 / 4"), 2.5, 1e-12);
  CHECK_NEAR(Ev(L"7 - 10"), -3, 1e-12);
  CHECK_NEAR(Ev(L"6 × 7"), 42, 1e-12);
  CHECK_NEAR(Ev(L"84 ÷ 2"), 42, 1e-12);
  CHECK_NEAR(Ev(L"6 · 7"), 42, 1e-12);
  CHECK_NEAR(Ev(L"6x7"), 42, 1e-12);
  CHECK_NEAR(Ev(L"6 х 7"), 42, 1e-12);  // Cyrillic х
  CHECK_NEAR(Ev(L"10 − 3"), 7, 1e-12);  // U+2212
  CHECK_NEAR(Ev(L"1 - 2 - 3"), -4, 1e-12);
  CHECK_NEAR(Ev(L"100 / 10 / 5"), 2, 1e-12);
  CHECK_NEAR(Ev(L"0.1 + 0.2"), 0.3, 1e-12);
  CHECK_NEAR(Ev(L".5 * 4"), 2, 1e-12);
}

TEST(Calc_PowerAndUnary) {
  CHECK_NEAR(Ev(L"2^10"), 1024, 1e-9);
  CHECK_NEAR(Ev(L"2**10"), 1024, 1e-9);
  CHECK_NEAR(Ev(L"2^3^2"), 512, 1e-9);  // right-assoc
  CHECK_NEAR(Ev(L"-2^2"), -4, 1e-12);
  CHECK_NEAR(Ev(L"(-2)^2"), 4, 1e-12);
  CHECK_NEAR(Ev(L"2^-1"), 0.5, 1e-12);
  CHECK_NEAR(Ev(L"--3"), 3, 1e-12);
  CHECK_NEAR(Ev(L"+5 - -5"), 10, 1e-12);
  CHECK_NEAR(Ev(L"3²"), 9, 1e-12);
  CHECK_NEAR(Ev(L"2³"), 8, 1e-12);
}

TEST(Calc_ImplicitMultiplication) {
  CHECK_NEAR(Ev(L"2(3+4)"), 14, 1e-12);
  CHECK_NEAR(Ev(L"(1+2)(3+4)"), 21, 1e-12);
  CHECK_NEAR(Ev(L"2pi"), 6.283185307179586, 1e-12);
  CHECK_NEAR(Ev(L"2π"), 6.283185307179586, 1e-12);
  CHECK_NEAR(Ev(L"3 sqrt 16"), 12, 1e-12);
  CHECK_NEAR(Ev(L"2e"), 5.43656365691809, 1e-12);
  CHECK(!Ok(L"2 3"));  // two plain numbers are not multiplied
}

TEST(Calc_Percent) {
  CHECK_NEAR(Ev(L"50%"), 0.5, 1e-12);
  CHECK_NEAR(Ev(L"200 + 15%"), 230, 1e-9);
  CHECK_NEAR(Ev(L"200 - 10%"), 180, 1e-9);
  CHECK_NEAR(Ev(L"200 * 15%"), 30, 1e-9);
  CHECK_NEAR(Ev(L"200 / 50%"), 400, 1e-9);
  CHECK_NEAR(Ev(L"100 + 10% + 10%"), 121, 1e-9);
  CHECK_NEAR(Ev(L"10 % 3"), 1, 1e-12);  // modulo when an operand follows
  CHECK_NEAR(Ev(L"10 mod 3"), 1, 1e-12);
  CHECK_NEAR(Ev(L"-1 mod 3"), 2, 1e-12);
  CHECK(!Ok(L"5 mod 0"));
}

TEST(Calc_Factorial) {
  CHECK_NEAR(Ev(L"5!"), 120, 1e-9);
  CHECK_NEAR(Ev(L"0!"), 1, 1e-12);
  CHECK_NEAR(Ev(L"3!!"), 720, 1e-9);
  CHECK_NEAR(Ev(L"fact(4)"), 24, 1e-9);
  CHECK_NEAR(Ev(L"0.5!"), 0.886226925452758, 1e-12);
  CHECK(!Ok(L"171!"));
  CHECK(!Ok(L"(-1)!"));
}

TEST(Calc_Functions) {
  CHECK_NEAR(Ev(L"sqrt(16)"), 4, 1e-12);
  CHECK_NEAR(Ev(L"sqrt 16"), 4, 1e-12);
  CHECK_NEAR(Ev(L"√16"), 4, 1e-12);
  CHECK_NEAR(Ev(L"√(9+16)"), 5, 1e-12);
  CHECK_NEAR(Ev(L"корень(81)"), 9, 1e-12);
  CHECK_NEAR(Ev(L"cbrt(27)"), 3, 1e-12);
  CHECK_NEAR(Ev(L"abs(-3)"), 3, 1e-12);
  CHECK_NEAR(Ev(L"ln(e)"), 1, 1e-12);
  CHECK_NEAR(Ev(L"log(1000)"), 3, 1e-12);
  CHECK_NEAR(Ev(L"lg 100"), 2, 1e-12);
  CHECK_NEAR(Ev(L"log2(1024)"), 10, 1e-12);
  CHECK_NEAR(Ev(L"log10(0.01)"), -2, 1e-12);
  CHECK_NEAR(Ev(L"exp(0)"), 1, 1e-12);
  CHECK_NEAR(Ev(L"floor(2.7) + ceil(2.1) + round(2.5)"), 2 + 3 + 3, 1e-12);
  CHECK_NEAR(Ev(L"min(3, 1, 2)"), 1, 1e-12);
  CHECK_NEAR(Ev(L"max(3; 7; 2)"), 7, 1e-12);
  CHECK_NEAR(Ev(L"pow(2, 8)"), 256, 1e-12);
  CHECK_NEAR(Ev(L"hypot(3, 4)"), 5, 1e-12);
  CHECK_NEAR(Ev(L"atan2(1, 1)"), 0.7853981633974483, 1e-12);
  CHECK_NEAR(Ev(L"SQRT(4)"), 2, 1e-12);
  CHECK(!Ok(L"sqrt(-1)"));
  CHECK(!Ok(L"ln(0)"));
  CHECK(!Ok(L"pow(2)"));
  CHECK(!Ok(L"max 3"));
  CHECK(!Ok(L"foo(3)"));
}

TEST(Calc_Trig) {
  CHECK_NEAR(Ev(L"sin(0)"), 0, 1e-12);
  CHECK_NEAR(Ev(L"sin 30°"), 0.5, 1e-12);
  CHECK_NEAR(Ev(L"sin(30°)"), 0.5, 1e-12);
  CHECK_NEAR(Ev(L"cos(60°)"), 0.5, 1e-12);
  CHECK_NEAR(Ev(L"tan(45°)"), 1, 1e-12);
  CHECK_NEAR(Ev(L"tg 45°"), 1, 1e-12);
  CHECK_EQ(Ev(L"sin(pi)"), 0.0);  // snapped, no 1.2e-16
  CHECK_EQ(Ev(L"cos(90°)"), 0.0);
  CHECK_NEAR(Ev(L"sin 30° + 1"), 1.5, 1e-12);
  CHECK_NEAR(Ev(L"asin(1)"), 1.5707963267948966, 1e-12);
  CHECK_NEAR(Ev(L"2 sin(30°)"), 1, 1e-12);
  CHECK(!Ok(L"tan(90°)"));
  CHECK_NEAR(Ev(L"tau / 2"), 3.141592653589793, 1e-12);
  CHECK_NEAR(Ev(L"cosh(0) + sinh(0) + tanh(0)"), 1, 1e-12);
}

TEST(Calc_Literals) {
  CHECK_NEAR(Ev(L"0xFF"), 255, 0);
  CHECK_NEAR(Ev(L"0b1010"), 10, 0);
  CHECK_NEAR(Ev(L"0o17"), 15, 0);
  CHECK_NEAR(Ev(L"0xff + 1"), 256, 0);
  CHECK_NEAR(Ev(L"0xFF_FF"), 65535, 0);
  CHECK(calc::Evaluate(L"0x2A").radixLiteral);
  CHECK(IsExpr(L"0x2A"));
  CHECK(!Ok(L"0b102"));
  CHECK_NEAR(Ev(L"1e3"), 1000, 0);
  CHECK_NEAR(Ev(L"2.5e-3 * 2"), 0.005, 1e-15);
  CHECK_NEAR(Ev(L"1E+2"), 100, 0);
}

TEST(Calc_GroupingAndComma) {
  CHECK_NEAR(Ev(L"1 000 000"), 1000000, 0);
  CHECK_NEAR(Ev(L"1 000 000 / 4"), 250000, 0);
  CHECK_NEAR(Ev(L"2 500 * 2"), 5000, 0);
  CHECK_NEAR(Ev(L"1_000_000 + 1"), 1000001, 0);
  CHECK_NEAR(Ev(L"1 234,5 + 0,5"), 1235, 1e-9);
  CHECK_NEAR(Ev(L"1 234 * 2"), 2468, 0);  // thin space (our own output format)
  CHECK(!Ok(L"2024 100"));                       // head longer than 3 digits: not a grouped number
  CHECK(!Ok(L"1 00"));
  CHECK_NEAR(Ev(L"2,5 * 2"), 5, 1e-12);
  CHECK(calc::Evaluate(L"2,5 * 2").commaDecimal);
  CHECK(!calc::Evaluate(L"2.5 * 2").commaDecimal);
  CHECK_NEAR(Ev(L"1,000,000 + 1"), 1000001, 0);  // English thousands (2+ groups)
  CHECK_NEAR(Ev(L"1,000.5 * 2"), 2001, 1e-9);
  CHECK_NEAR(Ev(L"max(2,5)"), 5, 0);            // comma separates args in multi-arg calls
  CHECK_NEAR(Ev(L"max(2,5; 1)"), 2.5, 1e-12);   // ...unless ';' is used
  CHECK_NEAR(Ev(L"sqrt(6,25)"), 2.5, 1e-12);    // single-arg call: decimal comma
  CHECK_NEAR(Ev(L"max((2,5), 1)"), 2.5, 1e-12); // parentheses reset the arg mode
}

TEST(Calc_IsExpression) {
  CHECK(!IsExpr(L"2024"));
  CHECK(!IsExpr(L"42"));
  CHECK(!IsExpr(L"-5"));
  CHECK(!IsExpr(L"1 000"));
  CHECK(!IsExpr(L"2,5"));
  CHECK(!IsExpr(L"(42)"));
  CHECK(IsExpr(L"2+2"));
  CHECK(!IsExpr(L"e"));   // start of "Edge"/"Excel": no calculator result
  CHECK(!IsExpr(L"pi"));
  CHECK(IsExpr(L"=pi"));  // '=' forces a result
  CHECK(IsExpr(L"=42"));
  CHECK(IsExpr(L"2pi"));
  CHECK(IsExpr(L"pi/2"));
  CHECK(IsExpr(L"50%"));
  CHECK(IsExpr(L"5!"));
  CHECK(IsExpr(L"=42*1"));
}

TEST(Calc_ErrorsAndEdges) {
  CHECK(!Ok(L""));
  CHECK(!Ok(L"="));
  CHECK(!Ok(L"1/0"));
  CHECK(!Ok(L"5 / (3 - 3)"));
  CHECK(!Ok(L"10^400"));
  CHECK(!Ok(L"hello"));
  CHECK(!Ok(L"chrome"));
  CHECK(!Ok(L"2 apples"));
  CHECK(!Ok(L"2 +"));
  CHECK(!Ok(L"* 2"));
  CHECK(!Ok(L"1.2.3"));
  CHECK(!Ok(L"()"));
  CHECK(!Ok(L"2)"));
  CHECK(!Ok(L"192.168.1.1"));
  CHECK(!Ok(L"23:30"));
  CHECK_NEAR(Ev(L"=2+2"), 4, 0);
  CHECK_NEAR(Ev(L"2+2="), 4, 0);
  CHECK_NEAR(Ev(L"(2+3"), 5, 0);       // tolerate a missing ')' at the end while typing
  CHECK_NEAR(Ev(L"sqrt(16"), 4, 0);
  CHECK_EQ(calc::Evaluate(L"0 * -1").value, 0.0);
  std::wstring deep(200, L'(');
  deep += L"1";
  CHECK(!Ok(deep.c_str()));  // recursion limit, no crash
  std::wstring neg(500, L'-');
  neg += L"1";
  CHECK(!Ok(neg.c_str()));
}

TEST(Calc_LeadingAmount) {
  calc::Result r;
  CHECK_EQ(calc::ParseLeadingAmount(L"100 usd", r), size_t(3));
  CHECK_NEAR(r.value, 100, 0);
  CHECK_EQ(calc::ParseLeadingAmount(L"1 000 000 р", r), size_t(9));
  CHECK_NEAR(r.value, 1e6, 0);
  CHECK(calc::ParseLeadingAmount(L"100*3 usd", r) > 0);
  CHECK_NEAR(r.value, 300, 0);
  CHECK(calc::ParseLeadingAmount(L"2,5 гб", r) > 0);
  CHECK(r.commaDecimal);
  CHECK_NEAR(r.value, 2.5, 1e-12);
  CHECK(calc::ParseLeadingAmount(L"10->mi", r) == 2);
  CHECK_EQ(calc::ParseLeadingAmount(L"usd 100", r), size_t(0));
  CHECK_EQ(calc::ParseLeadingAmount(L"1.2.3 km", r), size_t(0));
}

TEST(Calc_Pretty) {
  CHECK_EQ(calc::Pretty(L" = 2*3 /  4 "), std::wstring(L"2×3 ÷ 4"));
  CHECK_EQ(calc::Pretty(L"2**3"), std::wstring(L"2^3"));
}

TEST(Calc_Speed) {
  auto t0 = std::chrono::steady_clock::now();
  double sink = 0;
  for (int i = 0; i < 10000; ++i) sink += calc::Evaluate(L"sqrt(2) * (1 000 + 15%) - sin 30° / 2^3").value;
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
  CHECK(sink != 0);
  CHECK(us / 10000.0 < 20.0);  // ~1 µs expected; generous bound
}

// ---- units ------------------------------------------------------------------------------------------------

namespace {
double Conv(const wchar_t* q, size_t idx = 0) {
  units::Answer a;
  if (!units::Convert(q, a) || idx >= a.items.size()) return -999999.0;
  return a.items[idx].to;
}
bool UnitOk(const wchar_t* q) {
  units::Answer a;
  return units::Convert(q, a);
}
std::wstring ToSym(const wchar_t* q, size_t idx = 0) {
  units::Answer a;
  if (!units::Convert(q, a) || idx >= a.items.size()) return L"?";
  return a.items[idx].toSym;
}
}  // namespace

TEST(Units_Length) {
  CHECK_NEAR(Conv(L"10 km in miles"), 6.213711922, 1e-8);
  CHECK_NEAR(Conv(L"5 футов в см"), 152.4, 1e-9);
  CHECK_NEAR(Conv(L"1 inch to cm"), 2.54, 1e-12);
  CHECK_NEAR(Conv(L"1 дюйм в мм"), 25.4, 1e-12);
  CHECK_NEAR(Conv(L"3 мили в километры"), 4.828032, 1e-9);
  CHECK_NEAR(Conv(L"100 метров в футах"), 328.0839895, 1e-6);
  CHECK_NEAR(Conv(L"10km->mi"), 6.213711922, 1e-8);
  CHECK_NEAR(Conv(L"10 км = мили"), 6.213711922, 1e-8);
  CHECK_NEAR(Conv(L"2 ярда в метры"), 1.8288, 1e-12);
  CHECK_NEAR(Conv(L"10 cm in"), 3.937007874, 1e-8);  // trailing "in" is the unit, not the separator
  CHECK_NEAR(Conv(L"5 in in cm"), 12.7, 1e-12);
  CHECK_NEAR(Conv(L"1 морская миля в км"), 1.852, 1e-12);
}

TEST(Units_DefaultTargets) {
  CHECK_NEAR(Conv(L"10 km"), 6.213711922, 1e-8);
  CHECK_EQ(ToSym(L"10 km"), std::wstring(L"миль"));
  CHECK_NEAR(Conv(L"70 кг"), 154.3235835, 1e-6);
  CHECK_NEAR(Conv(L"30°C"), 86, 1e-9);
  CHECK_NEAR(Conv(L"100 °F"), 37.77777778, 1e-7);
  CHECK_NEAR(Conv(L"90 минут"), 1.5, 1e-12);
  CHECK_NEAR(Conv(L"90 минут", 1), 5400, 1e-9);
  CHECK(!UnitOk(L"5 m"));  // one-letter unit without target: ambiguous
  CHECK(!UnitOk(L"2 c"));
  CHECK(UnitOk(L"5 m in ft"));
}

TEST(Units_Mass) {
  CHECK_NEAR(Conv(L"70 кг в фунты"), 154.3235835, 1e-6);
  CHECK_NEAR(Conv(L"1 lb to g"), 453.59237, 1e-9);
  CHECK_NEAR(Conv(L"16 унций в граммах"), 453.59237, 1e-9);
  CHECK_NEAR(Conv(L"2,5 тонны в кг"), 2500, 1e-9);
  CHECK_NEAR(Conv(L"500 мг в г"), 0.5, 1e-12);
}

TEST(Units_Temperature) {
  CHECK_NEAR(Conv(L"100 f to c"), 37.77777778, 1e-7);
  CHECK_NEAR(Conv(L"30°C в F"), 86, 1e-9);
  CHECK_NEAR(Conv(L"-40 c to f"), -40, 1e-9);
  CHECK_NEAR(Conv(L"0 градусов цельсия в фаренгейты"), 32, 1e-9);
  CHECK_NEAR(Conv(L"300 kelvin to celsius"), 26.85, 1e-9);
  CHECK_NEAR(Conv(L"451 по фаренгейту в цельсия"), 232.7777778, 1e-6);
  CHECK_NEAR(Conv(L"20 ℃ в °F"), 68, 1e-9);
}

TEST(Units_Data) {
  CHECK_NEAR(Conv(L"1.5 гб в мб"), 1536, 1e-9);
  CHECK_NEAR(Conv(L"1 TB to GB"), 1024, 1e-9);
  CHECK_NEAR(Conv(L"2048 кб в мб"), 2, 1e-12);
  CHECK_NEAR(Conv(L"100 мбит в мб"), 11.920928955, 1e-8);
  CHECK_NEAR(Conv(L"8 бит в байты"), 1, 1e-12);
  CHECK_NEAR(Conv(L"1 GiB to MiB"), 1024, 1e-9);
  CHECK_NEAR(Conv(L"1.5 гб"), 1536, 1e-9);
}

TEST(Units_TimeSpeedAreaVolume) {
  CHECK_NEAR(Conv(L"2 часа в минуты"), 120, 1e-9);
  CHECK_NEAR(Conv(L"1 день в часах"), 24, 1e-9);
  CHECK_NEAR(Conv(L"3 недели в дни"), 21, 1e-9);
  CHECK_NEAR(Conv(L"60 mph в км/ч"), 96.56064, 1e-6);
  CHECK_NEAR(Conv(L"100 км/ч в м/с"), 27.77777778, 1e-7);
  CHECK_NEAR(Conv(L"60 миль в час в км/ч"), 96.56064, 1e-6);
  CHECK_NEAR(Conv(L"90 км в час"), 55.92340730, 1e-6);  // "км в час" = km/h -> mph by default
  CHECK_NEAR(Conv(L"10 узлов в км/ч"), 18.52, 1e-9);
  CHECK_NEAR(Conv(L"1 га в м2"), 10000, 1e-9);
  CHECK_NEAR(Conv(L"6 соток в м²"), 600, 1e-9);
  CHECK_NEAR(Conv(L"100 кв м в кв футах"), 1076.391042, 1e-5);
  CHECK_NEAR(Conv(L"1 gal to l"), 3.785411784, 1e-12);
  CHECK_NEAR(Conv(L"2 л в мл"), 2000, 1e-9);
  CHECK_NEAR(Conv(L"1 куб м в литры"), 1000, 1e-9);
  CHECK_NEAR(Conv(L"8 fl oz to ml"), 236.588236, 1e-6);
}

TEST(Units_Rejects) {
  CHECK(!UnitOk(L"10 km in kg"));  // category mismatch
  CHECK(!UnitOk(L"10 apples"));
  CHECK(!UnitOk(L"chrome"));
  CHECK(!UnitOk(L"2024"));
  CHECK(!UnitOk(L"10 km in miles please"));
  CHECK(!UnitOk(L"100 usd"));
  units::Answer a;
  CHECK(units::Convert(L"2,5 км в м", a) && a.commaDecimal);
}

TEST(Units_Format) {
  CHECK_EQ(units::FormatValue(6.21371192, L','), std::wstring(L"6,2137"));
  CHECK_EQ(units::FormatValue(1536, L'.'), std::wstring(L"1 536"));
  CHECK_EQ(units::FormatValue(0.000123456, L'.'), std::wstring(L"0.0001235"));
  CHECK_EQ(units::FormatValue(0, L'.'), std::wstring(L"0"));
}
