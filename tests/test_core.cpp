// Plain-assert tests (no framework). Run via `ctest`.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include "calc.h"
#include "screens.h"

using namespace fai;

static int failures = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static std::string title(AppManager& m) { return m.top()->view().title; }
static void press(AppManager& m, Key k) { m.handleKey(k); }

// Menu paths: {app, top-level tile (1..9), item inside the tile (0 = tile opens the app directly)}.
// Top-level: 1 Phone, 2 Messages, 3 Camera, 4 Gallery, 5 Calendar, 6 Clock, 7 Games, 8 Tools, 9 System.
struct AppPath { const char* name; int top, sub; const char* title; int downs; };  // downs >= 0: Down x N then OK inside the item
static const AppPath kApps[] = {
    {"Dialer", 1, 1, "Phone", -1},   {"Contacts", 1, 2, "Contacts", -1},    {"Call history", 1, 3, "Call history", -1},
    {"Messages", 2, 0, "Messages", -1}, {"Camera", 3, 0, "Camera", -1}, {"Gallery", 4, 1, "Gallery", -1}, {"Music", 4, 2, "Music", -1}, {"Videos", 4, 3, "Videos", -1}, {"FM Radio", 4, 4, "FM Radio", -1}, {"Recorder", 4, 5, "Recorder", -1}, {"Calendar", 5, 0, "Calendar", -1},
    {"Clock", 6, 1, "Clock", -1},    {"Alarms", 6, 2, "Alarms", -1},
    {"Snake", 7, 1, "Snake", -1}, {"Tic-Tac-Toe", 7, 2, "Tic-Tac-Toe", -1}, {"Number guess", 7, 3, "Number guess", -1},
    {"Calculator", 8, 1, "Calculator", -1}, {"Files", 8, 2, "Files", -1},
    {"Settings", 9, 1, "Settings", -1}, {"Connectivity", 9, 2, "Connectivity", -1}, {"Battery", 9, 3, "Battery", -1},
    // reached through Connectivity (one owner each): rows are Wi-Fi, Wi-Fi networks, Bluetooth, Mobile data, Airplane, VPN, Hotspot, SIM manager
    {"VPN", 9, 2, "VPN", 5}, {"Hotspot", 9, 2, "Hotspot", 6}, {"SIM manager", 9, 2, "SIM manager", 7}};

// From Home: open the menu, then the tile, then the item.
static void launch(AppManager& m, const char* app) {
  for (auto& a : kApps)
    if (std::string(a.name) == app) {
      m.handleKey(Key::Ok);
      m.handleKey(Key(int(Key::Num0) + a.top));
      if (a.sub) m.handleKey(Key(int(Key::Num0) + a.sub));
      if (a.downs >= 0) {
        for (int i = 0; i < a.downs; i++) m.handleKey(Key::Down);
        m.handleKey(Key::Ok);
      }
      return;
    }
  std::printf("FAIL unknown app %s\n", app);
  failures++;
}

static AppManager* boot() {
  auto* m = new AppManager;
  m->push(makeHome(*m));
  return m;
}

static void test_launcher_and_back() {
  auto* m = boot();
  CHECK(title(*m) == "FaiOS");
  press(*m, Key::Ok);
  CHECK(title(*m) == "Menu");
  CHECK(m->depth() == 2);
  press(*m, Key::Back);
  CHECK(title(*m) == "FaiOS");
  press(*m, Key::Back);  // root is never popped
  CHECK(m->depth() == 1);
  delete m;
}

static void test_all_apps_reachable_by_name() {
  for (auto& a : kApps) {
    auto* m = boot();
    launch(*m, a.name);
    CHECK(title(*m) == a.title);
    press(*m, Key::End);  // End returns home from anywhere
    CHECK(m->depth() == 1);
    delete m;
  }
}

static void test_grouped_menu() {
  auto* m = boot();
  press(*m, Key::Ok);
  auto v = m->top()->view();
  CHECK(v.layout == Layout::Grid && v.rows.size() == 9);
  CHECK(v.rows[0].text == "Phone" && v.rows[0].sub == "Phone & Contacts");
  press(*m, Key::Num1);  // Phone tile opens a short list, not the dialer
  v = m->top()->view();
  CHECK(title(*m) == "Phone" && v.layout == Layout::List && v.rows.size() == 3);
  CHECK(v.rows[0].text == "Dialer" && v.rows[1].text == "Contacts" && v.rows[2].text == "Call history");
  press(*m, Key::Down);  // arrows + OK work as well as digits
  press(*m, Key::Ok);
  CHECK(title(*m) == "Contacts");
  press(*m, Key::Back);  // back to the group, then the menu
  CHECK(title(*m) == "Phone");
  press(*m, Key::Back);
  CHECK(title(*m) == "Menu");
  press(*m, Key::Num2);  // Messages opens directly (no group)
  CHECK(title(*m) == "Messages");
  delete m;
}

static void test_grid_navigation() {  // launcher: 9 tiles, 3 columns
  auto* m = boot();
  press(*m, Key::Ok);
  CHECK(m->top()->view().layout == Layout::Grid && m->top()->view().rows.size() == 9);
  auto sel = [&] { return m->top()->view().selected; };
  CHECK(sel() == 0);
  press(*m, Key::Right);
  CHECK(sel() == 1);
  press(*m, Key::Down);  // 1 + 3 = 4
  CHECK(sel() == 4);
  press(*m, Key::Down);  // 7
  CHECK(sel() == 7);
  press(*m, Key::Down);  // no row below: wraps to top of the column
  CHECK(sel() == 1);
  press(*m, Key::Up);    // wraps to bottom of the column
  CHECK(sel() == 7);
  press(*m, Key::Left);
  CHECK(sel() == 6);
  press(*m, Key::Up);
  CHECK(sel() == 3);
  press(*m, Key::Up);
  CHECK(sel() == 0);
  press(*m, Key::Right);
  press(*m, Key::Ok);    // index 1 = Messages
  CHECK(title(*m) == "Messages");
  delete m;
}

static void test_list_navigation_wraps() {  // settings: 3-item vertical list
  auto* m = boot();
  launch(*m, "Settings");
  CHECK(m->top()->view().layout == Layout::List && m->top()->view().selected == 0);
  press(*m, Key::Up);
  CHECK(m->top()->view().selected == 2);
  press(*m, Key::Down);
  CHECK(m->top()->view().selected == 0);
  delete m;
}

static void test_rows_and_contact_call() {
  auto* m = boot();
  press(*m, Key::Soft2);  // Home right softkey = Contacts
  auto v = m->top()->view();
  CHECK(title(*m) == "Contacts" && v.rows[0].text == "Ali" && v.rows[0].lead == "@A" && !v.rows[0].sub.empty());
  press(*m, Key::Ok);     // contact card
  CHECK(m->top()->view().layout == Layout::Text && m->top()->view().keys.left == "Call");
  press(*m, Key::Soft1);  // Call -> dialer prefilled with the digits
  CHECK(title(*m) == "Phone" && m->top()->view().lines[0] == "03001112233");
  delete m;
}

static void test_history_and_settings_rows() {
  auto* m = boot();
  launch(*m, "Call history");
  auto v = m->top()->view();
  CHECK(v.rows[0].tone == 1 && v.rows[0].lead == "call_miss");   // missed call = danger
  CHECK(v.rows[1].tone == 0 && v.rows[1].trail == "2:14");
  press(*m, Key::End);
  launch(*m, "Settings");
  v = m->top()->view();
  CHECK(v.rows[0].text == "Brightness" && v.rows[0].trail == "60%");
  press(*m, Key::Ok);
  CHECK(m->top()->view().rows[0].trail == "80%");
  press(*m, Key::End);
  launch(*m, "Connectivity");
  CHECK(m->top()->view().rows[0].toggle == 1);
  press(*m, Key::Ok);
  CHECK(m->top()->view().rows[0].toggle == 0);
  delete m;
}

static void test_dialer() {
  auto* m = boot();
  press(*m, Key::Num0);  // digit on home opens dialer prefilled
  CHECK(title(*m) == "Phone");
  press(*m, Key::Num3);
  CHECK(m->top()->view().lines[0] == "03");
  press(*m, Key::Soft2);  // Clear removes last digit, screen stays
  CHECK(m->top()->view().lines[0] == "0");
  CHECK(m->depth() == 2);
  press(*m, Key::Back);
  press(*m, Key::Back);  // empty -> now pops
  CHECK(m->depth() == 1);
  delete m;
}

static void test_dialer_does_not_fake_calls() {
  auto* m = boot();
  press(*m, Key::Num5);
  press(*m, Key::Call);
  CHECK(m->top()->view().lines[1].find("not placed") != std::string::npos);
  delete m;
}

static void test_messages_mark_read() {
  auto* m = boot();
  CHECK(m->device.unread() == 2);
  launch(*m, "Messages");
  press(*m, Key::Ok);    // open first (unread)
  CHECK(m->device.unread() == 1);
  CHECK(title(*m) == "Ali");
  delete m;
}

static void test_settings_toggle() {  // radios live in Connectivity only
  auto* m = boot();
  launch(*m, "Connectivity");
  bool before = m->device.wifi;
  press(*m, Key::Ok);  // first item = Wi-Fi
  CHECK(m->device.wifi != before);
  delete m;
}

static void test_one_owner_per_setting() {
  auto* m = boot();
  launch(*m, "Settings");
  for (auto& r : m->top()->view().rows) {
    CHECK(r.toggle < 0);                                                           // no switches in Settings
    CHECK(r.text != "Wi-Fi" && r.text != "Bluetooth" && r.text != "Hotspot");      // no radio duplicates
  }
  press(*m, Key::End);
  launch(*m, "Connectivity");
  int wifi = 0, bt = 0, hs = 0, sims = 0;
  for (auto& r : m->top()->view().rows) { wifi += r.text == "Wi-Fi"; bt += r.text == "Bluetooth"; hs += r.text == "Hotspot"; sims += r.text == "SIM manager"; }
  CHECK(wifi == 1 && bt == 1 && hs == 1 && sims == 1);
  press(*m, Key::End);
  press(*m, Key::Ok);
  press(*m, Key::Num9);  // System group: Settings, Connectivity, Battery (no separate Hotspot / SIM entries)
  auto v = m->top()->view();
  CHECK(v.rows.size() == 3 && v.rows[0].text == "Settings" && v.rows[1].text == "Connectivity" && v.rows[2].text == "Battery");
  delete m;
}

static void test_keymap_names() {
  for (int i = 0; i < int(Key::Count); i++) {
    Key out;
    CHECK(keyFromName(keyName(Key(i)), out) && out == Key(i));
  }
  Key out;
  CHECK(!keyFromName("Bogus", out));
}


static void test_logger() {
  Logger l(3);
  int sunk = 0;
  l.sink = [&](const LogEntry&) { sunk++; };
  l.log(Level::Debug, "t", "dropped");  // below default min
  for (int i = 0; i < 5; i++) l.log(Level::Info, "t", std::to_string(i));
  CHECK(l.entries().size() == 3);
  CHECK(l.entries().front().msg == "2" && l.entries().back().msg == "4");
  CHECK(sunk == 5);
}

static void test_storage_and_safety() {
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / "faios-test-storage";
  fs::remove_all(dir);
  FileStorage f(dir.string());
  std::string out;
  CHECK(!f.exists("a/b.txt"));
  CHECK(f.write("a/b.txt", "hello"));
  CHECK(f.exists("a/b.txt") && f.read("a/b.txt", out) && out == "hello");
  CHECK(f.write("a/b.txt", "v2") && f.read("a/b.txt", out) && out == "v2");  // overwrite
  CHECK(!f.exists("a/b.txt.tmp"));                                           // no temp left behind
  CHECK(!f.write("../escape", "x"));
  CHECK(!f.write("/abs", "x"));
  CHECK(!f.read("../escape", out));
  MemStorage m;
  CHECK(m.write("k", "v") && m.read("k", out) && out == "v");
  CHECK(!m.write("../k", "v"));
  fs::remove_all(dir);
}

static void test_config() {
  MemStorage s;
  Config c(s);
  CHECK(!c.load());  // missing file is not fatal
  CHECK(c.getInt("n", 7) == 7 && c.getBool("b", true));
  c.setInt("n", 42);
  c.setBool("b", false);
  c.setString("s", "a=b\nc");  // newline/'=' must not corrupt the file
  CHECK(c.save());
  Config d(s);
  CHECK(d.load());
  CHECK(d.getInt("n", 0) == 42 && !d.getBool("b", true) && d.getString("s") == "a=b c");
  s.write("config.ini", "# c\nn = notanumber\n  x=1\nbroken line\n");
  Config e(s);
  e.load();
  CHECK(e.getInt("n", 5) == 5 && e.getInt("x", 0) == 1);  // junk -> default
}

static void test_notifications() {
  Notifications n;
  int a = n.add("A", "x"), b = n.add("B", "y");
  CHECK(a != b && n.count() == 2);
  CHECK(n.dismiss(a) && !n.dismiss(a) && n.count() == 1);
  n.clear();
  CHECK(n.count() == 0);
}

static void test_power_and_status() {
  AppManager m;
  m.power->setBrightness(250);
  CHECK(m.power->brightness() == 100);
  m.power->setBrightness(-5);
  CHECK(m.power->brightness() == 0);
  CHECK(m.status().badge == 2);
  m.notifications.add("Alarm", "");
  CHECK(m.status().badge == 3);
}

struct Probe : Service {
  Probe(const char* n, std::string& log, bool ok = true) : n_(n), log_(log), ok_(ok) {}
  const char* name() const override { return n_; }
  bool start() override { log_ += std::string("+") + n_; return ok_; }
  void stop() override { log_ += std::string("-") + n_; }
  const char* n_; std::string& log_; bool ok_;
};

static void test_service_lifecycle() {
  Logger l;
  std::string t;
  ServiceManager sm(l);
  sm.add(std::make_unique<Probe>("a", t));
  sm.add(std::make_unique<Probe>("b", t));
  CHECK(sm.startAll() && sm.running("a") && sm.running("b"));
  sm.stopAll();
  CHECK(t == "+a+b-b-a" && !sm.running("a"));

  std::string t2;
  ServiceManager bad(l);
  bad.add(std::make_unique<Probe>("a", t2));
  bad.add(std::make_unique<Probe>("b", t2, false));
  CHECK(!bad.startAll());
  CHECK(t2 == "+a+b-a" && !bad.running("a"));  // rolled back
}

static void test_settings_persist() {
  MemStorage s;
  {
    AppManager m;
    Config c(s);
    m.config = &c;
    m.push(makeHome(m));
    launch(m, "Connectivity");
    press(m, Key::Ok);    // toggle Wi-Fi (default on -> off)
    press(m, Key::End);
    launch(m, "Settings");
    press(m, Key::Ok);    // Brightness 60 -> 80
  }
  AppManager m2;
  Config c2(s);
  c2.load();
  m2.config = &c2;
  m2.loadSettings();
  CHECK(!m2.device.wifi);
  CHECK(m2.power->brightness() == 80);
}

#include "test_apps.inc"
#include "test_more_apps.inc"
#include "test_accounts.inc"
#include "test_vpn.inc"
#include "test_games.inc"
#include "test_gallery.inc"
#include "test_media_apps.inc"
#include "test_browser.inc"

int main() {
  test_launcher_and_back();
  test_all_apps_reachable_by_name();
  test_grouped_menu();
  test_grid_navigation();
  test_list_navigation_wraps();
  test_rows_and_contact_call();
  test_history_and_settings_rows();
  test_dialer();
  test_dialer_does_not_fake_calls();
  test_messages_mark_read();
  test_settings_toggle();
  test_one_owner_per_setting();
  test_keymap_names();
  test_logger();
  test_storage_and_safety();
  test_config();
  test_notifications();
  test_power_and_status();
  test_service_lifecycle();
  test_settings_persist();
  test_calc_logic();
  test_calc_screen_keys();
  test_clock_stopwatch();
  test_sim_manager_and_hotspot();
  test_storage_list_remove();
  test_file_manager();
  test_connectivity();
  test_battery_screen();
  test_alarms();
  test_notifications_screen();
  test_sha256_and_base64();
  test_pkce_rfc7636_vector();
  test_json();
  test_auth_url_and_redirect();
  test_token_endpoint();
  test_id_token_validation();
  test_account_sign_in_and_storage();
  test_account_sign_in_failures();
  test_no_secure_storage_fails_closed();
  test_token_refresh_and_reauth();
  test_tampered_tokens_need_reauth();
  test_scope_changes_and_sync_flags();
  test_remove_account();
  test_ip_parsing();
  test_wireguard_parse();
  test_tunnel_commands();
  test_kill_switch_ruleset();
  test_dns_text();
  test_network_manager();
  test_connectivity_monitor();
  test_vpn_store();
  test_vpn_connect_verifies_before_claiming_protection();
  test_vpn_simulated_backend_is_flagged();
  test_vpn_connect_failures();
  test_vpn_handshake_timeout_and_backoff();
  test_vpn_without_auto_reconnect_stops_in_error();
  test_vpn_kill_switch_behaviour();
  test_vpn_network_loss_and_return();
  test_vpn_route_and_handshake_health();
  test_vpn_endpoint_resolution_cache();
  test_vpn_logs_never_contain_secrets();
  test_command_backend();
  test_vpn_screens();
  test_vpn_import_screen();
  test_vpn_tick_runs_through_app_manager();
  test_rng();
  test_snake_logic();
  test_tictactoe_logic();
  test_guess_logic();
  test_calendar_math();
  test_event_store();
  test_snake_screen();
  test_tictactoe_screen();
  test_guess_screen();
  test_calendar_screens();
  test_frame_is_harmless_on_other_screens();
  test_media_kind();
  test_bmp_codec();
  test_gallery_listing();
  test_camera_and_gallery_screens();
  test_gallery_browse_view_delete();
  test_gallery_menu_tile();
  test_media_helpers();
  test_media_group_menu();
  test_music_player();
  test_videos_app();
  test_fm_radio();
  test_recorder();
  test_seek();
  test_video_and_audio_recording_options();
  test_url_policy();
  test_tab_manager();
  test_tab_budgets();
  test_accounts_screens();
  test_account_detail_and_sign_out();
  test_mock_signin_hook_is_visible_only_when_set();
#ifdef FAIOS_HAVE_OPENSSL
  test_aes_gcm_secret_box();
#else
  std::printf("SKIPPED: AES-GCM secret box tests (OpenSSL development files not installed)\n");
#endif
  if (failures) std::printf("%d FAILED of %d checks\n", failures, checks);
  else std::printf("all passed (%d checks)\n", checks);
  return failures ? 1 : 0;
}
