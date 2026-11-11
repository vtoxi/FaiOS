// FaiOS desktop simulator: LVGL + SDL2 renderer over the toolkit-independent core.
// Display size is a generic QVGA placeholder, NOT a Jazz Digit 4G spec.
#include <SDL2/SDL.h>
#include <lvgl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

#include "media.h"
#include "mock_google.h"
#include "screens.h"
#include "sim_links.h"
#include "sim_vpn.h"

// PNG decoding uses zlib (see decodePng).
#include <zlib.h>

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
    {"vpn", LV_SYMBOL_EYE_CLOSE, 0x38BDF8},      {"accounts", LV_SYMBOL_HOME, 0xA78BFA},      {"calendar", LV_SYMBOL_EDIT, 0x34D399},      {"games", LV_SYMBOL_PLAY, 0xFB7185},
    {"clock", LV_SYMBOL_LOOP, 0xF472B6},         {"alarm", LV_SYMBOL_BELL, 0xFB923C},
    {"files", LV_SYMBOL_DIRECTORY, 0xFBBF24},    {"folder", LV_SYMBOL_DIRECTORY, 0xFBBF24},
    {"file", LV_SYMBOL_FILE, 0x94A3B8},          {"connect", LV_SYMBOL_GPS, 0x22D3EE},
    {"battery", LV_SYMBOL_BATTERY_FULL, 0x4ADE80}, {"alert", LV_SYMBOL_WARNING, 0xF59E0B},
    {"mobile", LV_SYMBOL_DOWNLOAD, 0x60A5FA},    {"airplane", LV_SYMBOL_SHUFFLE, 0x94A3B8},         {"calc", LV_SYMBOL_KEYBOARD, 0x2DD4BF},
    {"camera", LV_SYMBOL_VIDEO, 0xF87171},       {"image", LV_SYMBOL_IMAGE, 0x38BDF8},       {"video", LV_SYMBOL_VIDEO, 0xFB923C},
    {"music", LV_SYMBOL_AUDIO, 0xA78BFA},       {"radio", LV_SYMBOL_VOLUME_MAX, 0x34D399},  {"mic", LV_SYMBOL_PLAY, 0xF87171},
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
  label(scr, hm, F14, C_TXT, 40, 4, 44);  // clock sits next to the signal bars, never under the icon group
  const char* bat = st.battery > 85 ? LV_SYMBOL_BATTERY_FULL : st.battery > 60 ? LV_SYMBOL_BATTERY_3
                    : st.battery > 35 ? LV_SYMBOL_BATTERY_2 : st.battery > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
  std::string g;
  if (st.badge) g += std::string(LV_SYMBOL_ENVELOPE) + " ";
  if (st.airplane) g += "AIR  ";
  if (st.vpnBlocked && !st.vpnProtected) g += "BLK ";                    // kill switch is blocking and no verified tunnel
  else if (st.vpnProtected) g += st.vpnSimulated ? "sVPN " : "VPN ";      // "sVPN" = simulated, never shown as protection
  if (st.hotspot) g += std::string(LV_SYMBOL_UPLOAD) + " ";
  if (st.bluetooth) g += std::string(LV_SYMBOL_BLUETOOTH) + " ";
  if (st.wifi) g += std::string(LV_SYMBOL_WIFI) + " ";
  g += std::to_string(st.battery) + "% " + (st.charging ? LV_SYMBOL_CHARGE : "") + bat;
  label(scr, g, F14, C_TXT, 88, 4, W - 94, LV_TEXT_ALIGN_RIGHT);  // icon group owns x=88..right edge; "..." if it ever overflows
}

static void drawSoftkeys(lv_obj_t* scr, const Softkeys& k) {
  rr(scr, 0, H - SOFT, W, SOFT, 0, C_SURF);
  rr(scr, 0, H - SOFT, W, 1, 0, C_LINE);
  int y = H - SOFT + 7;
  label(scr, k.left, F14, C_TXT, 10, y, 80);
  label(scr, k.center, F14, C_ACC, 66, y, 108, LV_TEXT_ALIGN_CENTER);  // wide enough for "Disconnect"
  label(scr, k.right, F14, C_TXT, W - 90, y, 80, LV_TEXT_ALIGN_RIGHT);
}

static void drawToggle(lv_obj_t* p, int x, int cy, bool on) {
  rr(p, x, cy - 9, 34, 18, 9, on ? C_OK : C_OFF);
  rr(p, x + (on ? 18 : 2), cy - 7, 14, 14, 7, 0xFFFFFF);
}

// ---- images (Gallery) ----
// BMP is decoded here; PNG through LVGL's decoder. Decoded and scaled pixels are cached by key so a redraw is cheap.
struct ImgBuf { int w = 0, h = 0; std::vector<uint32_t> px; };  // XRGB8888, top-down
static std::map<std::string, std::unique_ptr<ImgBuf>> g_decoded;
struct Scaled { std::vector<uint32_t> px; lv_image_dsc_t dsc; };
static std::map<std::string, std::unique_ptr<Scaled>> g_scaled;

static uint32_t be32(const unsigned char* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

// Minimal PNG reader (zlib): 8-bit gray / gray+alpha / RGB / RGBA, non-interlaced. Anything else is refused, not guessed.
static bool decodePng(const std::string& b, ImgBuf& out) {
  const unsigned char* d = reinterpret_cast<const unsigned char*>(b.data());
  if (b.size() < 33 || std::memcmp(d, "\x89PNG\r\n\x1a\n", 8) != 0) return false;
  uint32_t w = 0, h = 0;
  int depth = 0, ctype = -1, interlace = 0;
  std::string idat;
  for (size_t pos = 8; pos + 12 <= b.size();) {
    uint32_t len = be32(d + pos);
    if (len > b.size() - pos - 12) return false;
    const unsigned char* t = d + pos + 4;
    const unsigned char* body = d + pos + 8;
    if (!std::memcmp(t, "IHDR", 4) && len == 13) { w = be32(body); h = be32(body + 4); depth = body[8]; ctype = body[9]; interlace = body[12]; }
    else if (!std::memcmp(t, "IDAT", 4)) idat.append(reinterpret_cast<const char*>(body), len);
    else if (!std::memcmp(t, "IEND", 4)) break;
    pos += 12 + len;
  }
  int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
  if (!ch || depth != 8 || interlace || w < 1 || h < 1 || w > 2048 || h > 2048) return false;
  const size_t stride = size_t(w) * ch, need = (stride + 1) * h;
  std::vector<unsigned char> raw(need);
  uLongf got = uLongf(need);
  if (uncompress(raw.data(), &got, reinterpret_cast<const Bytef*>(idat.data()), uLong(idat.size())) != Z_OK || got != need) return false;
  std::vector<unsigned char> img(stride * h);
  for (size_t y = 0; y < h; y++) {
    unsigned char* cur = &img[y * stride];
    const unsigned char* up = y ? cur - stride : nullptr;
    const unsigned char* src = &raw[y * (stride + 1)];
    int f = src[0];
    if (f > 4) return false;
    for (size_t i = 0; i < stride; i++) {
      int a = i >= size_t(ch) ? cur[i - ch] : 0, u = up ? up[i] : 0, c = (up && i >= size_t(ch)) ? up[i - ch] : 0, x = src[1 + i], v;
      if (f == 0) v = x;
      else if (f == 1) v = x + a;
      else if (f == 2) v = x + u;
      else if (f == 3) v = x + (a + u) / 2;
      else { int pp = a + u - c, pa = std::abs(pp - a), pb = std::abs(pp - u), pc = std::abs(pp - c); v = x + (pa <= pb && pa <= pc ? a : pb <= pc ? u : c); }
      cur[i] = static_cast<unsigned char>(v);
    }
  }
  out.w = int(w);
  out.h = int(h);
  out.px.resize(size_t(w) * h);
  for (size_t i = 0; i < out.px.size(); i++) {  // alpha is ignored: photos are shown opaque
    const unsigned char* p = &img[i * ch];
    unsigned r = p[0], g = ch >= 3 ? p[1] : p[0], bl = ch >= 3 ? p[2] : p[0];
    out.px[i] = 0xFF000000u | r << 16 | g << 8 | bl;
  }
  return true;
}

static const ImgBuf* loadImage(const std::string& key) {
  auto it = g_decoded.find(key);
  if (it != g_decoded.end()) return it->second->w ? it->second.get() : nullptr;  // cached failures are remembered too
  if (g_decoded.size() > 40) { g_decoded.clear(); g_scaled.clear(); }
  auto buf = std::make_unique<ImgBuf>();
  std::string bytes;
  if (g_mgr.files && g_mgr.files->read(key, bytes)) {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
    if (parseBmp(bytes, w, h, rgb)) {
      buf->w = w; buf->h = h;
      buf->px.resize(size_t(w) * h);
      for (size_t i = 0; i < buf->px.size(); i++) buf->px[i] = 0xFF000000u | uint32_t(rgb[i * 3]) << 16 | uint32_t(rgb[i * 3 + 1]) << 8 | rgb[i * 3 + 2];
    } else if (!decodePng(bytes, *buf)) { buf->w = buf->h = 0; buf->px.clear(); }
  }
  const ImgBuf* r = buf->w ? buf.get() : nullptr;
  g_decoded[key] = std::move(buf);
  return r;
}

// Shows image `key` scaled to fit inside (x, y, w, h), centred. Returns false if it cannot be decoded.
static bool drawImageFit(lv_obj_t* p, const std::string& key, int x, int y, int w, int h) {
  const ImgBuf* src = loadImage(key);
  if (!src) return false;
  const double s = std::min(double(w) / src->w, double(h) / src->h);
  const int tw = std::max(1, int(src->w * s)), th = std::max(1, int(src->h * s));
  const std::string sk = key + "@" + std::to_string(tw) + "x" + std::to_string(th);
  auto it = g_scaled.find(sk);
  if (it == g_scaled.end()) {
    auto sc = std::make_unique<Scaled>();
    sc->px.resize(size_t(tw) * th);
    for (int yy = 0; yy < th; yy++)
      for (int xx = 0; xx < tw; xx++) sc->px[size_t(yy) * tw + xx] = src->px[size_t(yy * src->h / th) * src->w + size_t(xx * src->w / tw)];  // nearest neighbour
    std::memset(&sc->dsc, 0, sizeof sc->dsc);
    sc->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    sc->dsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    sc->dsc.header.w = tw;
    sc->dsc.header.h = th;
    sc->dsc.header.stride = tw * 4;
    sc->dsc.data_size = size_t(tw) * th * 4;
    sc->dsc.data = reinterpret_cast<const uint8_t*>(sc->px.data());
    it = g_scaled.emplace(sk, std::move(sc)).first;
  }
  lv_obj_t* im = lv_image_create(p);
  lv_image_set_src(im, &it->second->dsc);
  lv_obj_set_pos(im, x + (w - tw) / 2, y + (h - th) / 2);
  return true;
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
    else if (!r.trail.empty()) {  // short trails (counts, "On") reserve less room so long titles keep more space
      const int tww = r.trail.size() <= 4 ? 34 : 52;
      label(scr, r.trail, F14, sel ? 0xDBEAFE : C_DIM, right - tww, cy - 8, tww, LV_TEXT_ALIGN_RIGHT);
      right -= tww + 6;
    }
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
    if (!r.image.empty() && drawImageFit(scr, r.image, x + 3, y + 3, tile - 6, 36)) {}  // photo thumbnail
    else if (const Ic* ic = findIcon(r.lead)) label(scr, ic->sym, F28, sel ? 0xFFFFFF : ic->col, x, y + 5, tile, LV_TEXT_ALIGN_CENTER);
    label(scr, r.text, F12, sel ? 0xFFFFFF : C_DIM, x - 8, y + 41, tile + 16, LV_TEXT_ALIGN_CENTER);  // may spill into the tile gap so "Messages" fits
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

static void drawBoard(lv_obj_t* scr, const View& v, int top) {
  label(scr, v.lines[0], F14, C_DIM, 0, top + 2, W, LV_TEXT_ALIGN_CENTER);
  const int rows = int(v.lines.size()) - 1;
  if (rows < 1) return;
  const int cols = int(v.lines[1].size());
  const int availW = W - 16, availH = (H - SOFT - 6) - (top + 24);
  const int cell = std::min(availW / cols, availH / rows), bw = cell * cols, bh = cell * rows;
  const int x0 = (W - bw) / 2, y0 = top + 24 + (availH - bh) / 2;
  rr(scr, x0 - 2, y0 - 2, bw + 4, bh + 4, 6, C_SURF);
  const bool ttt = rows == 3 && cols == 3;  // Tic-Tac-Toe: big rounded cells with a cursor; Snake: plain cells
  for (int y = 0; y < rows; y++)
    for (int x = 0; x < cols; x++) {
      const char c = v.lines[1 + y][x];
      const bool sel = y * cols + x == v.selected;
      const int px = x0 + x * cell, py = y0 + y * cell;
      if (ttt) {
        rr(scr, px + 2, py + 2, cell - 4, cell - 4, 8, sel ? C_ACC : C_SURF2);
        if (c == 'X' || c == 'O')
          label(scr, std::string(1, c), F48, sel ? 0xFFFFFF : (c == 'X' ? 0x60A5FA : 0xF59E0B), px, py + (cell - lv_font_get_line_height(F48)) / 2, cell, LV_TEXT_ALIGN_CENTER);
      } else if (c == 'o') rr(scr, px + 1, py + 1, cell - 2, cell - 2, 4, 0x22C55E);
      else if (c == '@') rr(scr, px + 1, py + 1, cell - 2, cell - 2, 5, 0x86EFAC);
      else if (c == '*') rr(scr, px + 3, py + 3, cell - 6, cell - 6, (cell - 6) / 2, C_DANGER);
    }
}

static void drawCalendar(lv_obj_t* scr, const View& v, int top) {
  const int offset = std::atoi(v.lines[1].c_str()), days = std::atoi(v.lines[2].c_str());
  const int sel = std::atoi(v.lines[3].c_str()), today = std::atoi(v.lines[4].c_str());
  const std::string& mask = v.lines[6];
  label(scr, v.lines[0], F20, C_TXT, 0, top, W, LV_TEXT_ALIGN_CENTER);
  static const char* const hdr[] = {"M", "T", "W", "T", "F", "S", "S"};
  const int colW = (W - 16) / 7, x0 = 8 + ((W - 16) - colW * 7) / 2, rowH = 27, gy = top + 44;
  for (int i = 0; i < 7; i++) label(scr, hdr[i], F12, i >= 5 ? 0xFCA5A5 : C_DIM, x0 + i * colW, top + 28, colW, LV_TEXT_ALIGN_CENTER);
  for (int d = 1; d <= days; d++) {
    const int idx = offset + d - 1, col = idx % 7, px = x0 + col * colW, py = gy + (idx / 7) * rowH;
    if (d == sel) rr(scr, px + 1, py, colW - 2, rowH - 2, 8, C_ACC);
    else if (d == today) rr(scr, px + 1, py, colW - 2, rowH - 2, 8, C_SURF2);
    label(scr, std::to_string(d), F14, d == sel ? 0xFFFFFF : (col >= 5 ? 0xFCA5A5 : C_TXT), px, py + 4, colW, LV_TEXT_ALIGN_CENTER);
    if (size_t(d - 1) < mask.size() && mask[d - 1] == '1') rr(scr, px + colW / 2 - 2, py + rowH - 8, 4, 4, 2, d == sel ? 0xFFFFFF : C_ACC);
  }
  label(scr, v.lines[5], F14, C_DIM, 0, gy + 6 * rowH + 4, W, LV_TEXT_ALIGN_CENTER);
}

static void drawPhoto(lv_obj_t* scr, const View& v, int top) {
  const int bottom = H - SOFT - 6, capH = 34, ah = bottom - (top + 4) - capH;
  rr(scr, 8, top + 4, W - 16, ah, 10, 0x05080F);
  if (!drawImageFit(scr, v.image, 10, top + 6, W - 20, ah - 4)) {
    label(scr, "Preview not available for this format", F14, C_DIM, 20, top + 4 + ah / 2 - 16, W - 40, LV_TEXT_ALIGN_CENTER, true);
  }
  label(scr, v.lines[0], F14, C_TXT, 8, bottom - capH + 4, W - 16, LV_TEXT_ALIGN_CENTER);
  label(scr, v.lines[1], F12, C_DIM, 8, bottom - capH + 20, W - 16, LV_TEXT_ALIGN_CENTER);
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
  // Contact/message cards: avatar + larger text. Explanations (icon lead or none): compact, left-aligned, card fits the text.
  const bool person = !v.lead.empty() && v.lead[0] == '@';
  const lv_font_t* f = person ? F16 : F14;
  const int bottom = H - SOFT - 6;
  lv_obj_t* card = rr(scr, 8, top + 4, W - 16, 40, 14, C_SURF);  // height set after the text is measured
  int y = top + 14;
  if (!v.lead.empty()) { int d = person ? 56 : 36; drawLead(scr, v.lead, W / 2, y + d / 2, d, false); y += d + 10; }
  for (auto& l : v.lines) {
    lv_obj_t* o = label(scr, l, f, C_TXT, 20, y, W - 40, person ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, true);
    lv_obj_update_layout(o);
    y += lv_obj_get_height(o) + 6;
  }
  lv_obj_set_height(card, std::min(y + 8, bottom) - (top + 4));
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
      case Layout::Board: drawBoard(scr, v, top); break;
      case Layout::Calendar: drawCalendar(scr, v, top); break;
      case Layout::Photo: drawPhoto(scr, v, top); break;
      default: drawList(scr, v, top, H - SOFT); break;
    }
  }
  drawSoftkeys(scr, v.keys);
}

// Screenshot mode: --script "shot:home,Ok,shot:menu" --shots DIR renders to DIR/<name>.ppm and exits.
static uint8_t g_fb[W * H * 4];  // headless framebuffer (BGRA), used only in screenshot mode
static void headlessFlush(lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); }

static void shot(const std::string& path) {
  render();
  lv_obj_update_layout(lv_screen_active());
  lv_refr_now(nullptr);
  std::ofstream f(path, std::ios::binary);
  f << "P6\n" << W << " " << H << "\n255\n";
  for (int i = 0; i < W * H; i++) { char px[3] = {char(g_fb[i * 4 + 2]), char(g_fb[i * 4 + 1]), char(g_fb[i * 4])}; f.write(px, 3); }
}

static void runScript(const std::string& script, const std::string& dir) {
  std::stringstream ss(script);
  for (std::string tok; std::getline(ss, tok, ',');) {
    Key k;
    if (tok.rfind("shot:", 0) == 0) shot(dir + "/" + tok.substr(5) + ".ppm");
    else if (tok.rfind("wait:", 0) == 0) { SDL_Delay(Uint32(std::atoi(tok.c_str() + 5))); g_mgr.tick(std::time(nullptr)); }  // let simulated time pass, then tick services
    else if (keyFromName(tok, k)) g_mgr.handleKey(k);
    else std::fprintf(stderr, "script: unknown key '%s'\n", tok.c_str());
  }
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
  bool mockGoogle = false, vpnDemo = false;
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--mock-google") mockGoogle = true;
    if (std::string(argv[i]) == "--vpn-demo") vpnDemo = true;
  }
  std::string script, shotsDir = ".";
  for (int i = 1; i + 1 < argc; i++) {
    std::string a = argv[i];
    if (a == "--keymap") loadKeymap(argv[i + 1]);
    else if (a == "--script") script = argv[i + 1];
    else if (a == "--shots") shotsDir = argv[i + 1];
  }

  lv_init();
  lv_tick_set_cb(SDL_GetTicks);
  if (!script.empty()) {  // screenshot mode: render into RAM, no window
    lv_display_t* d = lv_display_create(W, H);
    lv_display_set_buffers(d, g_fb, nullptr, sizeof g_fb, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, headlessFlush);
  } else {
    lv_sdl_window_create(W, H);
    SDL_AddEventWatch(onSdlEvent, nullptr);
  }

  g_mgr.log.sink = [](const LogEntry& e) { std::fprintf(stderr, "[%s] %s: %s\n", levelName(e.level), e.tag.c_str(), e.msg.c_str()); };
  const char* home = std::getenv("HOME");
  FileStorage storage(std::string(home ? home : ".") + "/.faios");
  g_mgr.files = &storage;  // File manager browses ~/.faios
  Config config(storage);
  ServiceManager services(g_mgr.log);
  services.add(std::make_unique<FnService>("config", [&] { config.load(); g_mgr.config = &config; g_mgr.loadSettings(); return true; },
                                           [&] { g_mgr.saveSettings(); g_mgr.config = nullptr; }));
  services.startAll();

  // Accounts. Real transport does not exist yet (NullHttp = always offline); --mock-google swaps in a MOCK.
  SystemRandom rng;
  NullHttp nullHttp;
  MockGoogleHttp mockHttp("mock-client");
#ifdef FAIOS_HAVE_OPENSSL
  AesGcmSecretBox box(machineKeyMaterial(), rng);
#else
  NoSecretBox box;  // no OpenSSL: tokens are refused, never stored in plaintext
#endif
  AccountManager accounts(storage, box, mockGoogle ? static_cast<IHttp&>(mockHttp) : static_cast<IHttp&>(nullHttp), rng, g_mgr.log,
                          [] { return static_cast<long long>(std::time(nullptr)) * 1000; });
  OAuthClient oc;
  oc.clientId = mockGoogle ? "mock-client" : config.getString("google_client_id");
  oc.redirectUri = mockGoogle ? "http://127.0.0.1:8765/" : config.getString("google_redirect_uri");
  oc.clientSecret = config.getString("google_client_secret");  // optional; from config, never from source
  accounts.configure(oc);
  accounts.load();
  g_mgr.accounts = &accounts;

  // Network + VPN: link state is SIMULATED from the Device; the VPN backend is SIMULATED (no tunnel, no protection).
  SimLinkProvider links(g_mgr.device);
  NetworkManager network(links, g_mgr.log);
  SimResolver simResolver;
  SimVpnBackend simBackend;
  VpnStore vpnStore(storage, box, rng, g_mgr.log);
  vpnStore.load();
  VpnManager vpn(vpnStore, simBackend, simResolver, network, g_mgr.log, [] { return SimVpnBackend::nowMs(); });
  network.addListener([&](LinkKind, LinkKind) { vpn.onNetworkChange(); });
  network.poll();
  g_mgr.net = &network;
  g_mgr.vpnStore = &vpnStore;
  g_mgr.vpn = &vpn;
  EventStore events(storage);  // local calendar events
  events.load();
  g_mgr.events = &events;
  if (vpnDemo && !storage.exists("import/demo.conf") && vpnStore.profiles().empty()) storage.write("import/demo.conf", demoWgConfig());
  if (mockGoogle)
    g_mgr.mockSignIn = [&] {
      AuthRequest r = accounts.beginSignIn({scope::kOpenId, scope::kEmail, scope::kProfile});
      AddResult res = accounts.completeSignIn(r, oc.redirectUri + "?code=MOCK&state=" + r.state);
      if (res.status != AddStatus::Ok) g_mgr.notifications.add("Accounts", "Mock sign-in failed: " + (res.detail.empty() ? "see log" : res.detail));
    };

  g_mgr.onChange = [] { g_dirty = true; };
  g_mgr.push(makeHome(g_mgr));
  lv_timer_create([](lv_timer_t*) {  // 20 Hz: game frames; 10 Hz redraw on the Clock (stopwatch); 1 Hz services and Home clock
    static int n = 0;
    n++;
    g_mgr.frame();
    Layout l = g_mgr.top()->view().layout;
    bool second = n % 20 == 0;
    if (second) {
      g_mgr.tick(std::time(nullptr));  // fires due alarms, polls links, drives the VPN
      if (g_mgr.vpn) {                 // redraw when the tunnel state changes
        static std::string lastVpn;
        const VpnStatus& vs = g_mgr.vpn->status();
        std::string cur = std::string(vpnStateName(vs.state)) + (vs.protectionVerified ? "+" : "-") + vs.error + std::to_string(vs.rxBytes / 4096);
        if (cur != lastVpn) { lastVpn = cur; g_dirty = true; }
      }
    }
    if ((l == Layout::Clock && n % 2 == 0) || (l == Layout::Home && second)) g_dirty = true;
  }, 50, nullptr);

  if (!script.empty()) { runScript(script, shotsDir); services.stopAll(); return 0; }

  while (g_running) {
    lv_timer_handler();
    if (g_dirty) { g_dirty = false; render(); }  // ponytail: rebuilds whole UI per change; diff/update if it gets slow
    SDL_Delay(5);
  }
  services.stopAll();
  return 0;
}
