// Minimal JSON value, parser and serializer (RFC 8259). Used for OAuth responses and Google API payloads.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace fai {

class Json {
 public:
  enum Type { Null, Bool, Number, String, Array, Object };

  Json() = default;
  static Json makeBool(bool b) { Json j; j.t_ = Bool; j.b_ = b; return j; }
  static Json makeNumber(double n) { Json j; j.t_ = Number; j.n_ = n; return j; }
  static Json makeString(std::string s) { Json j; j.t_ = String; j.s_ = std::move(s); return j; }
  static Json makeArray() { Json j; j.t_ = Array; return j; }
  static Json makeObject() { Json j; j.t_ = Object; return j; }

  Type type() const { return t_; }
  bool isNull() const { return t_ == Null; }
  bool isObject() const { return t_ == Object; }
  bool isArray() const { return t_ == Array; }

  // Safe accessors: wrong type or missing key gives the default / a Null value, never a crash.
  const Json& operator[](const std::string& key) const;
  const Json& at(size_t i) const;
  size_t size() const { return t_ == Array ? a_.size() : t_ == Object ? o_.size() : 0; }
  std::string asString(const std::string& def = "") const { return t_ == String ? s_ : def; }
  double asNumber(double def = 0) const { return t_ == Number ? n_ : def; }
  bool asBool(bool def = false) const { return t_ == Bool ? b_ : def; }
  const std::vector<Json>& items() const { return a_; }
  const std::vector<std::pair<std::string, Json>>& members() const { return o_; }

  Json& set(const std::string& key, Json v);  // object: insert or replace
  Json& push(Json v);                         // array: append

  static bool parse(const std::string& text, Json& out, std::string* err = nullptr);
  std::string dump() const;

 private:
  Type t_ = Null;
  bool b_ = false;
  double n_ = 0;
  std::string s_;
  std::vector<Json> a_;
  std::vector<std::pair<std::string, Json>> o_;
};

}  // namespace fai
