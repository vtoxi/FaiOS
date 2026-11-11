// Included by test_core.cpp: game logic, calendar maths, event store, and the Games / Calendar screens.
#include "calendar.h"
#include "events.h"
#include "games.h"

static std::time_t localTime(int y, int mo, int d, int h = 12) {
  std::tm t = {};
  t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_isdst = -1;
  return std::mktime(&t);
}

static void test_rng() {
  Rng a(0), b(0);
  CHECK(a.next() != 0 && a.next() != 0);                  // seed 0 would freeze xorshift: it is replaced
  CHECK(Rng(5).next() == Rng(5).next() && Rng(5).next() != Rng(6).next());  // deterministic per seed
  Rng r(123);
  int seen[10] = {0};
  for (int i = 0; i < 2000; i++) { int v = r.below(10); CHECK(v >= 0 && v < 10); seen[v]++; }
  for (int i = 0; i < 10; i++) CHECK(seen[i] > 100);      // roughly uniform: no bucket starved
  CHECK(b.below(1) == 0);
}

static int countCh(const std::vector<std::string>& rows, char c) {
  int n = 0;
  for (auto& r : rows) n += int(std::count(r.begin(), r.end(), c));
  return n;
}
static std::pair<int, int> findCh(const std::vector<std::string>& rows, char c) {
  for (int y = 0; y < int(rows.size()); y++) { size_t x = rows[y].find(c); if (x != std::string::npos) return {int(x), y}; }
  return {-1, -1};
}

// Steers toward the food without reversing or stepping into a wall or its own body.
static void botSteer(Snake& s, Dir& cur) {
  auto rows = s.board();
  auto h = findCh(rows, '@'), f = findCh(rows, '*');
  struct Opt { Dir d; int dx, dy; };
  const Opt opts[4] = {{Dir::Right, 1, 0}, {Dir::Left, -1, 0}, {Dir::Down, 0, 1}, {Dir::Up, 0, -1}};
  int best = 1 << 30;
  Dir pick = cur;
  for (auto& o : opts) {
    bool reverse = (o.d == Dir::Left && cur == Dir::Right) || (o.d == Dir::Right && cur == Dir::Left) || (o.d == Dir::Up && cur == Dir::Down) || (o.d == Dir::Down && cur == Dir::Up);
    int nx = h.first + o.dx, ny = h.second + o.dy;
    if (reverse || nx < 0 || ny < 0 || ny >= int(rows.size()) || nx >= int(rows[0].size()) || rows[ny][nx] == 'o') continue;
    int dist = std::abs(nx - f.first) + std::abs(ny - f.second);
    if (dist < best) { best = dist; pick = o.d; }
  }
  cur = pick;
  s.turn(pick);
}

static void test_snake_logic() {
  Snake s(14, 13, 42);
  auto b = s.board();
  CHECK(b.size() == 13 && b[0].size() == 14 && countCh(b, '@') == 1 && countCh(b, 'o') == 2 && countCh(b, '*') == 1);
  auto head = findCh(b, '@');
  CHECK(head.first == 7 && head.second == 6);              // centre, heading right
  CHECK(s.alive() && s.score() == 0);

  s.turn(Dir::Left);                                       // an instant reversal would be suicide: ignored
  s.step();
  CHECK(findCh(s.board(), '@') == std::make_pair(8, 6) && s.alive());
  s.turn(Dir::Up);
  s.step();
  CHECK(findCh(s.board(), '@') == std::make_pair(8, 5));
  s.turn(Dir::Down);                                       // Up -> Down is also a reversal
  s.step();
  CHECK(findCh(s.board(), '@') == std::make_pair(8, 4) && s.alive());

  Snake wall(14, 13, 1);                                   // running into the wall ends the game, and it stays ended
  int steps = 0;
  while (wall.alive() && steps < 100) { wall.step(); steps++; }
  CHECK(!wall.alive() && steps == 7 && !wall.step());

  // a bot proves eating works: score and length grow by one per food, and food respawns on a free cell
  Snake bot(14, 13, 7);
  Dir dir = Dir::Right;
  for (int i = 0; i < 600 && bot.alive() && bot.score() < 6; i++) { botSteer(bot, dir); bot.step(); }
  CHECK(bot.score() == 6 && bot.alive());
  auto rows = bot.board();
  CHECK(countCh(rows, '@') == 1 && countCh(rows, 'o') == 2 + 6 && countCh(rows, '*') == 1);

  // biting yourself: with a long body, folding back on it in a tight loop is fatal
  bool died = false;
  for (int i = 0; i < 12 && !died; i++) {
    for (Dir d : {Dir::Up, Dir::Left, Dir::Down}) { bot.turn(d); if (!bot.step()) { died = true; break; } }
  }
  CHECK(died && !bot.alive());

  CHECK(Snake(14, 13, 9).board() == Snake(14, 13, 9).board());  // same seed, same game
  CHECK(Snake(14, 13, 9).board() != Snake(14, 13, 10).board());  // different seeds put the food elsewhere
}

static void test_tictactoe_logic() {
  TicTacToe g(5);
  CHECK(g.winner() == 0 && g.at(4) == '.');
  CHECK(g.play(0) && g.at(0) == 'X');
  CHECK(g.at(4) == 'O');                                  // computer takes the centre
  CHECK(!g.play(0) && !g.play(4) && !g.play(-1) && !g.play(9));  // occupied / out of range
  int x = 0, o = 0;
  for (int i = 0; i < 9; i++) { x += g.at(i) == 'X'; o += g.at(i) == 'O'; }
  CHECK(x == 1 && o == 1);

  // the computer blocks an immediate win
  TicTacToe b(1);
  b.play(0);          // X at 0, O takes the centre
  b.play(1);          // X threatens 0-1-2: O must block at 2
  CHECK(b.at(2) == 'O');
  // and takes its own win when it has one
  b.play(3);          // X blocks nothing: O (4, 2) can now complete 2-4-6 unless it must block 0-3-6 first
  CHECK(b.at(6) == 'O');

  // a finished game accepts no more moves
  for (uint32_t seed = 1; seed < 60; seed++) {
    TicTacToe t(seed);
    for (int c = 0; c < 9 && !t.winner(); c++) t.play(c);
    if (t.winner()) { bool any = false; for (int c = 0; c < 9; c++) any |= t.play(c); CHECK(!any); }
  }
  // the human CAN win: this AI wins/blocks/takes the centre, then plays randomly, so it is beatable.
  // Search every human line of play for some seed; if none existed the test (and the game) would be pointless.
  struct Dfs {
    static bool humanCanWin(const TicTacToe& g) {
      if (g.winner()) return g.winner() == 'X';
      for (int c = 0; c < 9; c++) {
        TicTacToe n = g;
        if (n.play(c) && humanCanWin(n)) return true;
      }
      return false;
    }
  };
  bool humanWon = false;
  for (uint32_t seed = 1; seed < 200 && !humanWon; seed++) humanWon = Dfs::humanCanWin(TicTacToe(seed));
  CHECK(humanWon);
  g.reset();
  CHECK(g.winner() == 0 && g.at(0) == '.');
}

static void test_guess_logic() {
  for (uint32_t seed : {1u, 2u, 99u, 12345u}) {
    Guess g(seed);
    CHECK(g.secret() >= 1 && g.secret() <= 100 && g.tries() == 0);
    CHECK(g.guess(0) == Guess::Invalid && g.guess(101) == Guess::Invalid && g.guess(-5) == Guess::Invalid && g.tries() == 0);  // invalid input costs nothing
    int lo = 1, hi = 100, tries = 0;                      // binary search must always win within 7 guesses
    Guess::Result r = Guess::Low;
    while (r != Guess::Right && tries < 20) {
      int mid = (lo + hi) / 2;
      r = g.guess(mid);
      tries++;
      if (r == Guess::Low) { CHECK(mid < g.secret()); lo = mid + 1; }
      else if (r == Guess::High) { CHECK(mid > g.secret()); hi = mid - 1; }
    }
    CHECK(r == Guess::Right && tries <= 7 && g.tries() == tries);
  }
  CHECK(Guess(3).secret() == Guess(3).secret());
}

static void test_calendar_math() {
  CHECK(isLeap(2024) && isLeap(2000) && !isLeap(1900) && !isLeap(2026) && isLeap(2400));
  CHECK(daysInMonth(2024, 2) == 29 && daysInMonth(2026, 2) == 28 && daysInMonth(1900, 2) == 28 && daysInMonth(2026, 11) == 30 && daysInMonth(2026, 12) == 31);
  CHECK(weekday(2026, 11, 11) == 3);                      // Wednesday
  CHECK(weekday(2000, 1, 1) == 6 && weekday(1970, 1, 1) == 4 && weekday(2024, 2, 29) == 4 && weekday(2026, 11, 1) == 0);
  Date d = addDays({2026, 12, 31}, 1);
  CHECK(d.y == 2027 && d.m == 1 && d.d == 1);
  d = addDays({2026, 3, 1}, -1);
  CHECK(d.y == 2026 && d.m == 2 && d.d == 28);
  d = addDays({2024, 3, 1}, -1);
  CHECK(d.y == 2024 && d.m == 2 && d.d == 29);            // leap day
  d = addDays({2026, 1, 1}, -1);
  CHECK(d.y == 2025 && d.m == 12 && d.d == 31);
  d = addDays({2026, 11, 11}, 7);
  CHECK(d.m == 11 && d.d == 18);
  d = addDays({2026, 11, 28}, 7);
  CHECK(d.m == 12 && d.d == 5);
  d = addDays({2026, 5, 15}, 0);
  CHECK(d.m == 5 && d.d == 15);
  Date walk = {2026, 11, 11};
  for (int i = 0; i < 400; i++) walk = addDays(walk, 1);  // adding then subtracting returns to the start
  for (int i = 0; i < 400; i++) walk = addDays(walk, -1);
  CHECK(walk.y == 2026 && walk.m == 11 && walk.d == 11);
  CHECK(validDate({2026, 2, 28}) && !validDate({2026, 2, 29}) && !validDate({2026, 13, 1}) && !validDate({2026, 4, 31}) && !validDate({1969, 1, 1}) && !validDate({2026, 1, 0}));
}

static void test_event_store() {
  MemStorage s;
  EventStore st(s);
  CHECK(st.load() && st.all().empty());
  int a = st.add({2026, 11, 12}, 9 * 60 + 30, 45, "Dentist");
  int b = st.add({2026, 11, 12}, -1, 0, "Holiday");
  int c = st.add({2026, 11, 12}, 8 * 60, 30, "  ");
  CHECK(a == 1 && b == 2 && c == 3);
  auto day = st.forDay({2026, 11, 12});
  CHECK(day.size() == 3 && day[0].title == "Holiday" && day[1].startMin == 480 && day[1].title == "Event" && day[2].title == "Dentist");  // all-day first, then by time
  CHECK(st.hasEvents({2026, 11, 12}) && !st.hasEvents({2026, 11, 13}));
  CHECK(day[2].source == "local" && st.add({2026, 11, 14}, 600, 30, "From account", "acct-1") == 4 && st.forDay({2026, 11, 14})[0].source == "acct-1");

  // persistence, ids keep increasing even after removals
  EventStore st2(s);
  CHECK(st2.load() && st2.all().size() == 4 && st2.forDay({2026, 11, 12})[2].durationMin == 45);
  CHECK(st2.remove(4) && !st2.remove(4) && st2.add({2026, 11, 15}, 0, 10, "x") == 5);
  EventStore st3(s);
  st3.load();
  CHECK(st3.all().size() == 4 && !st3.hasEvents({2026, 11, 14}));

  // rejected input
  CHECK(st.add({2026, 2, 30}, 0, 10, "bad date") == 0 && st.add({2026, 11, 1}, 1440, 10, "bad time") == 0 && st.add({2026, 11, 1}, -2, 10, "bad") == 0 &&
        st.add({2026, 11, 1}, 0, -1, "bad dur") == 0 && st.add({2026, 11, 1}, 0, 5000, "bad dur") == 0);
  int id = st.add({2026, 11, 1}, 0, 10, std::string("line\nbreak\t") + std::string(100, 'z'));
  CHECK(id > 0);
  for (auto& e : st.forDay({2026, 11, 1})) CHECK(e.title.find('\n') == std::string::npos && e.title.size() <= 60);

  // damaged files: unreadable = ignored (and never overwritten blindly), bad rows dropped, good rows kept
  MemStorage bad;
  bad.write("calendar/local.json", "{broken");
  EventStore sb(bad);
  CHECK(!sb.load() && sb.all().empty());
  bad.write("calendar/local.json", "{\"next_id\":3,\"events\":[{\"id\":1,\"y\":2026,\"m\":13,\"d\":1,\"start\":0,\"dur\":5,\"title\":\"bad\"},{\"id\":2,\"y\":2026,\"m\":1,\"d\":5,\"start\":30,\"dur\":5,\"title\":\"ok\"},{\"id\":0,\"y\":2026,\"m\":1,\"d\":5}]}");
  EventStore sg(bad);
  CHECK(sg.load() && sg.all().size() == 1 && sg.all()[0].title == "ok" && sg.add({2026, 1, 6}, 0, 5, "n") == 3);

  // storage that cannot write: nothing is half-added
  struct ReadOnly : MemStorage { bool write(const std::string&, const std::string&) override { return false; } } ro;
  EventStore sr(ro);
  CHECK(sr.add({2026, 11, 1}, 0, 5, "x") == 0 && sr.all().empty());

  EventStore full(s);
  full.load();
  MemStorage big;
  EventStore fb(big);
  for (size_t i = 0; i < EventStore::kMax; i++) fb.add({2026, 1, 1 + int(i % 28)}, 0, 5, "e");
  CHECK(fb.all().size() == EventStore::kMax && fb.add({2026, 1, 1}, 0, 5, "one too many") == 0);
}

static std::pair<int, int> boardHead(AppManager& m) {
  auto v = m.top()->view();
  std::vector<std::string> rows(v.lines.begin() + 1, v.lines.end());
  return findCh(rows, '@');
}

static void test_snake_screen() {
  auto* m = boot();
  long long now = 5000;
  m->nowMs = [&] { return now; };
  launch(*m, "Snake");
  CHECK(title(*m) == "Snake" && m->top()->view().layout == Layout::Board && m->top()->view().lines.size() == 1 + 13 && m->top()->view().lines[0] == "Score 0");
  CHECK(m->top()->view().keys.center == "Pause");
  CHECK(boardHead(*m) == std::make_pair(7, 6));
  m->frame();                                              // first frame only starts the clock
  CHECK(boardHead(*m) == std::make_pair(7, 6));
  now += 199; m->frame();
  CHECK(boardHead(*m) == std::make_pair(7, 6));            // one step every 200 ms at the start
  now += 1; m->frame();
  CHECK(boardHead(*m) == std::make_pair(8, 6));
  now += 400; m->frame();
  CHECK(boardHead(*m) == std::make_pair(10, 6));
  press(*m, Key::Down);
  now += 200; m->frame();
  CHECK(boardHead(*m) == std::make_pair(10, 7));

  press(*m, Key::Ok);                                      // pause: time passes, nothing moves, no catch-up afterwards
  CHECK(m->top()->view().keys.center == "Resume" && m->top()->view().lines[0].find("Paused") != std::string::npos);
  now += 5000; m->frame();
  CHECK(boardHead(*m) == std::make_pair(10, 7));
  press(*m, Key::Ok);
  now += 200; m->frame();
  CHECK(boardHead(*m) == std::make_pair(10, 8));           // exactly one step, not a burst
  now += 100000; m->frame();                               // a long stall (e.g. the process was suspended) advances at most a few steps
  CHECK(m->top()->view().lines[0].find("Game over") != std::string::npos || boardHead(*m).second <= 12);

  for (int i = 0; i < 20 && m->top()->view().lines[0].find("Game over") == std::string::npos; i++) { now += 200; m->frame(); }
  CHECK(m->top()->view().lines[0].find("Game over") != std::string::npos && m->top()->view().keys.center == "Retry");  // walked into the wall
  press(*m, Key::Ok);                                      // retry
  CHECK(m->top()->view().lines[0] == "Score 0" && boardHead(*m) == std::make_pair(7, 6) && m->top()->view().keys.center == "Pause");
  CHECK(!m->top()->onKey(Key::Num5));                      // unrelated keys are not consumed
  press(*m, Key::Back);
  CHECK(title(*m) == "Games");
  delete m;
}

static void test_tictactoe_screen() {
  auto* m = boot();
  launch(*m, "Tic-Tac-Toe");
  auto v = m->top()->view();
  CHECK(title(*m) == "Tic-Tac-Toe" && v.lines.size() == 4 && v.lines[0] == "Your turn (X)" && v.selected == 4 && v.keys.center == "Place");
  press(*m, Key::Left);
  CHECK(m->top()->view().selected == 3);
  press(*m, Key::Left);
  CHECK(m->top()->view().selected == 5);                   // wraps along the row
  press(*m, Key::Up); press(*m, Key::Up);
  CHECK(m->top()->view().selected == 8);                   // 5 -> 2 -> 8: wraps along the column
  press(*m, Key::Down);
  CHECK(m->top()->view().selected == 2);
  press(*m, Key::Ok);                                      // X at 2, the computer answers
  v = m->top()->view();
  int x = 0, o = 0;
  for (int r = 1; r <= 3; r++) for (char c : v.lines[r]) { x += c == 'X'; o += c == 'O'; }
  CHECK(v.lines[1][2] == 'X' && x == 1 && o == 1);
  press(*m, Key::Ok);                                      // occupied cell: nothing happens
  x = o = 0;
  v = m->top()->view();                                   // keep the View alive: iterating a line of a temporary dangles
  for (int r = 1; r <= 3; r++) for (char c : v.lines[r]) { x += c == 'X'; o += c == 'O'; }
  CHECK(x == 1 && o == 1);
  for (int guard = 0; guard < 30 && m->top()->view().keys.center == "Place"; guard++) {  // play it out
    press(*m, Key::Right);
    press(*m, Key::Ok);
    if (guard % 3 == 2) press(*m, Key::Down);
  }
  v = m->top()->view();
  CHECK(v.keys.center == "Again" && (v.lines[0] == "You win!" || v.lines[0] == "Computer wins" || v.lines[0] == "Draw"));
  press(*m, Key::Ok);
  v = m->top()->view();
  CHECK(v.lines[0] == "Your turn (X)" && v.lines[1] == "..." && v.lines[2] == "..." && v.lines[3] == "...");
  delete m;
}

static void test_guess_screen() {
  auto* m = boot();
  long long now = 777;
  m->nowMs = [&] { return now; };
  launch(*m, "Number guess");
  int secret = Guess((uint32_t(now & 0xFFFFFFFFLL)) | 1u).secret();   // same seed rule the screen uses
  auto v = m->top()->view();
  CHECK(title(*m) == "Number guess" && v.layout == Layout::Dial && v.lines[0].empty() && v.keys.right == "Back");
  auto type = [&](int n) {
    for (char c : std::to_string(n)) press(*m, Key(int(Key::Num0) + (c - '0')));
    press(*m, Key::Ok);
  };
  int wrong = secret == 50 ? 51 : 50;
  type(wrong);
  CHECK(m->top()->view().lines[1] == (wrong < secret ? "Higher" : "Lower"));
  press(*m, Key::Num1); press(*m, Key::Num2); press(*m, Key::Num3); press(*m, Key::Num4);   // 3-digit limit
  CHECK(m->top()->view().lines[0] == "123" && m->top()->view().keys.right == "Clear");
  press(*m, Key::Back);
  CHECK(m->top()->view().lines[0] == "12");
  press(*m, Key::Ok);                                      // 12
  type(0);                                                 // 0 is not in range
  CHECK(m->top()->view().lines[1] == "Pick 1 to 100");
  type(secret);
  v = m->top()->view();
  CHECK(v.lines[1].find("Correct!") == 0 && v.lines[1].find("tries") != std::string::npos && v.keys.left == "Again");
  press(*m, Key::Num5);                                    // no typing after winning
  CHECK(m->top()->view().lines[0].empty());
  press(*m, Key::Ok);                                      // new game
  CHECK(m->top()->view().lines[1] == "I am thinking of 1 to 100" && m->top()->view().keys.left == "Guess");
  delete m;
}

static void test_calendar_screens() {
  MemStorage store;
  EventStore ev(store);
  ev.load();
  auto* m = boot();
  m->events = &ev;
  m->wall = [] { return localTime(2026, 11, 11); };
  launch(*m, "Calendar");
  auto v = m->top()->view();
  CHECK(title(*m) == "Calendar" && v.layout == Layout::Calendar && v.lines.size() == 7);
  CHECK(v.lines[0] == "November 2026" && v.lines[1] == "6" && v.lines[2] == "30" && v.lines[3] == "11" && v.lines[4] == "11" && v.lines[5] == "Wednesday 11 November 2026");
  CHECK(v.lines[6] == std::string(30, '0'));
  press(*m, Key::Left);
  CHECK(m->top()->view().lines[3] == "10" && m->top()->view().lines[5] == "Tuesday 10 November 2026");
  press(*m, Key::Down);
  CHECK(m->top()->view().lines[3] == "17");
  for (int i = 0; i < 3; i++) press(*m, Key::Up);          // 17 -> 10 -> 3 -> Oct 27
  v = m->top()->view();
  CHECK(v.lines[0] == "October 2026" && v.lines[3] == "27" && v.lines[2] == "31" && v.lines[4] == "0");  // "today" only shows in the current month
  for (int i = 0; i < 5; i++) press(*m, Key::Down);        // 27 -> Nov 3, 10, 17, 24 -> Dec 1
  CHECK(m->top()->view().lines[0] == "December 2026" && m->top()->view().lines[3] == "1" && m->top()->view().lines[1] == "1");  // Dec 1 2026 is a Tuesday
  press(*m, Key::Soft1);                                   // Today
  CHECK(m->top()->view().lines[0] == "November 2026" && m->top()->view().lines[3] == "11");

  press(*m, Key::Ok);                                      // that day's events
  CHECK(title(*m) == "11 Nov 2026" && m->top()->view().rows.size() == 1 && m->top()->view().rows[0].text == "Add event");
  press(*m, Key::Ok);
  CHECK(title(*m) == "New event" && m->top()->view().layout == Layout::Dial && m->top()->view().lines[0] == "__:__");
  press(*m, Key::Ok);
  CHECK(m->top()->view().lines[1] == "Enter 4 digits (HHMM)");
  for (int k : {2, 5, 6, 1}) press(*m, Key(int(Key::Num0) + k));
  press(*m, Key::Ok);
  CHECK(m->top()->view().lines[1] == "Invalid time" && ev.all().empty());
  for (int i = 0; i < 4; i++) press(*m, Key::Back);
  for (int k : {0, 9, 3, 0}) press(*m, Key(int(Key::Num0) + k));
  CHECK(m->top()->view().lines[0] == "09:30");
  press(*m, Key::Ok);
  CHECK(title(*m) == "11 Nov 2026" && ev.all().size() == 1 && ev.all()[0].startMin == 570 && ev.all()[0].source == "local");
  v = m->top()->view();
  CHECK(v.rows.size() == 2 && v.rows[1].text == "09:30" && v.rows[1].sub == "Event" && v.keys.center == "Add");
  press(*m, Key::Back);                                    // the month grid shows a mark on that day
  v = m->top()->view();
  CHECK(v.lines[6].size() == 30 && v.lines[6][10] == '1' && std::count(v.lines[6].begin(), v.lines[6].end(), '1') == 1);

  press(*m, Key::Ok);
  press(*m, Key::Down);
  CHECK(m->top()->view().keys.center == "Delete");
  press(*m, Key::Ok);
  CHECK(title(*m) == "Delete event?");
  press(*m, Key::Down);
  press(*m, Key::Ok);                                      // cancel
  CHECK(title(*m) == "11 Nov 2026" && ev.all().size() == 1);
  press(*m, Key::Ok);
  press(*m, Key::Ok);                                      // delete
  CHECK(title(*m) == "11 Nov 2026" && ev.all().empty() && m->top()->view().rows.size() == 1);
  EventStore again(store);                                 // and it is really gone from storage
  again.load();
  CHECK(again.all().empty());
  delete m;

  auto* n = boot();                                        // calendar storage unavailable: the grid still works, adding does not pretend
  n->wall = [] { return localTime(2026, 11, 11); };
  launch(*n, "Calendar");
  CHECK(n->top()->view().lines[6] == std::string(30, '0'));
  press(*n, Key::Ok);
  CHECK(n->top()->view().rows[0].sub == "Events unavailable");
  press(*n, Key::Ok);
  CHECK(title(*n) == "11 Nov 2026");
  delete n;
}

static void test_frame_is_harmless_on_other_screens() {
  auto* m = boot();
  bool changed = false;
  m->onChange = [&] { changed = true; };
  m->frame();                                              // Home does not animate: no redraw requested
  CHECK(!changed);
  launch(*m, "Calculator");
  changed = false;
  m->frame();
  CHECK(!changed);
  delete m;
}
