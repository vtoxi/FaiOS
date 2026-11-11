// Included by test_core.cpp: media classification, BMP codec, gallery listing, Gallery / Camera screens.
#include "media.h"

static void test_media_kind() {
  CHECK(mediaKind("a.bmp") == MediaKind::Image && mediaKind("A.JPG") == MediaKind::Image && mediaKind("x.y.png") == MediaKind::Image && mediaKind("p.webp") == MediaKind::Image);
  CHECK(mediaKind("clip.mp4") == MediaKind::Video && mediaKind("C.3GP") == MediaKind::Video && mediaKind("m.webm") == MediaKind::Video);
  CHECK(mediaKind("song.mp3") == MediaKind::Audio && mediaKind("v.OGG") == MediaKind::Audio);
  CHECK(mediaKind("notes.txt") == MediaKind::Other && mediaKind("noext") == MediaKind::Other && mediaKind("") == MediaKind::Other && mediaKind("trailing.") == MediaKind::Other);
  CHECK(mediaKind(".bmp") == MediaKind::Image);  // a dot-file named like an extension is still judged by the extension
}

static void test_bmp_codec() {
  std::string b = makeBmp(160, 120, 5);
  int w = 0, h = 0;
  std::vector<uint8_t> rgb;
  CHECK(b.size() == 54 + 160 * 3 * 120 && b.compare(0, 2, "BM") == 0);
  CHECK(parseBmp(b, w, h, rgb) && w == 160 && h == 120 && rgb.size() == size_t(160) * 120 * 3);
  CHECK(makeBmp(160, 120, 5) == b && makeBmp(160, 120, 6) != b);  // deterministic per seed
  // the disc is yellow-ish and the corner is not
  bool disc = false;
  for (size_t i = 0; i < rgb.size(); i += 3) disc |= rgb[i] == 250 && rgb[i + 1] == 220 && rgb[i + 2] == 80;
  CHECK(disc && !(rgb[0] == 250 && rgb[1] == 220 && rgb[2] == 80));

  // rows are padded to 4 bytes: widths that are not a multiple of 4 must round-trip exactly
  for (int ww : {1, 2, 3, 5, 7}) {
    std::string t = makeBmp(ww, 3, 9);
    CHECK(t.size() == 54 + ((size_t(ww) * 3 + 3) & ~size_t(3)) * 3 && parseBmp(t, w, h, rgb) && w == ww && h == 3 && rgb.size() == size_t(ww) * 3 * 3);
  }
  // top-down BMPs (negative height) read the same picture
  std::string up = makeBmp(6, 4, 3), down = up;
  int32_t hn = -4;
  std::memcpy(&down[22], &hn, 4);
  std::string flipped = down.substr(0, 54);
  size_t stride = (6 * 3 + 3) & ~size_t(3);
  for (int r = 3; r >= 0; r--) flipped += up.substr(54 + stride * size_t(r), stride);  // rows reversed = top-down storage
  std::vector<uint8_t> a, c;
  CHECK(parseBmp(up, w, h, a) && parseBmp(flipped, w, h, c) && a == c);

  // everything it does not understand is rejected, never guessed
  CHECK(!parseBmp("", w, h, rgb) && !parseBmp("BM", w, h, rgb) && !parseBmp(std::string(100, 'x'), w, h, rgb));
  CHECK(!parseBmp(b.substr(0, b.size() - 10), w, h, rgb));          // truncated pixel data
  CHECK(!parseBmp(b.substr(0, 53), w, h, rgb));                     // truncated header
  std::string bad = b;
  bad[28] = 8;                                                       // 8 bits per pixel
  CHECK(!parseBmp(bad, w, h, rgb));
  bad = b; bad[30] = 1;                                              // RLE compression
  CHECK(!parseBmp(bad, w, h, rgb));
  bad = b; bad[0] = 'X';
  CHECK(!parseBmp(bad, w, h, rgb));
  bad = b; uint32_t huge = 100000; std::memcpy(&bad[18], &huge, 4);  // 100000 px wide claims far more data than exists
  CHECK(!parseBmp(bad, w, h, rgb));
  bad = b; uint32_t off = 1000000; std::memcpy(&bad[10], &off, 4);   // pixel offset beyond the file
  CHECK(!parseBmp(bad, w, h, rgb));
  bad = b; uint32_t zero = 0; std::memcpy(&bad[18], &zero, 4);
  CHECK(!parseBmp(bad, w, h, rgb));
  CHECK(makeBmp(0, 5, 1).empty() && makeBmp(5, -1, 1).empty() && makeBmp(5000, 5, 1).empty());
}

static void test_gallery_listing() {
  MemStorage s;
  CHECK(listGallery(s).empty() && nextPhotoName(s) == "IMG_0001.bmp");
  s.write("DCIM/IMG_0002.bmp", "x");
  s.write("DCIM/IMG_0010.png", "x");
  s.write("DCIM/IMG_0009.JPG", "x");
  s.write("DCIM/notes.txt", "x");
  s.write("DCIM/sub/IMG_0099.bmp", "x");     // sub-folders are not browsed
  s.write("DCIM/vacation.bmp", "x");         // not IMG_nnnn: listed, but ignored for numbering
  s.write("Videos/clip_b.mp4", "x");
  s.write("Videos/clip_a.3gp", "x");
  s.write("Videos/readme.txt", "x");
  s.write("Music/song.mp3", "x");            // other folders are not part of the gallery
  auto g = listGallery(s);
  std::vector<std::string> names;
  for (auto& f : g) names.push_back(f.key);
  CHECK((names == std::vector<std::string>{"DCIM/vacation.bmp", "DCIM/IMG_0010.png", "DCIM/IMG_0009.JPG", "DCIM/IMG_0002.bmp", "Videos/clip_b.mp4", "Videos/clip_a.3gp"}));
  CHECK(g[0].kind == MediaKind::Image && g[4].kind == MediaKind::Video && g[4].name == "clip_b.mp4");
  CHECK(nextPhotoName(s) == "IMG_0011.bmp");  // the highest number + 1, even with gaps
}

static void test_camera_and_gallery_screens() {
  MemStorage store;
  auto* m = boot();
  m->files = &store;
  long long now = 100;
  m->nowMs = [&] { return now; };
  launch(*m, "Camera");
  CHECK(title(*m) == "Camera" && m->top()->view().layout == Layout::Camera && m->top()->view().keys.left == "Gallery" && m->top()->view().lines[1] == "0");
  press(*m, Key::Ok);
  CHECK(store.exists("DCIM/IMG_0001.bmp") && m->top()->view().lines[0] == "Saved IMG_0001.bmp (simulated)" && m->top()->view().lines[1] == "1");  // never pretends to be real
  now += 1;
  press(*m, Key::Ok);
  std::string data;
  int w = 0, h = 0;
  std::vector<uint8_t> rgb;
  store.read("DCIM/IMG_0002.bmp", data);
  CHECK(parseBmp(data, w, h, rgb) && w == 160 && h == 120 && m->top()->view().lines[1] == "2");
  std::string first;
  store.read("DCIM/IMG_0001.bmp", first);
  CHECK(first != data);                                    // different placeholder per shot

  press(*m, Key::Soft1);                                   // Camera -> Gallery
  auto v = m->top()->view();
  CHECK(title(*m) == "Gallery" && v.layout == Layout::Grid && v.rows.size() == 2 && v.rows[0].text == "IMG_0002" && v.rows[0].image == "DCIM/IMG_0002.bmp" && v.rows[0].lead == "image");
  press(*m, Key::Back);
  CHECK(title(*m) == "Camera");
  delete m;

  auto* n = boot();                                        // no storage: honest, not a crash
  launch(*n, "Camera");
  press(*n, Key::Ok);
  CHECK(n->top()->view().lines[0] == "Storage unavailable" && n->top()->view().lines[1] == "0");
  press(*n, Key::Soft1);
  CHECK(title(*n) == "Gallery" && n->top()->view().layout == Layout::Text && n->top()->view().lines[0] == "Storage unavailable");
  delete n;
}

static void test_gallery_browse_view_delete() {
  MemStorage store;
  auto* m = boot();
  m->files = &store;
  launch(*m, "Gallery");
  auto v = m->top()->view();
  CHECK(title(*m) == "Gallery" && v.layout == Layout::Text && v.lines[0] == "No photos or videos yet.");  // empty state explains what to do
  press(*m, Key::Back);
  for (int i = 1; i <= 5; i++) store.write("DCIM/IMG_000" + std::to_string(i) + ".bmp", makeBmp(40, 30, uint32_t(i)));
  store.write("DCIM/IMG_0006.png", "pngdata");
  store.write("Videos/holiday.mp4", std::string(3000, 'v'));
  launch(*m, "Gallery");
  v = m->top()->view();
  CHECK(v.layout == Layout::Grid && v.rows.size() == 7 && v.rows[0].text == "IMG_0006" && v.rows[0].image == "DCIM/IMG_0006.png" && v.rows[6].text == "holiday");
  CHECK(v.rows[6].lead == "video" && v.rows[6].image.empty() && v.rows[6].sub == "holiday.mp4");        // videos have no thumbnail
  CHECK(v.selected == 0 && v.keys.center == "Open");
  press(*m, Key::Down);
  CHECK(m->top()->view().selected == 3);                                    // 3 columns
  press(*m, Key::Down); press(*m, Key::Down);
  CHECK(m->top()->view().selected == 0);                                    // 3 -> 6 -> wraps to the top of that column
  press(*m, Key::Up);
  CHECK(m->top()->view().selected == 6);
  press(*m, Key::Left);
  CHECK(m->top()->view().selected == 5);
  press(*m, Key::Right); press(*m, Key::Right);
  CHECK(m->top()->view().selected == 0);

  press(*m, Key::Ok);                                                       // newest photo (a PNG the renderer decides how to show)
  v = m->top()->view();
  CHECK(title(*m) == "IMG_0006" && v.layout == Layout::Photo && v.image == "DCIM/IMG_0006.png" && v.lines[0] == "IMG_0006.png" && v.lines[1].rfind("1 / 7", 0) == 0 && v.keys.left == "Delete");
  press(*m, Key::Left);                                                     // previous wraps to the last item: the video
  v = m->top()->view();
  CHECK(title(*m) == "holiday" && v.layout == Layout::Text && v.lead == "video" && v.lines[2] == "Video playback is not available yet." && v.lines[1] == "2.9 KB");
  press(*m, Key::Right);
  CHECK(title(*m) == "IMG_0006");
  press(*m, Key::Right);
  v = m->top()->view();
  CHECK(title(*m) == "IMG_0005" && v.image == "DCIM/IMG_0005.bmp" && v.lines[1].rfind("2 / 7", 0) == 0);

  press(*m, Key::Soft1);                                                    // delete: cancel first
  CHECK(title(*m) == "Delete file?");
  press(*m, Key::Down);
  press(*m, Key::Ok);
  CHECK(title(*m) == "IMG_0005" && store.exists("DCIM/IMG_0005.bmp"));
  press(*m, Key::Soft1);
  press(*m, Key::Ok);                                                       // confirm
  CHECK(title(*m) == "Gallery" && !store.exists("DCIM/IMG_0005.bmp") && m->top()->view().rows.size() == 6);   // the grid is live
  CHECK(store.exists("DCIM/IMG_0004.bmp"));                                 // only the chosen file went

  press(*m, Key::Ok);
  press(*m, Key::Right);
  store.remove("DCIM/IMG_0004.bmp");                                        // removed behind the viewer's back (e.g. another app)
  v = m->top()->view();
  CHECK(v.lines.size() == 1 && v.lines[0] == "File not found" && v.keys.left.empty());
  press(*m, Key::Back);
  CHECK(title(*m) == "Gallery");
  delete m;
}

static void test_gallery_menu_tile() {
  auto* m = boot();
  press(*m, Key::Ok);
  auto v = m->top()->view();
  CHECK(v.rows.size() == 9 && v.rows[3].text == "Gallery" && v.rows[3].sub == "Photos & videos" && v.rows[2].text == "Camera");
  delete m;
}
