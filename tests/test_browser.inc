// Included by test_core.cpp: Fai Browser URL policy and the tab memory manager (fake engine; no web engine is linked yet).
#include <map>

#include "browser.h"

static void test_url_policy() {
  auto ok = [](const char* in, const char* want) { UrlResult r = normalizeUrl(in); return r.ok && r.url == want; };
  CHECK(ok("example.com", "https://example.com") && ok("  example.com/a?b=1#c  ", "https://example.com/a?b=1#c") && ok("http://example.com", "http://example.com"));
  CHECK(ok("HTTPS://Example.com/Path", "HTTPS://Example.com/Path"));  // scheme case is kept, the check on it is case-insensitive
  CHECK(ok("localhost:8080/x", "https://localhost:8080/x") && ok("10.0.0.1:81", "https://10.0.0.1:81") && ok("[::1]:80", "https://[::1]:80"));
  for (const char* bad : {"", "   ", "javascript:alert(1)", "JaVaScRiPt:alert(1)", "data:text/html,<b>x</b>", "file:///etc/passwd", "ftp://example.com", "blob:https://x/y",
                          "about:blank", "chrome://settings", "https://user:pass@example.com", "https://trusted.com@evil.com", "http://", "https://", "https://:80", "https://.com",
                          "exa mple.com", "example.com\n", "https://exa<mple.com", "https://exa_mple.com", "mailto:a@b.c", "tel:123"}) {
    UrlResult r = normalizeUrl(bad);
    if (r.ok) std::printf("accepted: [%s] -> %s\n", bad, r.url.c_str());
    CHECK(!r.ok && !r.error.empty());
  }
  CHECK(!normalizeUrl(std::string(3000, 'a')).ok && !normalizeUrl(std::string("example.com/") + char(1)).ok);
  CHECK(hostOf("https://Example.COM:8443/x") == "example.com" && hostOf("example.org/p") == "example.org" && hostOf("javascript:1").empty());
}

namespace {
struct FakeEngine : fai::IBrowserEngine {
  const char* name() const override { return "fake"; }
  bool simulated() const override { return true; }
  int create(const std::string& url) override {
    if (failNext) { failNext = false; return -1; }
    mem[next] = memFor.count(url) ? memFor[url] : 20000;
    live++;
    return next++;
  }
  void destroy(int h) override { mem.erase(h); live--; destroyed++; }
  long memoryKb(int h) override { return mem.at(h); }
  std::map<int, long> mem;
  std::map<std::string, long> memFor;
  int next = 1, live = 0, destroyed = 0;
  bool failNext = false;
};
}  // namespace

static void test_tab_manager() {
  FakeEngine e;
  long long now = 0;
  BrowserBudget b;
  b.maxTabs = 4; b.maxActiveTabs = 2; b.maxTotalKb = 50000; b.maxTabKb = 40000;
  TabManager t(e, b, [&] { return now; });
  int a = 0, c = 0, d = 0;
  std::string err;
  CHECK(t.open("javascript:alert(1)", nullptr, &err) == OpenResult::BadUrl && !err.empty() && t.tabs().empty() && e.live == 0);  // refused before any renderer exists
  now = 1; CHECK(t.open("a.com", &a) == OpenResult::Ok && t.foreground() == a && e.live == 1);
  now = 2; CHECK(t.open("b.com", &c) == OpenResult::Ok && t.foreground() == c && e.live == 2);
  now = 3; CHECK(t.open("c.com", &d) == OpenResult::Ok);
  CHECK(e.live == 2 && t.tabs().size() == 3);                                   // never more than maxActiveTabs live renderers
  CHECK(t.tabs()[0].state == TabState::Suspended && t.tabs()[0].url == "https://a.com" && t.tabs()[1].state == TabState::Active && t.tabs()[2].state == TabState::Active);  // LRU went first, URL kept
  now = 4; CHECK(t.switchTo(a) && t.foreground() == a && e.live == 2 && t.tabs()[0].state == TabState::Active);   // resume frees a slot first
  CHECK(t.tabs()[1].state == TabState::Suspended && t.tabs()[2].state == TabState::Active);                        // b was the least recently used
  CHECK(!t.switchTo(999));

  CHECK(t.open("e.com") == OpenResult::Ok && t.open("f.com") == OpenResult::TooManyTabs && t.tabs().size() == 4);   // maxTabs
  CHECK(t.close(a) == true && t.tabs().size() == 3 && !t.close(a));                                                // close destroys the renderer
  int live = e.live;
  for (auto& x : t.tabs()) if (x.state == TabState::Active) live--;
  CHECK(live == 0);                                                                                                // engine and manager agree on what is alive
  CHECK(t.foreground() == t.tabs().back().id);

  e.failNext = true;
  CHECK(t.open("g.com", nullptr, &err) == OpenResult::EngineFailed && t.tabs().size() == 3 && !err.empty());   // a failed create adds no tab
}

static void test_tab_budgets() {
  FakeEngine e;
  long long now = 0;
  BrowserBudget b;
  b.maxTabs = 4; b.maxActiveTabs = 3; b.maxTotalKb = 50000; b.maxTabKb = 40000;
  TabManager t(e, b, [&] { return now; });
  int a, c, d;
  now = 1; t.open("a.com", &a);
  now = 2; t.open("b.com", &c);
  CHECK(t.totalKb() == 40000);
  now = 3; t.open("c.com", &d);                                    // 60000 > 50000: the least recently used background tab is suspended
  CHECK(e.live == 2 && t.tabs()[0].state == TabState::Suspended && t.tabs()[2].state == TabState::Active && t.totalKb() <= 50000);
  e.mem[t.tabs()[2].handle] = 45000;                               // the foreground tab grows past maxTabKb: discarded, not allowed to starve the device
  e.mem[t.tabs()[1].handle] = 20000;
  t.enforce();
  CHECK(t.discarded() == 1 && t.tabs()[2].state == TabState::Suspended && t.tabs()[2].url == "https://c.com");
  CHECK(e.live == 1);

  FakeEngine e2;
  e2.memFor["https://heavy.com"] = 90000;                          // a page that is too big on its own
  TabManager t2(e2, b, [&] { return now; });
  int h;
  CHECK(t2.open("heavy.com", &h) == OpenResult::Ok);
  CHECK(t2.tabs()[0].state == TabState::Suspended && t2.discarded() == 1 && e2.live == 0);   // refused to keep it alive
}
