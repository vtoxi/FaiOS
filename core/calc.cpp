#include "calc.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fai {

std::string Calc::fmt(double v) {
  char b[40];
  std::snprintf(b, sizeof b, "%.10g", v);
  std::string s = b;
  return s == "-0" ? "0" : s;
}

void Calc::digit(int d) {
  if (err_) clear();
  if (d < 0 || d > 9) return;
  if (fresh_) { entry_.clear(); fresh_ = false; }
  if (entry_.size() >= 12) return;
  if (entry_ == "0") entry_ = std::string(1, char('0' + d));
  else entry_ += char('0' + d);
}

void Calc::dot() {
  if (err_) clear();
  if (fresh_) { entry_ = "0."; fresh_ = false; return; }
  if (entry_.find('.') == std::string::npos && entry_.size() < 12) entry_ += '.';
}

bool Calc::apply() {
  double cur = std::strtod(entry_.c_str(), nullptr), r = cur;
  switch (op_) {
    case '+': r = acc_ + cur; break;
    case '-': r = acc_ - cur; break;
    case '*': r = acc_ * cur; break;
    case '/': if (cur == 0) { err_ = true; return false; } r = acc_ / cur; break;
    default: break;
  }
  if (!std::isfinite(r)) { err_ = true; return false; }
  acc_ = r;
  entry_ = fmt(r);
  return true;
}

void Calc::op(char o) {
  if (err_) return;
  if (op_ && !fresh_) { if (!apply()) return; }          // chain: resolve the pending op first
  else if (!op_) acc_ = std::strtod(entry_.c_str(), nullptr);
  op_ = o;
  fresh_ = true;
}

void Calc::equals() {
  if (err_ || !op_) return;
  if (!apply()) return;
  op_ = 0;
  fresh_ = true;
}

void Calc::backspace() {
  if (!canBackspace()) return;
  entry_.pop_back();
  if (entry_.empty() || entry_ == "-") entry_ = "0";
}

void Calc::clear() {
  acc_ = 0; op_ = 0; entry_ = "0"; fresh_ = true; err_ = false;
}

std::string Calc::expr() const {
  if (!op_ || err_) return "";
  return fmt(acc_) + " " + (op_ == '*' ? 'x' : op_);
}

}  // namespace fai
