#include "Test.h"
#include "logic/CurrencyQuery.h"

using namespace cs;

namespace {

const char* kRatesJson = R"({
  "result": "success",
  "time_last_update_unix": 1760000000,
  "base_code": "USD",
  "rates": {"USD": 1, "RUB": 92.455, "EUR": 0.9, "GBP": 0.8, "CNY": 7.2, "KZT": 500, "JPY": 150,
            "UAH": 41, "BYN": 3.3, "CZK": 23, "SEK": 10.5, "INR": 84, "TRY": 34, "AMD": 390, "XAU": 0.0004}
})";

const currency::Rates& R() {
  static currency::Rates r = [] {
    currency::Rates x;
    currency::ParseRates(kRatesJson, x);
    return x;
  }();
  return r;
}

struct P {
  bool ok;
  currency::Query q;
};
P Q(const wchar_t* text, const wchar_t* base = L"RUB", bool withRates = true) {
  P p;
  p.ok = currency::Parse(text, base, withRates ? &R() : nullptr, p.q);
  return p;
}
std::wstring To(const P& p, size_t i = 0) { return i < p.q.to.size() ? p.q.to[i] : L"-"; }

}  // namespace

TEST(Currency_ParseRates) {
  const auto& r = R();
  CHECK_EQ(r.base, std::wstring(L"USD"));
  CHECK_EQ(r.updatedUnix, int64_t(1760000000));
  CHECK(r.Has(L"RUB"));
  CHECK(!r.Has(L"BTC"));
  double v = 0;
  CHECK(r.Convert(100, L"USD", L"RUB", v));
  CHECK_NEAR(v, 9245.5, 1e-9);
  CHECK(r.Convert(90, L"EUR", L"USD", v));
  CHECK_NEAR(v, 100, 1e-9);
  CHECK(r.Convert(1, L"EUR", L"RUB", v));
  CHECK_NEAR(v, 92.455 / 0.9, 1e-9);
  CHECK(!r.Convert(1, L"EUR", L"BTC", v));

  currency::Rates bad;
  CHECK(!currency::ParseRates(R"({"result":"error","error-type":"x"})", bad));
  CHECK(!currency::ParseRates("not json", bad));
  CHECK(!currency::ParseRates(R"({"rates":{}})", bad));
  currency::Rates eur;  // frankfurter-like: base without itself in the table
  CHECK(currency::ParseRates(R"({"base":"eur","date":"2025-01-01","rates":{"USD":1.1,"rub":100}})", eur));
  CHECK(eur.Has(L"EUR") && eur.Has(L"RUB"));
  CHECK(eur.Convert(1.1, L"USD", L"RUB", v));
  CHECK_NEAR(v, 100, 1e-9);
}

TEST(Currency_Basic) {
  auto p = Q(L"100 usd");
  CHECK(p.ok);
  CHECK_NEAR(p.q.amount, 100, 0);
  CHECK_EQ(p.q.from, std::wstring(L"USD"));
  CHECK_EQ(To(p), std::wstring(L"RUB"));
  CHECK(!p.q.explicitTarget);

  p = Q(L"100$");
  CHECK(p.ok && p.q.from == L"USD" && p.q.amount == 100);
  p = Q(L"$100");
  CHECK(p.ok && p.q.from == L"USD" && p.q.amount == 100);
  p = Q(L"€50");
  CHECK(p.ok && p.q.from == L"EUR" && p.q.amount == 50);
  p = Q(L"€ 50 в долларах");
  CHECK(p.ok && p.q.from == L"EUR" && To(p) == L"USD");
  p = Q(L"100 USD to EUR");
  CHECK(p.ok && p.q.from == L"USD" && To(p) == L"EUR" && p.q.explicitTarget);
  p = Q(L"100 евро в usd");
  CHECK(p.ok && p.q.from == L"EUR" && To(p) == L"USD");
  p = Q(L"50£");
  CHECK(p.ok && p.q.from == L"GBP");
  p = Q(L"100₸");
  CHECK(p.ok && p.q.from == L"KZT");
  p = Q(L"100 zł");
  CHECK(p.ok && p.q.from == L"PLN");
}

TEST(Currency_RussianNames) {
  auto p = Q(L"100 долларов");
  CHECK(p.ok && p.q.from == L"USD" && To(p) == L"RUB");
  p = Q(L"100 баксов в рублях");
  CHECK(p.ok && p.q.from == L"USD" && To(p) == L"RUB" && p.q.explicitTarget);
  p = Q(L"5000 тенге в рубли");
  CHECK(p.ok && p.q.from == L"KZT" && To(p) == L"RUB");
  p = Q(L"1 доллар в евро");
  CHECK(p.ok && To(p) == L"EUR");
  p = Q(L"200 юаней");
  CHECK(p.ok && p.q.from == L"CNY");
  p = Q(L"1000 иен в рублях");
  CHECK(p.ok && p.q.from == L"JPY");
  p = Q(L"10 фунтов стерлингов в евро");
  CHECK(p.ok && p.q.from == L"GBP" && To(p) == L"EUR");
  p = Q(L"100 белорусских рублей в рубли");
  CHECK(p.ok && p.q.from == L"BYN" && To(p) == L"RUB");
  p = Q(L"100 гривен");
  CHECK(p.ok && p.q.from == L"UAH");
  p = Q(L"100 шведских крон в евро");
  CHECK(p.ok && p.q.from == L"SEK");
  p = Q(L"100 крон");
  CHECK(p.ok && p.q.from == L"CZK");
  p = Q(L"500 руб. в $");
  CHECK(p.ok && p.q.from == L"RUB" && To(p) == L"USD");
  p = Q(L"100 р");
  CHECK(p.ok && p.q.from == L"RUB");
  p = Q(L"10 dollars to rubles");
  CHECK(p.ok && p.q.from == L"USD" && To(p) == L"RUB");
  p = Q(L"10 ДОЛЛАРОВ В РУБЛИ");
  CHECK(p.ok && p.q.from == L"USD");
}

TEST(Currency_Multipliers) {
  auto p = Q(L"1к юаней в рубли");
  CHECK(p.ok && p.q.amount == 1000 && p.q.from == L"CNY");
  p = Q(L"1k usd");
  CHECK(p.ok && p.q.amount == 1000);
  p = Q(L"2 млн рублей в долларах");
  CHECK(p.ok && p.q.amount == 2e6 && p.q.from == L"RUB" && To(p) == L"USD");
  p = Q(L"1,5м евро");
  CHECK(p.ok && p.q.amount == 1.5e6 && p.q.commaDecimal);
  p = Q(L"3 тыс. долларов");
  CHECK(p.ok && p.q.amount == 3000);
  p = Q(L"100*3 usd");
  CHECK(p.ok && p.q.amount == 300);
  p = Q(L"1 000 000 р в usd");
  CHECK(p.ok && p.q.amount == 1e6);
}

TEST(Currency_Defaults) {
  auto p = Q(L"100 рублей");  // base currency -> USD and EUR
  CHECK(p.ok && p.q.to.size() == 2 && To(p, 0) == L"USD" && To(p, 1) == L"EUR");
  p = Q(L"100 usd", L"EUR");
  CHECK(p.ok && To(p) == L"EUR");
  p = Q(L"usd в рубли");  // no amount -> 1
  CHECK(p.ok && p.q.amount == 1 && !p.q.hasAmount && To(p) == L"RUB");
  p = Q(L"100 usd в eur и gbp");
  CHECK(p.ok && p.q.to.size() == 2 && To(p, 1) == L"GBP");
  p = Q(L"100 usd -> eur, rub");
  CHECK(p.ok && p.q.to.size() == 2);
}

TEST(Currency_Rejects) {
  CHECK(!Q(L"100").ok);
  CHECK(!Q(L"2024").ok);
  CHECK(!Q(L"usd").ok);    // no amount, no target
  CHECK(!Q(L"евро").ok);
  CHECK(!Q(L"chrome").ok);
  CHECK(!Q(L"100 km").ok);
  CHECK(!Q(L"100 usd в км").ok);
  CHECK(!Q(L"100 usd в usd").ok);
  CHECK(!Q(L"1.2.3 usd").ok);
  CHECK(!Q(L"5 м").ok);
  CHECK(!Q(L"100 xyz").ok);
  CHECK(!Q(L"1 cup in ml").ok);
  CHECK(!Q(L"").ok);
}

TEST(Currency_IsoCodesFromRates) {
  CHECK(Q(L"1 xau").ok);              // in the rates table, not in the dictionary
  CHECK(!Q(L"1 xau", L"RUB", false).ok);  // no rates: only dictionary codes
  CHECK(Q(L"1 usd", L"RUB", false).ok);
  CHECK(Q(L"1 btc", L"RUB", false).ok);   // dictionary knows it; provider skips it when absent from rates
}

TEST(Currency_Format) {
  CHECK_EQ(currency::FormatMoney(9245.5), std::wstring(L"9 245,50"));
  CHECK_EQ(currency::FormatMoney(100), std::wstring(L"100,00"));
  CHECK_EQ(currency::FormatMoney(1234567.891), std::wstring(L"1 234 567,89"));
  CHECK_EQ(currency::FormatMoney(0.0108), std::wstring(L"0,0108"));
  CHECK_EQ(currency::FormatMoney(0.5), std::wstring(L"0,5"));
  CHECK_EQ(currency::FormatMoney(0), std::wstring(L"0,00"));
  CHECK_EQ(currency::FormatMoney(-12.3, L'.'), std::wstring(L"-12.30"));
  CHECK_EQ(std::wstring(currency::Symbol(L"RUB")), std::wstring(L"₽"));
  CHECK(currency::Symbol(L"SEK") == nullptr);
}
