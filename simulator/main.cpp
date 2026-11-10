// FaiOS desktop simulator: LVGL + SDL2 renderer over the toolkit-independent core.
// Display size is a generic QVGA placeholder, NOT a Jazz Digit 4G spec.
#include <SDL2/SDL.h>
#include <lvgl.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>

#include "screens.h"

using namespace fai;

static const int W = 240, H = 320, BAR = 22, SOFT = 26, ROW = 28;
static const uint32_t C_BG = 0x101418, C_BAR = 0x1c2128, C_ACC = 0x2f81f7, C_TXT = 0xe6edf3, C_DIM = 0x8b949e;

static AppManager g_mgr;
static bool g_dirty = true, g_running = true;
static std::map<SDL_Keycode, Key> g_keys;

static lv_obj_t* box(lv_obj_t* p, int x, int y, int w, int h, uint32_t c) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t* text(lv_obj_t* p, const std::string& s, const lv_font_t* f, uint32_t c, int x, int y) {
  lv_obj_t* l = lv_label_create(p);
  lv_label_set_text(l, s.c_str());
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

static void render() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
  const View v = g_mgr.top()->view();
  const SystemStatus st = g_mgr.status();

  // status bar: clock | signal bars, wifi, battery
  box(scr, 0, 0, W, BAR, C_BAR);
  std::time_t t = std::time(nullptr);
  char hm[8];
  std::strftime(hm, sizeof hm, "%H:%M", std::localtime(&t));
  text(scr, hm, &lv_font_montserrat_14, C_TXT, 6, 2);
  for (int i = 0; i < 4; i++)
    box(scr, 100 + i * 6, BAR - 5 - (i + 1) * 3, 4, (i + 1) * 3, i < st.signal ? C_TXT : C_DIM);
  if (st.wifi) text(scr, LV_SYMBOL_WIFI, &lv_font_montserrat_14, C_TXT, 132, 2);
  if (st.badge) text(scr, LV_SYMBOL_ENVELOPE, &lv_font_montserrat_14, C_ACC, 156, 2);
  text(scr, std::to_string(st.battery) + "%", &lv_font_montserrat_14, C_TXT, 196, 2);

  if (v.home) {
    text(scr, v.lines[0], &lv_font_montserrat_48, C_TXT, 40, 70);
    text(scr, v.lines[1], &lv_font_montserrat_20, C_DIM, 60, 135);
    if (v.lines.size() > 2) text(scr, v.lines[2], &lv_font_montserrat_14, C_ACC, 60, 175);
  } else {
    text(scr, v.title, &lv_font_montserrat_20, C_ACC, 8, BAR + 4);
    const int top = BAR + 34, rows = (H - SOFT - top) / ROW;
    int first = v.selected >= rows ? v.selected - rows + 1 : 0;
    for (int i = first; i < (int)v.lines.size() && i - first < rows; i++) {
      int y = top + (i - first) * ROW;
      if (i == v.selected) box(scr, 0, y, W, ROW, C_ACC);
      text(scr, v.lines[i], &lv_font_montserrat_14, C_TXT, 10, y + 6);
    }
  }

  // softkey bar
  box(scr, 0, H - SOFT, W, SOFT, C_BAR);
  text(scr, v.keys.left, &lv_font_montserrat_14, C_TXT, 8, H - SOFT + 4);
  lv_obj_t* r = text(scr, v.keys.right, &lv_font_montserrat_14, C_TXT, 0, H - SOFT + 4);
  lv_obj_update_layout(r);
  lv_obj_set_x(r, W - 8 - lv_obj_get_width(r));
}

static void defaultKeys() {
  g_keys = {{SDLK_UP, Key::Up}, {SDLK_DOWN, Key::Down}, {SDLK_LEFT, Key::Left}, {SDLK_RIGHT, Key::Right},
            {SDLK_RETURN, Key::Ok}, {SDLK_KP_ENTER, Key::Ok}, {SDLK_ESCAPE, Key::Back}, {SDLK_BACKSPACE, Key::Back},
            {SDLK_F1, Key::Soft1}, {SDLK_F2, Key::Soft2}, {SDLK_F3, Key::Call}, {SDLK_F4, Key::End},
            {SDLK_ASTERISK, Key::Star}, {SDLK_KP_MULTIPLY, Key::Star}, {SDLK_HASH, Key::Pound}, {SDLK_KP_DIVIDE, Key::Pound}};
  for (int i = 0; i < 10; i++) {
    g_keys[SDLK_0 + i] = Key(int(Key::Num0) + i);
    g_keys[i ? SDLK_KP_1 + (i - 1) : SDLK_KP_0] = Key(int(Key::Num0) + i);  // keypad: KP_1..KP_9, then KP_0
  }
}

// Optional override file: lines of "<SDL key name> <FaiOS key name>", e.g. "F5 Call".
static void loadKeymap(const char* path) {
  std::ifstream f(path);
  std::string sdl, fai;
  while (f >> sdl >> fai) {
    SDL_Keycode c = SDL_GetKeyFromName(sdl.c_str());
    Key k;
    if (c != SDLK_UNKNOWN && keyFromName(fai, k)) g_keys[c] = k;
    else std::fprintf(stderr, "keymap: ignoring '%s %s'\n", sdl.c_str(), fai.c_str());
  }
}

static int onSdlEvent(void*, SDL_Event* e) {
  if (e->type == SDL_QUIT) g_running = false;
  else if (e->type == SDL_KEYDOWN) {
    auto it = g_keys.find(e->key.keysym.sym);
    if (it != g_keys.end()) g_mgr.handleKey(it->second);
  }
  return 0;
}

int main(int argc, char** argv) {
  defaultKeys();
  if (argc > 2 && std::string(argv[1]) == "--keymap") loadKeymap(argv[2]);

  lv_init();
  lv_sdl_window_create(W, H);
  lv_tick_set_cb(SDL_GetTicks);
  SDL_AddEventWatch(onSdlEvent, nullptr);

  g_mgr.log.sink = [](const LogEntry& e) { std::fprintf(stderr, "[%s] %s: %s\n", levelName(e.level), e.tag.c_str(), e.msg.c_str()); };
  const char* home = std::getenv("HOME");
  FileStorage storage(std::string(home ? home : ".") + "/.faios");
  Config config(storage);
  ServiceManager services(g_mgr.log);
  services.add(std::make_unique<FnService>("config", [&] { config.load(); g_mgr.config = &config; g_mgr.loadSettings(); return true; },
                                           [&] { g_mgr.saveSettings(); g_mgr.config = nullptr; }));
  services.startAll();

  g_mgr.onChange = [] { g_dirty = true; };
  g_mgr.push(makeHome(g_mgr));
  lv_timer_create([](lv_timer_t*) { if (g_mgr.top()->view().home) g_dirty = true; }, 1000, nullptr);  // clock tick

  while (g_running) {
    lv_timer_handler();
    if (g_dirty) { g_dirty = false; render(); }  // ponytail: rebuilds whole UI per change; diff/update if it gets slow
    SDL_Delay(5);
  }
  services.stopAll();
  return 0;
}
