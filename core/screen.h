// FaiOS core: UI-toolkit-independent screen model. No LVGL/SDL/Linux includes here.
#pragma once
#include <string>
#include <vector>

namespace fai {

// Logical feature-phone keys. Num0..Num9 must stay contiguous and last.
enum class Key { Up, Down, Left, Right, Ok, Back, Soft1, Soft2, Call, End, Star, Pound,
                 Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9, Count };

inline int digitOf(Key k) {
  return (k >= Key::Num0 && k <= Key::Num9) ? int(k) - int(Key::Num0) : -1;
}
const char* keyName(Key k);
bool keyFromName(const std::string& name, Key& out);  // used for the configurable keymap

struct Softkeys { std::string left, center, right; };

enum class Layout { List, Grid, Home, Text, Dial, Clock, Calc, Camera, Battery };

// One list/grid entry. lead: icon name ("call", "wifi", ...) or "@A" for a letter avatar.
struct Row {
  std::string text, sub, lead, trail;
  int toggle = -1;      // -1 none, 0 off, 1 on (drawn as a switch)
  bool unread = false;  // accent dot
  int tone = 0;         // 0 normal, 1 danger
};

// What a screen wants drawn. The renderer (LVGL today) is dumb and just draws this.
//   List/Grid: rows + selected.  Home: lines = {time, date[, badge text]}.
//   Text: lines (wrapped body) + optional lead avatar.  Dial: lines = {number, note}.
//   Clock: lines = {HH:MM, date, stopwatch}.  Calc: lines = {pending expression, display}.  Camera: lines = {note, photo count}.  Battery: lines = {percent, state, voltage, temperature, note}.
struct View {
  std::string title;
  Layout layout = Layout::List;
  std::vector<Row> rows;
  std::vector<std::string> lines;
  std::string lead;
  int selected = -1;
  Softkeys keys;
};

class Screen {
 public:
  virtual ~Screen() = default;
  // Return true if the key was consumed. Unconsumed Back/Soft2 pops the screen.
  virtual bool onKey(Key k) = 0;
  virtual View view() const = 0;
};

}  // namespace fai
