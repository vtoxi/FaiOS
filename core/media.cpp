#include "media.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace fai {

MediaKind mediaKind(const std::string& n) {
  size_t dot = n.rfind('.');
  if (dot == std::string::npos) return MediaKind::Other;
  std::string e = n.substr(dot + 1);
  for (char& c : e) c = char(std::tolower((unsigned char)c));
  for (const char* x : {"bmp", "png", "jpg", "jpeg", "gif", "webp"}) if (e == x) return MediaKind::Image;
  for (const char* x : {"mp4", "3gp", "webm", "mkv", "avi", "mov"}) if (e == x) return MediaKind::Video;
  for (const char* x : {"mp3", "ogg", "wav", "aac", "m4a", "flac", "amr"}) if (e == x) return MediaKind::Audio;
  return MediaKind::Other;
}

static std::vector<MediaFile> inDir(IStorage& s, const std::string& dir, MediaKind want) {
  std::vector<MediaFile> v;
  for (auto& n : s.list(dir)) {
    if (n.empty() || n.back() == '/') continue;  // sub-folders are not browsed
    if (mediaKind(n) == want) v.push_back({dir + n, n, want});
  }
  std::sort(v.begin(), v.end(), [](const MediaFile& a, const MediaFile& b) { return a.name > b.name; });
  return v;
}

std::vector<MediaFile> listGallery(IStorage& s) {
  auto v = inDir(s, kPhotoDir, MediaKind::Image);
  auto vid = inDir(s, kVideoDir, MediaKind::Video);
  v.insert(v.end(), vid.begin(), vid.end());
  return v;
}

std::string nextPhotoName(IStorage& s) {
  int hi = 0;
  for (auto& n : s.list(kPhotoDir)) {
    int num = 0;
    if (std::sscanf(n.c_str(), "IMG_%d", &num) == 1) hi = std::max(hi, num);
  }
  char b[24];
  std::snprintf(b, sizeof b, "IMG_%04d.bmp", hi + 1);
  return b;
}

static void le32(std::string& o, uint32_t v) { for (int i = 0; i < 4; i++) o += char((v >> (i * 8)) & 0xFF); }
static void le16(std::string& o, uint16_t v) { o += char(v & 0xFF); o += char(v >> 8); }
static uint32_t rd32(const std::string& s, size_t p) { return uint32_t(uint8_t(s[p])) | uint32_t(uint8_t(s[p + 1])) << 8 | uint32_t(uint8_t(s[p + 2])) << 16 | uint32_t(uint8_t(s[p + 3])) << 24; }
static uint16_t rd16(const std::string& s, size_t p) { return uint16_t(uint8_t(s[p]) | uint8_t(s[p + 1]) << 8); }

std::string makeBmp(int w, int h, uint32_t seed) {
  if (w < 1 || h < 1 || w > 4096 || h > 4096) return "";
  const uint32_t stride = (uint32_t(w) * 3 + 3) & ~3u, size = stride * uint32_t(h);
  std::string o = "BM";
  le32(o, 54 + size); le16(o, 0); le16(o, 0); le32(o, 54);
  le32(o, 40); le32(o, uint32_t(w)); le32(o, uint32_t(h)); le16(o, 1); le16(o, 24); le32(o, 0); le32(o, size); le32(o, 2835); le32(o, 2835); le32(o, 0); le32(o, 0);
  const double cx = w * (0.3 + 0.4 * ((seed * 7) % 10) / 10.0), cy = h * (0.3 + 0.4 * ((seed * 3) % 10) / 10.0), rad = std::min(w, h) * 0.22;
  const int hue = int((seed * 53) % 256);
  for (int y = h - 1; y >= 0; y--) {  // bottom-up rows
    for (int x = 0; x < w; x++) {
      int r = (x * 200 / w + hue) & 0xFF, g = (y * 200 / h + hue / 2) & 0xFF, b = 255 - ((x + y) * 160 / (w + h));
      if (std::hypot(x - cx, y - cy) < rad) { r = 250; g = 220; b = 80; }
      o += char(b); o += char(g); o += char(r);
    }
    for (uint32_t p = uint32_t(w) * 3; p < stride; p++) o += char(0);
  }
  return o;
}

bool parseBmp(const std::string& d, int& w, int& h, std::vector<uint8_t>& rgb) {
  if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') return false;
  uint32_t off = rd32(d, 10), hdr = rd32(d, 14);
  int32_t iw = int32_t(rd32(d, 18)), ih = int32_t(rd32(d, 22));
  if (hdr < 40 || rd16(d, 26) != 1 || rd16(d, 28) != 24 || rd32(d, 30) != 0) return false;  // only plain 24-bit
  bool topDown = ih < 0;
  uint32_t uh = uint32_t(topDown ? -int64_t(ih) : ih);
  if (iw < 1 || iw > 4096 || uh < 1 || uh > 4096) return false;
  uint64_t stride = (uint64_t(iw) * 3 + 3) & ~3ull;
  if (off < 54 || uint64_t(off) + stride * uh > d.size()) return false;  // truncated or lying header
  w = iw;
  h = int(uh);
  rgb.assign(size_t(w) * size_t(h) * 3, 0);
  for (int y = 0; y < h; y++) {
    const char* row = d.data() + off + stride * uint64_t(topDown ? y : h - 1 - y);
    for (int x = 0; x < w; x++) {
      uint8_t* px = &rgb[(size_t(y) * w + x) * 3];
      px[0] = uint8_t(row[x * 3 + 2]); px[1] = uint8_t(row[x * 3 + 1]); px[2] = uint8_t(row[x * 3]);
    }
  }
  return true;
}

}  // namespace fai
