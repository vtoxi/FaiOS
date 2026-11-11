// Calendar maths (Gregorian), no time library needed, so it is deterministic and testable.
#pragma once

namespace fai {

struct Date { int y, m, d; };

inline bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

inline int daysInMonth(int y, int m) {
  static const int n[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && isLeap(y) ? 29 : n[m - 1];
}

// 0 = Sunday ... 6 = Saturday (Sakamoto's method).
inline int weekday(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

inline Date addDays(Date a, int n) {  // small |n| (steps of one day)
  for (; n > 0; n--) {
    if (++a.d > daysInMonth(a.y, a.m)) { a.d = 1; if (++a.m > 12) { a.m = 1; a.y++; } }
  }
  for (; n < 0; n++) {
    if (--a.d < 1) { if (--a.m < 1) { a.m = 12; a.y--; } a.d = daysInMonth(a.y, a.m); }
  }
  return a;
}

}  // namespace fai
