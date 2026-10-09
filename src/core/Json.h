#pragma once
// Minimal JSON DOM (parse + serialize). Input/output is UTF-8; strings are stored as std::wstring.
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cs::json {

class Value {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Value() = default;
  static Value Bool(bool b);
  static Value Number(double d);
  static Value String(std::wstring s);
  static Value Array();
  static Value Object();

  Type type() const { return type_; }
  bool IsNull() const { return type_ == Type::Null; }
  bool IsObject() const { return type_ == Type::Object; }
  bool IsArray() const { return type_ == Type::Array; }
  bool IsString() const { return type_ == Type::String; }
  bool IsNumber() const { return type_ == Type::Number; }
  bool IsBool() const { return type_ == Type::Bool; }

  bool AsBool(bool def = false) const { return type_ == Type::Bool ? b_ : def; }
  double AsNumber(double def = 0) const { return type_ == Type::Number ? n_ : def; }
  int AsInt(int def = 0) const { return type_ == Type::Number ? int(n_) : def; }
  const std::wstring& AsString() const;  // empty string if not a string

  // Object access. Missing key / non-object -> reference to a static Null value.
  const Value& operator[](std::string_view key) const;
  bool Has(std::string_view key) const;
  Value& Set(std::string key, Value v);  // turns Null into Object
  const std::vector<std::pair<std::string, Value>>& Members() const { return obj_; }

  // Array access
  size_t Size() const { return type_ == Type::Array ? arr_.size() : 0; }
  const Value& At(size_t i) const;
  Value& Push(Value v);  // turns Null into Array
  const std::vector<Value>& Items() const { return arr_; }

 private:
  Type type_ = Type::Null;
  bool b_ = false;
  double n_ = 0;
  std::wstring s_;
  std::vector<Value> arr_;
  std::vector<std::pair<std::string, Value>> obj_;  // insertion-ordered
};

// Returns false on syntax error (out is left Null). Accepts // and /* */ comments and trailing commas.
bool Parse(std::string_view utf8, Value& out, std::string* error = nullptr);
std::string Serialize(const Value& v, bool pretty = true);

}  // namespace cs::json
