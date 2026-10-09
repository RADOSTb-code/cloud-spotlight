#pragma once
// Calculator: allocation-free recursive-descent evaluator (no eval). Portable, no Windows headers.
//
// Syntax: + - * / × ÷ · x ^ ** (right-assoc) mod, unary ±, parentheses, implicit multiplication (2(3+4), 2pi,
// (1+2)(3+4)), postfix % ("50%" = 0.5; "200 + 15%" = 230, "200 - 10%" = 180, "200 * 15%" = 30), "a % b" = modulo,
// ! factorial, ° degrees, ² ³, constants pi π e tau, functions sqrt √ cbrt abs sin cos tan cot asin acos atan
// sinh cosh tanh ln log lg log2 exp floor ceil round sign min max pow hypot atan2 (with or without parens:
// "sin 30°", "sqrt(16)"), literals 0xFF 0b1010 0o17 1e3 .5, grouping "1 000 000" / "1_000" / "1,000,000",
// comma decimal "2,5" (inside multi-argument calls the comma separates arguments; ';' always does).
// A leading "=" forces a result ("=pi"); a trailing "=" is ignored. A bare constant ("e", "pi") is not an
// expression (it is usually the start of an app name). Division by zero, NaN, overflow, garbage -> ok == false.
#include <string>
#include <string_view>

namespace cs::calc {

struct Result {
  bool ok = false;
  double value = 0;
  bool isExpression = false;  // false for a bare number ("2024", "-5", "1 000", "2,5") — nothing to show
  bool commaDecimal = false;  // input used ',' as decimal separator -> format the answer with ','
  bool radixLiteral = false;  // input had 0x/0b/0o literals -> also show the result in hex
};

Result Evaluate(std::wstring_view expr);

// Splits a leading amount ("100", "1 000,5", "100*3", "-2.5") off `text` (amounts for unit/currency queries).
// Returns the number of chars consumed (0 if there is no amount) and the evaluation result in `out`.
size_t ParseLeadingAmount(std::wstring_view text, Result& out);

// Normalised expression for display: trimmed, '=' stripped, whitespace collapsed, "**" -> "^", '*' -> '×',
// '/' -> '÷'.
std::wstring Pretty(std::wstring_view expr);

}  // namespace cs::calc
