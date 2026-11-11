// Calculator logic (no UI). Chain semantics: 2 + 3 * 4 = 20 (left to right), like a basic phone calculator.
#pragma once
#include <string>

namespace fai {

class Calc {
 public:
  void digit(int d);
  void dot();
  void op(char o);  // '+', '-', '*', '/'
  void equals();
  void backspace();
  void clear();
  std::string display() const { return err_ ? "Error" : entry_; }
  std::string expr() const;       // e.g. "12 x" while an operator is pending, else ""
  bool canBackspace() const { return !err_ && !fresh_; }

 private:
  bool apply();  // acc_ = acc_ <op_> current entry; false on error
  static std::string fmt(double v);
  double acc_ = 0;
  char op_ = 0;
  std::string entry_ = "0";
  bool fresh_ = true;  // next digit starts a new number
  bool err_ = false;
};

}  // namespace fai
