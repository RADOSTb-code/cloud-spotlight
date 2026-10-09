#include "core/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/Str.h"

namespace cs::json {

namespace {
const Value kNull;
const std::wstring kEmpty;
}  // namespace

Value Value::Bool(bool b) { Value v; v.type_ = Type::Bool; v.b_ = b; return v; }
Value Value::Number(double d) { Value v; v.type_ = Type::Number; v.n_ = d; return v; }
Value Value::String(std::wstring s) { Value v; v.type_ = Type::String; v.s_ = std::move(s); return v; }
Value Value::Array() { Value v; v.type_ = Type::Array; return v; }
Value Value::Object() { Value v; v.type_ = Type::Object; return v; }

const std::wstring& Value::AsString() const { return type_ == Type::String ? s_ : kEmpty; }

const Value& Value::operator[](std::string_view key) const {
  if (type_ != Type::Object) return kNull;
  for (auto& [k, v] : obj_)
    if (k == key) return v;
  return kNull;
}

bool Value::Has(std::string_view key) const {
  if (type_ != Type::Object) return false;
  for (auto& kv : obj_)
    if (kv.first == key) return true;
  return false;
}

Value& Value::Set(std::string key, Value v) {
  if (type_ == Type::Null) type_ = Type::Object;
  for (auto& kv : obj_)
    if (kv.first == key) { kv.second = std::move(v); return kv.second; }
  obj_.emplace_back(std::move(key), std::move(v));
  return obj_.back().second;
}

const Value& Value::At(size_t i) const { return (type_ == Type::Array && i < arr_.size()) ? arr_[i] : kNull; }

Value& Value::Push(Value v) {
  if (type_ == Type::Null) type_ = Type::Array;
  arr_.push_back(std::move(v));
  return arr_.back();
}

namespace {

struct Parser {
  std::string_view s;
  size_t i = 0;
  std::string err;

  void SkipWs() {
    for (;;) {
      while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
      if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
        while (i < s.size() && s[i] != '\n') ++i;
      } else if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
        size_t e = s.find("*/", i + 2);
        i = e == std::string_view::npos ? s.size() : e + 2;
      } else {
        break;
      }
    }
  }

  bool Fail(const char* m) {
    if (err.empty()) err = std::string(m) + " at offset " + std::to_string(i);
    return false;
  }

  static void AppendCp(std::string& out, unsigned cp) {
    if (cp < 0x80) out.push_back(char(cp));
    else if (cp < 0x800) { out.push_back(char(0xC0 | (cp >> 6))); out.push_back(char(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
      out.push_back(char(0xE0 | (cp >> 12)));
      out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(char(0xF0 | (cp >> 18)));
      out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(char(0x80 | (cp & 0x3F)));
    }
  }

  bool Hex4(unsigned& out) {
    if (i + 4 > s.size()) return Fail("bad \\u escape");
    out = 0;
    for (int k = 0; k < 4; ++k) {
      char c = s[i++];
      out <<= 4;
      if (c >= '0' && c <= '9') out |= unsigned(c - '0');
      else if (c >= 'a' && c <= 'f') out |= unsigned(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') out |= unsigned(c - 'A' + 10);
      else return Fail("bad hex digit");
    }
    return true;
  }

  bool ParseString(std::string& out) {
    ++i;  // opening quote
    while (i < s.size()) {
      char c = s[i++];
      if (c == '"') return true;
      if (c != '\\') { out.push_back(c); continue; }
      if (i >= s.size()) break;
      char e = s[i++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          unsigned cp = 0;
          if (!Hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            i += 2;
            unsigned lo = 0;
            if (!Hex4(lo)) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          AppendCp(out, cp);
          break;
        }
        default: return Fail("bad escape");
      }
    }
    return Fail("unterminated string");
  }

  bool ParseValue(Value& out, int depth) {
    if (depth > 64) return Fail("too deep");
    SkipWs();
    if (i >= s.size()) return Fail("unexpected end");
    char c = s[i];
    if (c == '{') {
      ++i;
      out = Value::Object();
      for (;;) {
        SkipWs();
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        if (i >= s.size() || s[i] != '"') return Fail("expected key");
        std::string key;
        if (!ParseString(key)) return false;
        SkipWs();
        if (i >= s.size() || s[i] != ':') return Fail("expected ':'");
        ++i;
        Value v;
        if (!ParseValue(v, depth + 1)) return false;
        out.Set(std::move(key), std::move(v));
        SkipWs();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        return Fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      ++i;
      out = Value::Array();
      for (;;) {
        SkipWs();
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        Value v;
        if (!ParseValue(v, depth + 1)) return false;
        out.Push(std::move(v));
        SkipWs();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        return Fail("expected ',' or ']'");
      }
    }
    if (c == '"') {
      std::string str;
      if (!ParseString(str)) return false;
      out = Value::String(str::Utf8ToWide(str));
      return true;
    }
    if (s.compare(i, 4, "true") == 0) { i += 4; out = Value::Bool(true); return true; }
    if (s.compare(i, 5, "false") == 0) { i += 5; out = Value::Bool(false); return true; }
    if (s.compare(i, 4, "null") == 0) { i += 4; out = Value(); return true; }
    if (c == '-' || (c >= '0' && c <= '9')) {
      size_t b = i;
      ++i;
      while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                              s[i] == '+' || s[i] == '-'))
        ++i;
      std::string num(s.substr(b, i - b));
      char* end = nullptr;
      double d = std::strtod(num.c_str(), &end);
      if (!end || *end) return Fail("bad number");
      out = Value::Number(d);
      return true;
    }
    return Fail("unexpected character");
  }
};

void Escape(std::string& out, const std::string& s) {
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(char(c));
        }
    }
  }
  out.push_back('"');
}

void Write(std::string& out, const Value& v, bool pretty, int indent) {
  auto nl = [&](int lvl) {
    if (!pretty) return;
    out.push_back('\n');
    out.append(size_t(lvl) * 2, ' ');
  };
  switch (v.type()) {
    case Value::Type::Null: out += "null"; break;
    case Value::Type::Bool: out += v.AsBool() ? "true" : "false"; break;
    case Value::Type::Number: {
      double d = v.AsNumber();
      char buf[32];
      if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) std::snprintf(buf, sizeof buf, "%.0f", d);
      else std::snprintf(buf, sizeof buf, "%.17g", d);
      out += buf;
      break;
    }
    case Value::Type::String: Escape(out, str::WideToUtf8(v.AsString())); break;
    case Value::Type::Array: {
      if (v.Items().empty()) { out += "[]"; break; }
      bool simple = true;
      for (auto& it : v.Items())
        if (it.IsArray() || it.IsObject()) simple = false;
      out.push_back('[');
      bool first = true;
      for (auto& it : v.Items()) {
        if (!first) out.push_back(',');
        if (simple) { if (!first && pretty) out.push_back(' '); }
        else nl(indent + 1);
        first = false;
        Write(out, it, pretty, indent + 1);
      }
      if (!simple) nl(indent);
      out.push_back(']');
      break;
    }
    case Value::Type::Object: {
      if (v.Members().empty()) { out += "{}"; break; }
      out.push_back('{');
      bool first = true;
      for (auto& [k, m] : v.Members()) {
        if (!first) out.push_back(',');
        first = false;
        nl(indent + 1);
        Escape(out, k);
        out += pretty ? ": " : ":";
        Write(out, m, pretty, indent + 1);
      }
      nl(indent);
      out.push_back('}');
      break;
    }
  }
}

}  // namespace

bool Parse(std::string_view utf8, Value& out, std::string* error) {
  if (utf8.size() >= 3 && (unsigned char)utf8[0] == 0xEF && (unsigned char)utf8[1] == 0xBB &&
      (unsigned char)utf8[2] == 0xBF)
    utf8.remove_prefix(3);
  Parser p{utf8};
  Value v;
  bool ok = p.ParseValue(v, 0);
  if (ok) {
    p.SkipWs();
    if (p.i != utf8.size()) ok = p.Fail("trailing data");
  }
  if (!ok) {
    if (error) *error = p.err;
    out = Value();
    return false;
  }
  out = std::move(v);
  return true;
}

std::string Serialize(const Value& v, bool pretty) {
  std::string out;
  Write(out, v, pretty, 0);
  if (pretty) out.push_back('\n');
  return out;
}

}  // namespace cs::json
