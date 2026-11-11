#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fai {

static const Json kNull;

const Json& Json::operator[](const std::string& key) const {
  if (t_ == Object)
    for (auto& m : o_)
      if (m.first == key) return m.second;
  return kNull;
}

const Json& Json::at(size_t i) const { return t_ == Array && i < a_.size() ? a_[i] : kNull; }

Json& Json::set(const std::string& key, Json v) {
  if (t_ != Object) { *this = makeObject(); }
  for (auto& m : o_)
    if (m.first == key) { m.second = std::move(v); return *this; }
  o_.emplace_back(key, std::move(v));
  return *this;
}

Json& Json::push(Json v) {
  if (t_ != Array) { *this = makeArray(); }
  a_.push_back(std::move(v));
  return *this;
}

namespace {

struct Parser {
  const std::string& s;
  size_t p = 0;
  std::string err;
  explicit Parser(const std::string& text) : s(text) {}

  bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at offset " + std::to_string(p); return false; }
  void ws() { while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) p++; }
  bool lit(const char* w) {
    size_t n = std::char_traits<char>::length(w);
    if (s.compare(p, n, w) != 0) return false;
    p += n;
    return true;
  }

  static void utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
  }

  bool hex4(unsigned& v) {
    if (p + 4 > s.size()) return fail("short \\u escape");
    v = 0;
    for (int i = 0; i < 4; i++) {
      char c = s[p++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else return fail("bad \\u escape");
    }
    return true;
  }

  bool str(std::string& out) {
    if (s[p] != '"') return fail("expected string");
    p++;
    while (p < s.size()) {
      unsigned char c = s[p++];
      if (c == '"') return true;
      if (c < 0x20) return fail("control character in string");
      if (c != '\\') { out += char(c); continue; }
      if (p >= s.size()) break;
      char e = s[p++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp < 0xDC00) {  // surrogate pair
            unsigned lo;
            if (s.compare(p, 2, "\\u") != 0) return fail("lone surrogate");
            p += 2;
            if (!hex4(lo)) return false;
            if (lo < 0xDC00 || lo > 0xDFFF) return fail("bad surrogate pair");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) return fail("lone surrogate");
          utf8(out, cp);
          break;
        }
        default: return fail("bad escape");
      }
    }
    return fail("unterminated string");
  }

  bool value(Json& out, int depth) {
    if (depth > 64) return fail("nesting too deep");
    ws();
    if (p >= s.size()) return fail("unexpected end");
    char c = s[p];
    if (c == '{') {
      p++;
      out = Json::makeObject();
      ws();
      if (p < s.size() && s[p] == '}') { p++; return true; }
      for (;;) {
        ws();
        std::string k;
        if (p >= s.size() || !str(k)) return fail("expected key");
        ws();
        if (p >= s.size() || s[p++] != ':') return fail("expected ':'");
        Json v;
        if (!value(v, depth + 1)) return false;
        out.set(k, std::move(v));
        ws();
        if (p >= s.size()) return fail("unterminated object");
        if (s[p] == ',') { p++; continue; }
        if (s[p] == '}') { p++; return true; }
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      p++;
      out = Json::makeArray();
      ws();
      if (p < s.size() && s[p] == ']') { p++; return true; }
      for (;;) {
        Json v;
        if (!value(v, depth + 1)) return false;
        out.push(std::move(v));
        ws();
        if (p >= s.size()) return fail("unterminated array");
        if (s[p] == ',') { p++; continue; }
        if (s[p] == ']') { p++; return true; }
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') { std::string v; if (!str(v)) return false; out = Json::makeString(std::move(v)); return true; }
    if (lit("true")) { out = Json::makeBool(true); return true; }
    if (lit("false")) { out = Json::makeBool(false); return true; }
    if (lit("null")) { out = Json(); return true; }
    if (c == '-' || (c >= '0' && c <= '9')) {  // validate the RFC 8259 number grammar, then convert
      size_t st = p;
      if (s[p] == '-') p++;
      if (p >= s.size()) return fail("bad number");
      if (s[p] == '0') p++;
      else if (s[p] >= '1' && s[p] <= '9') while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
      else return fail("bad number");
      if (p < s.size() && s[p] == '.') {
        p++;
        size_t d = p;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
        if (p == d) return fail("bad number");
      }
      if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
        p++;
        if (p < s.size() && (s[p] == '+' || s[p] == '-')) p++;
        size_t d = p;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
        if (p == d) return fail("bad number");
      }
      out = Json::makeNumber(std::strtod(s.substr(st, p - st).c_str(), nullptr));
      return true;
    }
    return fail("unexpected character");
  }
};

void escape(std::string& out, const std::string& s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += char(c);
    }
  }
  out += '"';
}

}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string* err) {
  Parser ps(text);
  Json v;
  bool ok = ps.value(v, 0);
  if (ok) { ps.ws(); if (ps.p != text.size()) ok = ps.fail("trailing data"); }
  if (ok) out = std::move(v);
  else if (err) *err = ps.err;
  return ok;
}

std::string Json::dump() const {
  std::string out;
  switch (t_) {
    case Null: out = "null"; break;
    case Bool: out = b_ ? "true" : "false"; break;
    case Number: {
      char b[40];
      if (std::floor(n_) == n_ && std::fabs(n_) < 1e15) std::snprintf(b, sizeof b, "%lld", (long long)n_);
      else std::snprintf(b, sizeof b, "%.17g", n_);
      out = b;
      break;
    }
    case String: escape(out, s_); break;
    case Array:
      out = "[";
      for (size_t i = 0; i < a_.size(); i++) out += (i ? "," : "") + a_[i].dump();
      out += "]";
      break;
    case Object:
      out = "{";
      for (size_t i = 0; i < o_.size(); i++) { if (i) out += ","; escape(out, o_[i].first); out += ":" + o_[i].second.dump(); }
      out += "}";
      break;
  }
  return out;
}

}  // namespace fai
