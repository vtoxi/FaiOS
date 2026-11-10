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

// What a screen wants drawn. The renderer (LVGL today) is dumb and just draws this.
struct View {
  std::string title;
  std::vector<std::string> lines;
  int selected = -1;  // highlighted line, -1 = none
  bool home = false;  // home screen uses the big-clock layout
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
