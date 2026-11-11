// Included by test_core.cpp: Music / Videos / FM Radio / Recorder (all SIMULATED) and the Media group.

static void test_media_helpers() {
  CHECK(formatClock(0) == "0:00" && formatClock(59999) == "0:59" && formatClock(61000) == "1:01" && formatClock(-5) == "0:00" && formatClock(3600000) == "60:00");
  CHECK(makeWav(0).empty() && makeWav(601).empty() && makeWav(-1).empty());
  std::string w = makeWav(2);
  CHECK(w.size() == 44 + 2 * 8000 * 2 && w.compare(0, 4, "RIFF") == 0 && w.compare(8, 4, "WAVE") == 0 && w.compare(36, 4, "data") == 0);
  uint32_t dataBytes = uint32_t(uint8_t(w[40])) | uint32_t(uint8_t(w[41])) << 8 | uint32_t(uint8_t(w[42])) << 16 | uint32_t(uint8_t(w[43])) << 24;
  CHECK(dataBytes == 32000 && w.find_first_not_of('\0', 44) == std::string::npos);  // it really is silence
  MemStorage s;
  CHECK(nextRecordingName(s) == "REC_0001.wav");
  s.write("Recordings/REC_0007.wav", "x");
  s.write("Recordings/other.wav", "x");
  CHECK(nextRecordingName(s) == "REC_0008.wav");
  s.write("Music/b.mp3", "x"); s.write("Music/a.OGG", "x"); s.write("Music/cover.jpg", "x");
  auto l = listMedia(s, kMusicDir, MediaKind::Audio);
  CHECK(l.size() == 2 && l[0].name == "b.mp3" && l[1].name == "a.OGG");
}

static void test_media_group_menu() {
  auto* m = boot();
  press(*m, Key::Ok);
  auto v = m->top()->view();
  CHECK(v.rows.size() == 9 && v.rows[3].text == "Media" && v.rows[2].text == "Camera");
  press(*m, Key::Num4);
  v = m->top()->view();
  CHECK(title(*m) == "Media" && v.rows.size() == 5 && v.rows[0].text == "Gallery" && v.rows[1].text == "Music" && v.rows[2].text == "Videos" && v.rows[3].text == "FM Radio" && v.rows[4].text == "Recorder");
  CHECK(v.rows[3].sub == "Simulated" && v.rows[4].sub == "Simulated");  // the menu itself says what is not real
  delete m;
}

static void test_music_player() {
  MemStorage store;
  auto* m = boot();
  m->files = &store;
  long long now = 1000;
  m->nowMs = [&] { return now; };
  launch(*m, "Music");
  CHECK(title(*m) == "Music" && m->top()->view().layout == Layout::Text && m->top()->view().lines[0].find("No music") == 0);
  press(*m, Key::Back);
  store.write("Music/alpha.mp3", "a");
  store.write("Music/beta.ogg", "b");
  launch(*m, "Music");
  auto v = m->top()->view();
  CHECK(v.layout == Layout::List && v.rows.size() == 2 && v.rows[0].text == "beta" && v.rows[1].text == "alpha");
  press(*m, Key::Ok);
  v = m->top()->view();
  CHECK(title(*m) == "beta" && v.keys.center == "Play" && v.lines[1] == "Paused 0:00   1/2" && v.lines[2] == "Volume 5/10");
  CHECK(v.lines[3].find("SIMULATED") == 0);                                 // never pretends audio is playing
  press(*m, Key::Ok);                                                       // play
  now += 65000;
  v = m->top()->view();
  CHECK(v.keys.center == "Pause" && v.lines[1] == "Playing 1:05   1/2");
  press(*m, Key::Ok);                                                       // pause freezes the clock
  now += 30000;
  CHECK(m->top()->view().lines[1] == "Paused 1:05   1/2");
  press(*m, Key::Up);
  press(*m, Key::Up);
  CHECK(m->top()->view().lines[2] == "Volume 7/10");
  for (int i = 0; i < 20; i++) press(*m, Key::Up);
  CHECK(m->top()->view().lines[2] == "Volume 10/10");
  for (int i = 0; i < 20; i++) press(*m, Key::Down);
  CHECK(m->top()->view().lines[2] == "Volume 0/10");
  press(*m, Key::Num3);                                                     // next track restarts the timer, wraps around
  v = m->top()->view();
  CHECK(title(*m) == "alpha" && v.lines[1] == "Paused 0:00   2/2");
  press(*m, Key::Num3);
  CHECK(title(*m) == "beta");
  press(*m, Key::Num1);
  CHECK(title(*m) == "alpha");
  store.remove("Music/alpha.mp3");                                          // vanished behind the player's back
  CHECK(m->top()->view().lines[0] == "File not found");
  press(*m, Key::Back);
  CHECK(title(*m) == "Music");
  delete m;

  auto* n = boot();                                                         // no storage: honest
  launch(*n, "Music");
  CHECK(n->top()->view().lines[0] == "Storage unavailable");
  delete n;
}

static void test_videos_app() {
  MemStorage store;
  auto* m = boot();
  m->files = &store;
  store.write("Videos/holiday.mp4", "v");
  store.write("Music/song.mp3", "s");
  launch(*m, "Videos");
  auto v = m->top()->view();
  CHECK(v.rows.size() == 1 && v.rows[0].text == "holiday" && v.rows[0].lead == "video");   // music is not listed here
  press(*m, Key::Ok);
  v = m->top()->view();
  CHECK(v.lead == "video" && v.lines[3] == "SIMULATED: timer only, no video decoder.");
  delete m;
}

static void test_fm_radio() {
  auto* m = boot();
  launch(*m, "FM Radio");
  auto v = m->top()->view();
  CHECK(title(*m) == "FM Radio" && v.lines[0] == "98.7 MHz" && v.lines[1] == "Off   Volume 5/10" && v.keys.center == "On" && v.lines[3].find("SIMULATED") == 0);
  press(*m, Key::Ok);
  CHECK(m->top()->view().lines[1] == "On   Volume 5/10" && m->top()->view().keys.center == "Off");
  press(*m, Key::Right);
  CHECK(m->top()->view().lines[0] == "98.8 MHz");
  press(*m, Key::Left); press(*m, Key::Left);
  CHECK(m->top()->view().lines[0] == "98.6 MHz");
  for (int i = 0; i < 300; i++) press(*m, Key::Right);                      // wraps past 108.0 back to 87.5
  int f = 0;
  std::sscanf(m->top()->view().lines[0].c_str(), "%d", &f);
  CHECK(f >= 87 && f <= 108);
  for (int i = 0; i < 400; i++) press(*m, Key::Left);
  std::sscanf(m->top()->view().lines[0].c_str(), "%d", &f);
  CHECK(f >= 87 && f <= 108);
  while (m->top()->view().lines[0] != "87.5 MHz") press(*m, Key::Left);
  press(*m, Key::Left);
  CHECK(m->top()->view().lines[0] == "108.0 MHz");                          // wraps below 87.5
  press(*m, Key::Right);
  CHECK(m->top()->view().lines[0] == "87.5 MHz");
  CHECK(m->top()->view().lines[2] == "No presets. Save adds one.");
  press(*m, Key::Soft1);                                                    // save 87.5 as preset 1
  press(*m, Key::Right); press(*m, Key::Right);
  press(*m, Key::Soft1);                                                    // 87.7 as preset 2
  CHECK(m->top()->view().lines[2] == "1: 87.5   2: 87.7");
  press(*m, Key::Num1);
  CHECK(m->top()->view().lines[0] == "87.5 MHz");
  press(*m, Key::Num5);                                                     // empty slot: nothing happens
  CHECK(m->top()->view().lines[0] == "87.5 MHz");
  press(*m, Key::Num2);
  CHECK(m->top()->view().lines[0] == "87.7 MHz");
  for (int i = 0; i < 20; i++) press(*m, Key::Down);
  CHECK(m->top()->view().lines[1] == "On   Volume 0/10");
  delete m;
}

static void test_recorder() {
  MemStorage store;
  auto* m = boot();
  m->files = &store;
  long long now = 5000;
  m->nowMs = [&] { return now; };
  launch(*m, "Recorder");
  auto v = m->top()->view();
  CHECK(title(*m) == "Recorder" && v.lines[0] == "Ready" && v.keys.center == "Record" && v.lines[1].find("SIMULATED") == 0);
  press(*m, Key::Ok);
  now += 3200;
  v = m->top()->view();
  CHECK(v.lines[0] == "Recording 0:03" && v.keys.center == "Stop");
  press(*m, Key::Ok);
  CHECK(store.exists("Recordings/REC_0001.wav") && m->top()->view().lines[1] == "Saved REC_0001.wav (simulated silence)" && m->top()->view().lines[0] == "Ready");
  std::string wav;
  store.read("Recordings/REC_0001.wav", wav);
  CHECK(wav == makeWav(3));
  press(*m, Key::Ok);                                                       // a sub-second take still saves a valid 1 s file
  now += 100;
  press(*m, Key::Ok);
  store.read("Recordings/REC_0002.wav", wav);
  CHECK(wav == makeWav(1));
  press(*m, Key::Soft1);                                                    // Recordings list -> player
  v = m->top()->view();
  CHECK(title(*m) == "Recordings" && v.rows.size() == 2 && v.rows[0].text == "REC_0002");
  press(*m, Key::Ok);
  CHECK(title(*m) == "REC_0002" && m->top()->view().lead == "music");
  delete m;

  auto* n = boot();
  launch(*n, "Recorder");
  press(*n, Key::Ok);
  CHECK(n->top()->view().lines[1] == "Storage unavailable" && n->top()->view().lines[0] == "Ready");  // refuses to "record" nowhere
  delete n;
}

static void test_seek() {
  MemStorage store;
  auto* m = boot();
  m->files = &store;
  long long now = 0;
  m->nowMs = [&] { return now; };
  store.write("Music/song.mp3", "not decodable");   // length unknown
  launch(*m, "Music");
  press(*m, Key::Ok);
  auto v = m->top()->view();
  CHECK(v.lines[1] == "Paused 0:00   1/1" && v.lines[4].find("Length unknown") == 0);   // says so instead of inventing a length
  press(*m, Key::Left);
  CHECK(m->top()->view().lines[1] == "Paused 0:00   1/1");                             // cannot seek before the start
  press(*m, Key::Right); press(*m, Key::Right);
  CHECK(m->top()->view().lines[1] == "Paused 0:20   1/1");
  press(*m, Key::Left);
  CHECK(m->top()->view().lines[1] == "Paused 0:10   1/1");
  press(*m, Key::Ok);                                                                  // play from 0:10, then seek while playing
  now += 5000;
  press(*m, Key::Right);
  CHECK(m->top()->view().lines[1] == "Playing 0:25   1/1");                            // 10 + 5 played + 10 skipped
  now += 1000;
  CHECK(m->top()->view().lines[1] == "Playing 0:26   1/1");                            // clock keeps running from the new spot
  press(*m, Key::Left); press(*m, Key::Left); press(*m, Key::Left);
  CHECK(m->top()->view().lines[1] == "Playing 0:00   1/1");
  delete m;

  // a WAV has a known length: seeking clamps to it and play at the end restarts
  auto* n = boot();
  n->files = &store;
  n->nowMs = [&] { return now; };
  store.write("Recordings/REC_0001.wav", makeWav(25));
  CHECK(wavDurationMs(makeWav(25)) == 25000 && wavDurationMs("junk") == -1 && wavDurationMs(makeWav(25).substr(0, 30)) == -1);
  CHECK(wavDurationMs(makeWav(25).substr(0, 44 + 8000)) == 500);                       // truncated file: only the bytes that exist count
  launch(*n, "Recorder");
  press(*n, Key::Soft1);
  press(*n, Key::Ok);
  v = n->top()->view();
  CHECK(v.lines[1] == "Paused 0:00 / 0:25   1/1" && v.lines[4].find("Length unknown") == std::string::npos);
  press(*n, Key::Right); press(*n, Key::Right); press(*n, Key::Right);
  CHECK(n->top()->view().lines[1] == "Finished 0:25 / 0:25   1/1");
  press(*n, Key::Ok);                                                                  // at the end: Play starts over
  CHECK(n->top()->view().lines[1] == "Playing 0:00 / 0:25   1/1");
  now += 40000;                                                                        // plays past the end -> stops at the end
  v = n->top()->view();
  CHECK(v.lines[1] == "Finished 0:25 / 0:25   1/1" && v.keys.center == "Play");
  delete n;
}

static void test_video_and_audio_recording_options() {
  CHECK(makeWav(1, 16000).size() == 44 + 32000 && wavDurationMs(makeWav(7, 16000)) == 7000 && makeWav(1, 44100).empty() && makeWav(1, 0).empty());
  CHECK(makeVideoStub(0).empty() && makeVideoStub(601).empty() && makeVideoStub(5).find("FAIOS-SIMULATED-VIDEO") == 0 && makeVideoStub(5).find("duration_seconds=5") != std::string::npos);
  MemStorage s;
  CHECK(nextVideoName(s) == "VID_0001.mp4");
  s.write("Videos/VID_0004.mp4", "x");
  CHECK(nextVideoName(s) == "VID_0005.mp4");

  MemStorage store;
  auto* m = boot();
  m->files = &store;
  long long now = 1000;
  m->nowMs = [&] { return now; };
  launch(*m, "Camera");
  auto v = m->top()->view();
  CHECK(v.lines[2] == "Photo" && v.keys.center == "Capture" && v.lines[3].empty());
  press(*m, Key::Right);                                                    // switch to video mode
  v = m->top()->view();
  CHECK(v.lines[2] == "Video" && v.keys.center == "Record");
  press(*m, Key::Ok);
  now += 4200;
  v = m->top()->view();
  CHECK(v.lines[3] == "REC 0:04" && v.keys.center == "Stop");
  press(*m, Key::Left);                                                     // mode is locked while recording
  CHECK(m->top()->view().lines[2] == "Video");
  press(*m, Key::Ok);
  v = m->top()->view();
  CHECK(store.exists("Videos/VID_0001.mp4") && v.lines[0] == "Saved VID_0001.mp4 (simulated, not real video)" && v.lines[3].empty() && v.lines[4] == "1");
  std::string data;
  store.read("Videos/VID_0001.mp4", data);
  CHECK(data == makeVideoStub(4) && !store.exists("DCIM/IMG_0001.bmp"));    // a video take does not create a photo
  press(*m, Key::Ok);                                                       // sub-second take still saves
  now += 50;
  press(*m, Key::Back);                                                     // Back while recording saves, and stays
  CHECK(title(*m) == "Camera" && store.exists("Videos/VID_0002.mp4") && m->top()->view().lines[3].empty());
  press(*m, Key::Back);
  CHECK(title(*m) != "Camera");
  press(*m, Key::End);
  launch(*m, "Videos");                                                     // recorded videos show up in the Videos app (and the Gallery)
  CHECK(m->top()->view().rows.size() == 2);
  delete m;

  auto* r = boot();
  r->files = &store;
  r->nowMs = [&] { return now; };
  launch(*r, "Recorder");
  v = r->top()->view();
  CHECK(v.lines[2] == "Quality: Voice 8 kHz  (Left/Right)");
  press(*r, Key::Right);
  CHECK(r->top()->view().lines[2] == "Quality: Standard 16 kHz  (Left/Right)");
  press(*r, Key::Ok);
  now += 2000;
  CHECK(r->top()->view().lines[2] == "Quality: Standard 16 kHz");
  press(*r, Key::Left);                                                     // locked while recording
  CHECK(r->top()->view().lines[2] == "Quality: Standard 16 kHz");
  press(*r, Key::Back);                                                     // saves, stays
  std::string wav;
  CHECK(title(*r) == "Recorder" && store.read("Recordings/REC_0001.wav", wav) && wav == makeWav(2, 16000) && wavDurationMs(wav) == 2000);
  press(*r, Key::Back);
  CHECK(title(*r) != "Recorder");
  delete r;
}
