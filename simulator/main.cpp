// FaiOS desktop simulator: LVGL + SDL2 renderer over the toolkit-independent core.
// Display size is a generic QVGA placeholder, NOT a Jazz Digit 4G spec.
#include <SDL2/SDL.h>
#include <lvgl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>

#include "screens.h"

using namespace fai;

static const int W = 240, H = 320, STATUS = 24, SOFT = 30;
// Theme (dark navy). All colours live here.
static const uint32_t C_BG = 0x0B1220, C_BG2 = 0x1E3A8A, C_SURF = 0x141C2E, C_SURF2 = 0x1E293B, C_LINE = 0x243049,
                      C_ACC = 0x3B82F6, C_TXT = 0xF1F5F9, C_DIM = 0x94A3B8, C_DANGER = 0xEF4444, C_OK = 0x22C55E, C_OFF = 0x475569;
#define F12 (&lv_font_montserrat_12)
#define F14 (&lv_font_montserrat_14)
#define F16 (&lv_font_montserrat_16)
#define F20 (&lv_font_montserrat_20)
#define F28 (&lv_font_montserrat_28)
#define F48 (&lv_font_montserrat_48)

static AppManager g_mgr;
static bool g_dirty = true, g_running = true;
static std::map<SDL_Keycode, Key> g_keys;

static lv_obj_t* rr(lv_obj_t* p, int x, int y, int w, int h, int radius, uint32_t c) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_shadow_width(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

// Label. w > 0 fixes the width: single line with "..." by default, or wrapped.
static lv_obj_t* label(lv_obj_t* p, const std::string& s, const lv_font_t* f, uint32_t c, int x, int y, int w = 0,
                       lv_text_align_t a = LV_TEXT_ALIGN_LEFT, bool wrap = false) {
  lv_obj_t* l = lv_label_create(p);
  lv_label_set_text(l, s.c_str());
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
  if (w) {
    if (wrap) { lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_width(l, w); }
    else { lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_size(l, w, lv_font_get_line_height(f)); }
    lv_obj_set_style_text_align(l, a, 0);
  }
  lv_obj_set_pos(l, x, y);
  return l;
}

struct Ic { const char* name; const char* sym; uint32_t col; };
static const Ic kIcons[] = {
    {"call", LV_SYMBOL_CALL, 0x22C55E},          {"contacts", LV_SYMBOL_LIST, 0xA78BFA},
    {"mail", LV_SYMBOL_ENVELOPE, 0x60A5FA},      {"history", LV_SYMBOL_REFRESH, 0xF59E0B},
    {"settings", LV_SYMBOL_SETTINGS, 0x94A3B8},  {"wifi", LV_SYMBOL_WIFI, 0x60A5FA},
    {"bluetooth", LV_SYMBOL_BLUETOOTH, 0x60A5FA}, {"brightness", LV_SYMBOL_EYE_OPEN, 0xF59E0B},
    {"info", LV_SYMBOL_FILE, 0x94A3B8},          {"call_in", LV_SYMBOL_CALL, 0x22C55E},
    {"call_out", LV_SYMBOL_CALL, 0x60A5FA},      {"call_miss", LV_SYMBOL_CALL, 0xEF4444},
    {"clock", LV_SYMBOL_LOOP, 0xF472B6},         {"alarm", LV_SYMBOL_BELL, 0xFB923C},
    {"files", LV_SYMBOL_DIRECTORY, 0xFBBF24},    {"folder", LV_SYMBOL_DIRECTORY, 0xFBBF24},
    {"file", LV_SYMBOL_FILE, 0x94A3B8},          {"connect", LV_SYMBOL_GPS, 0x22D3EE},
    {"battery", LV_SYMBOL_BATTERY_FULL, 0x4ADE80}, {"alert", LV_SYMBOL_WARNING, 0xF59E0B},
    {"mobile", LV_SYMBOL_DOWNLOAD, 0x60A5FA},    {"airplane", LV_SYMBOL_SHUFFLE, 0x94A3B8},         {"calc", LV_SYMBOL_KEYBOARD, 0x2DD4BF},
    {"camera", LV_SYMBOL_IMAGE, 0xF87171},
    {"hotspot", LV_SYMBOL_WIFI, 0x38BDF8},       {"sim", LV_SYMBOL_SD_CARD, 0xFBBF24}};

static const Ic* findIcon(const std::string& n) {
  for (auto& i : kIcons) if (n == i.name) return &i;
  return nullptr;
}

// Icon or letter avatar in a circle of diameter d centred at (cx, cy).
static void drawLead(lv_obj_t* p, const std::string& lead, int cx, int cy, int d, bool sel) {
  if (lead.empty()) return;
  static const uint32_t pal[] = {0x6366F1, 0x14B8A6, 0xF97316, 0xEC4899, 0x8B5CF6};
  const lv_font_t* f = d > 40 ? F28 : F16;
  if (lead[0] == '@') {
    rr(p, cx - d / 2, cy - d / 2, d, d, d / 2, pal[(unsigned char)lead[1] % 5]);
    label(p, lead.substr(1), f, 0xFFFFFF, cx - d / 2, cy - lv_font_get_line_height(f) / 2, d, LV_TEXT_ALIGN_CENTER);
  } else if (const Ic* ic = findIcon(lead)) {
    rr(p, cx - d / 2, cy - d / 2, d, d, d / 2, sel ? 0x1D4ED8 : C_SURF2);
    label(p, ic->sym, F14, sel ? 0xFFFFFF : ic->col, cx - d / 2, cy - lv_font_get_line_height(F14) / 2, d, LV_TEXT_ALIGN_CENTER);
  }
}

static void drawStatusBar(lv_obj_t* scr, const SystemStatus& st) {
  for (int i = 0; i < 4; i++) rr(scr, 10 + i * 6, 18 - (i + 1) * 3 - 1, 4, (i + 1) * 3, 1, i < st.signal ? C_TXT : C_OFF);
  std::time_t t = std::time(nullptr);
  char hm[8];
  std::strftime(hm, sizeof hm, "%H:%M", std::localtime(&t));
  label(scr, hm, F14, C_TXT, 0, 4, W, LV_TEXT_ALIGN_CENTER);
  const char* bat = st.battery > 85 ? LV_SYMBOL_BATTERY_FULL : st.battery > 60 ? LV_SYMBOL_BATTERY_3
                    : st.battery > 35 ? LV_SYMBOL_BATTERY_2 : st.battery > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
  std::string g;
  if (st.badge) g += std::string(LV_SYMBOL_ENVELOPE) + "  ";
  if (st.airplane) g += "AIR  ";
  if (st.hotspot) g += std::string(LV_SYMBOL_UPLOAD) + "  ";
  if (st.bluetooth) g += std::string(LV_SYMBOL_BLUETOOTH) + "  ";
  if (st.wifi) g += std::string(LV_SYMBOL_WIFI) + "  ";
  g += std::to_string(st.battery) + "% " + (st.charging ? LV_SYMBOL_CHARGE : "") + bat;
  label(scr, g, F14, C_TXT, W - 138, 4, 130, LV_TEXT_ALIGN_RIGHT);
}

static void drawSoftkeys(lv_obj_t* scr, const Softkeys& k) {
  rr(scr, 0, H - SOFT, W, SOFT, 0, C_SURF);
  rr(scr, 0, H - SOFT, W, 1, 0, C_LINE);
  int y = H - SOFT + 7;
  label(scr, k.left, F14, C_TXT, 10, y, 80);
  label(scr, k.center, F14, C_ACC, 80, y, 80, LV_TEXT_ALIGN_CENTER);
  label(scr, k.right, F14, C_TXT, W - 90, y, 80, LV_TEXT_ALIGN_RIGHT);
}

static void drawToggle(lv_obj_t* p, int x, int cy, bool on) {
  rr(p, x, cy - 9, 34, 18, 9, on ? C_OK : C_OFF);
  rr(p, x + (on ? 18 : 2), cy - 7, 14, 14, 7, 0xFFFFFF);
}

static void drawList(lv_obj_t* scr, const View& v, int top, int bottom) {
  bool anySub = false;
  for (auto& r : v.rows) anySub |= !r.sub.empty();
  const int rowH = anySub ? 50 : 42, n = (int)v.rows.size(), visible = (bottom - top) / rowH;
  const int first = v.selected >= visible ? v.selected - visible + 1 : 0;
  for (int i = first; i < n && i - first < visible; i++) {
    const Row& r = v.rows[i];
    const bool sel = i == v.selected;
    const int y = top + (i - first) * rowH, cy = y + rowH / 2;
    rr(scr, 6, y + 2, W - 12, rowH - 4, 10, sel ? C_ACC : C_SURF);
    int tx = 16;
    if (!r.lead.empty()) { drawLead(scr, r.lead, 16 + 16, cy, 32, sel); tx = 16 + 42; }
    int right = W - 16;
    if (r.toggle >= 0) { drawToggle(scr, right - 34, cy, r.toggle == 1); right -= 42; }
    else if (!r.trail.empty()) { label(scr, r.trail, F14, sel ? 0xDBEAFE : C_DIM, right - 52, cy - 8, 52, LV_TEXT_ALIGN_RIGHT); right -= 58; }
    else if (r.unread) { rr(scr, right - 8, cy - 4, 8, 8, 4, sel ? 0xFFFFFF : C_ACC); right -= 16; }
    const int tw = right - tx;
    const uint32_t main = sel ? 0xFFFFFF : (r.tone == 1 ? C_DANGER : C_TXT);
    if (r.sub.empty()) label(scr, r.text, F16, main, tx, cy - 9, tw);
    else {
      label(scr, r.text, F16, main, tx, y + 7, tw);
      label(scr, r.sub, F12, sel ? 0xDBEAFE : C_DIM, tx, y + 28, tw);
    }
  }
  if (n > visible) {  // scrollbar
    const int th = (bottom - top - 8) * visible / n, ty = top + 4 + (bottom - top - 8 - th) * first / (n - visible);
    rr(scr, W - 4, top + 4, 2, bottom - top - 8, 1, C_LINE);
    rr(scr, W - 4, ty, 2, th, 1, C_DIM);
  }
}

static void drawGrid(lv_obj_t* scr, const View& v, int top) {
  // 3 columns, 3 visible rows; scrolls by row when there are more apps.
  const int cols = 3, tile = 60, gy = 6, gx = (W - cols * tile) / (cols + 1), visRows = 3;
  const int selRow = v.selected >= 0 ? v.selected / cols : 0, firstRow = selRow >= visRows ? selRow - visRows + 1 : 0;
  for (int i = 0; i < (int)v.rows.size(); i++) {
    const int row = i / cols - firstRow;
    if (row < 0 || row >= visRows) continue;
    const Row& r = v.rows[i];
    const bool sel = i == v.selected;
    const int x = gx + (i % cols) * (tile + gx), y = top + row * (tile + gy);
    rr(scr, x, y, tile, tile, 14, sel ? C_ACC : C_SURF);
    if (const Ic* ic = findIcon(r.lead)) label(scr, ic->sym, F28, sel ? 0xFFFFFF : ic->col, x, y + 5, tile, LV_TEXT_ALIGN_CENTER);
    label(scr, r.text, F12, sel ? 0xFFFFFF : C_DIM, x + 2, y + 41, tile - 4, LV_TEXT_ALIGN_CENTER);
  }
  if (v.selected >= 0) {  // full name of the selected app under the grid
    const Row& r = v.rows[v.selected];
    label(scr, r.sub.empty() ? r.text : r.sub, F20, C_TXT, 0, top + visRows * (tile + gy) + 2, W, LV_TEXT_ALIGN_CENTER);
  }
  const int totalRows = ((int)v.rows.size() + cols - 1) / cols;
  if (totalRows > visRows) {  // page dots on the right edge
    for (int r = 0; r < totalRows; r++) rr(scr, W - 6, top + 4 + r * 10, 4, 4, 2, r == selRow ? C_ACC : C_LINE);
  }
}

static void drawClock(lv_obj_t* scr, const View& v, int top) {
  label(scr, v.lines[0], F48, C_TXT, 0, top + 4, W, LV_TEXT_ALIGN_CENTER);
  label(scr, v.lines[1], F16, C_DIM, 0, top + 62, W, LV_TEXT_ALIGN_CENTER);
  rr(scr, 8, top + 100, W - 16, 84, 14, C_SURF);
  label(scr, "STOPWATCH", F12, C_DIM, 8, top + 110, W - 16, LV_TEXT_ALIGN_CENTER);
  label(scr, v.lines[2], F28, C_TXT, 8, top + 134, W - 16, LV_TEXT_ALIGN_CENTER);
}

static void drawCalc(lv_obj_t* scr, const View& v, int top) {
  rr(scr, 8, top + 2, W - 16, 78, 14, C_SURF);
  label(scr, v.lines[0], F14, C_DIM, 18, top + 12, W - 36, LV_TEXT_ALIGN_RIGHT);
  label(scr, v.lines[1], F28, C_TXT, 18, top + 38, W - 36, LV_TEXT_ALIGN_RIGHT);
  static const struct { const char* sym; const char* op; } chips[] = {
      {LV_SYMBOL_UP, "+"}, {LV_SYMBOL_DOWN, "-"}, {LV_SYMBOL_LEFT, "x"}, {LV_SYMBOL_RIGHT, "/"}};
  for (int i = 0; i < 4; i++) {
    const int x = 8 + i * 58, y = top + 92;
    rr(scr, x, y, 54, 40, 10, C_SURF2);
    label(scr, std::string(chips[i].sym) + " " + chips[i].op, F16, C_ACC, x, y + 11, 54, LV_TEXT_ALIGN_CENTER);
  }
  label(scr, "OK or # =    * .", F14, C_DIM, 8, top + 144, W - 16, LV_TEXT_ALIGN_CENTER);
}

static void drawBattery(lv_obj_t* scr, const View& v, int top) {
  const int pct = std::atoi(v.lines[0].c_str());
  label(scr, v.lines[0] + "%", F48, C_TXT, 0, top + 4, W, LV_TEXT_ALIGN_CENTER);
  rr(scr, 20, top + 66, W - 40, 14, 7, C_SURF2);  // level bar
  rr(scr, 20, top + 66, std::max(8, (W - 40) * pct / 100), 14, 7, pct > 20 ? C_OK : C_DANGER);
  static const char* const names[] = {"Status", "Voltage", "Temperature"};
  for (int i = 0; i < 3; i++) {
    const int y = top + 94 + i * 36;
    rr(scr, 8, y, W - 16, 32, 10, C_SURF);
    label(scr, names[i], F14, C_DIM, 18, y + 8, 100);
    label(scr, v.lines[1 + i], F14, C_TXT, 110, y + 8, W - 128, LV_TEXT_ALIGN_RIGHT);
  }
  label(scr, v.lines[4], F12, C_OFF, 8, top + 206, W - 16, LV_TEXT_ALIGN_CENTER);
}

static void drawCamera(lv_obj_t* scr, const View& v, int top) {
  const int x = 10, y = top + 4, w = W - 20, h = 168;
  rr(scr, x, y, w, h, 10, 0x05080F);
  const int c = 16, t = 2, col = 0xFFFFFF;  // corner brackets
  for (int i = 0; i < 4; i++) {
    const int cx = x + 8 + (i % 2) * (w - 16 - c), cy = y + 8 + (i / 2) * (h - 16 - c);
    rr(scr, cx, cy, c, t, 0, col);
    rr(scr, cx, cy + (i / 2 ? c - t : 0), c, t, 0, col);
    rr(scr, cx + (i % 2 ? c - t : 0), cy, t, c, 0, col);
  }
  label(scr, "SIMULATED PREVIEW", F12, C_DIM, x, y + 70, w, LV_TEXT_ALIGN_CENTER);
  label(scr, "no camera hardware", F12, C_OFF, x, y + 88, w, LV_TEXT_ALIGN_CENTER);
  label(scr, v.lines[0].empty() ? "Press OK to capture" : v.lines[0], F14, v.lines[0].empty() ? C_DIM : C_OK, 8, y + h + 8, W - 16, LV_TEXT_ALIGN_CENTER);
  label(scr, "Photos: " + v.lines[1], F12, C_DIM, 8, y + h + 28, W - 16, LV_TEXT_ALIGN_CENTER);
}

static void drawDial(lv_obj_t* scr, const View& v, int top) {
  const std::string& num = v.lines[0];
  if (num.empty()) label(scr, "Enter number", F16, C_DIM, 0, top + 14, W, LV_TEXT_ALIGN_CENTER);
  else label(scr, num, F28, C_TXT, 8, top + 8, W - 16, LV_TEXT_ALIGN_CENTER);
  if (!v.lines[1].empty()) label(scr, v.lines[1], F12, 0xF59E0B, 8, top + 44, W - 16, LV_TEXT_ALIGN_CENTER);
  static const char* const keys[] = {"1", "2 ABC", "3 DEF", "4 GHI", "5 JKL", "6 MNO", "7 PQRS", "8 TUV", "9 WXYZ", "*", "0 +", "#"};
  for (int i = 0; i < 12; i++) {
    const int x = 8 + (i % 3) * 76, y = top + 64 + (i / 3) * 40;
    rr(scr, x, y, 68, 34, 10, C_SURF);
    label(scr, keys[i], F14, C_TXT, x, y + 8, 68, LV_TEXT_ALIGN_CENTER);
  }
}

static void drawText(lv_obj_t* scr, const View& v, int top) {
  rr(scr, 8, top + 4, W - 16, 170, 14, C_SURF);
  int y = top + 16;
  if (!v.lead.empty()) { drawLead(scr, v.lead, W / 2, y + 28, 56, false); y += 66; }
  for (auto& l : v.lines) {
    lv_obj_t* o = label(scr, l, F16, C_TXT, 20, y, W - 40, v.lead.empty() ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, true);
    lv_obj_update_layout(o);
    y += lv_obj_get_height(o) + 6;
  }
}

static void drawHome(lv_obj_t* scr, const View& v) {
  label(scr, v.lines[0], F48, C_TXT, 0, 62, W, LV_TEXT_ALIGN_CENTER);
  label(scr, v.lines[1], F16, C_DIM, 0, 124, W, LV_TEXT_ALIGN_CENTER);
  if (v.lines.size() > 2) {
    rr(scr, 50, 168, 140, 32, 16, C_SURF2);
    label(scr, std::string(LV_SYMBOL_ENVELOPE) + "  " + v.lines[2], F14, 0x93C5FD, 50, 175, 140, LV_TEXT_ALIGN_CENTER);
  }
}

static void render() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_clean(scr);
  const View v = g_mgr.top()->view();
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
  if (v.layout == Layout::Home) {  // wallpaper gradient
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(C_BG2), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
  } else {
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, 0);
  }

  drawStatusBar(scr, g_mgr.status());
  if (v.layout == Layout::Home) drawHome(scr, v);
  else {
    label(scr, v.title, F20, C_TXT, 14, STATUS + 2, W - 28);
    rr(scr, 12, STATUS + 31, W - 24, 1, 0, C_LINE);
    const int top = STATUS + 36;
    switch (v.layout) {
      case Layout::Grid: drawGrid(scr, v, top + 4); break;
      case Layout::Dial: drawDial(scr, v, top); break;
      case Layout::Text: drawText(scr, v, top); break;
      case Layout::Clock: drawClock(scr, v, top); break;
      case Layout::Calc: drawCalc(scr, v, top); break;
      case Layout::Camera: drawCamera(scr, v, top); break;
      case Layout::Battery: drawBattery(scr, v, top); break;
      default: drawList(scr, v, top, H - SOFT); break;
    }
  }
  drawSoftkeys(scr, v.keys);
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
  g_mgr.files = &storage;  // File manager browses ~/.faios
  Config config(storage);
  ServiceManager services(g_mgr.log);
  services.add(std::make_unique<FnService>("config", [&] { config.load(); g_mgr.config = &config; g_mgr.loadSettings(); return true; },
                                           [&] { g_mgr.saveSettings(); g_mgr.config = nullptr; }));
  services.startAll();

  g_mgr.onChange = [] { g_dirty = true; };
  g_mgr.push(makeHome(g_mgr));
  lv_timer_create([](lv_timer_t*) {  // clock tick: 10 Hz on Clock (stopwatch), 1 Hz on Home
    static int n = 0;
    Layout l = g_mgr.top()->view().layout;
    bool second = ++n % 10 == 0;
    if (second) g_mgr.tick(std::time(nullptr));  // fires due alarms
    if (l == Layout::Clock || (l == Layout::Home && second)) g_dirty = true;
  }, 100, nullptr);

  while (g_running) {
    lv_timer_handler();
    if (g_dirty) { g_dirty = false; render(); }  // ponytail: rebuilds whole UI per change; diff/update if it gets slow
    SDL_Delay(5);
  }
  services.stopAll();
  return 0;
}
