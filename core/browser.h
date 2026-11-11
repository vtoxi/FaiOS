// Fai Browser core: URL policy, the engine abstraction, and a tab manager that enforces resource budgets.
// No web engine is linked yet (see docs/BROWSER.md). Everything here is engine-independent, so a WPE WebKit, Servo or
// other backend can be added behind IBrowserEngine without touching the UI or the policy.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace fai {

// Only http and https are ever loaded. javascript:, data:, file:, ftp:, blob: and unknown schemes are refused, as are
// URLs with embedded credentials (user:pass@host, a classic spoofing trick) and control characters.
struct UrlResult { bool ok = false; std::string url, error; };
UrlResult normalizeUrl(const std::string& input);  // "example.com/a" -> "https://example.com/a"
std::string hostOf(const std::string& url);        // "" if not an http(s) URL

struct BrowserBudget {
  int maxTabs = 4;               // open tabs, active or suspended
  long maxActiveTabs = 2;        // tabs that may hold a live renderer at once
  long maxTotalKb = 150 * 1024;  // all live renderers together
  long maxTabKb = 100 * 1024;    // one tab; above this it is discarded, never allowed to grow
};

class IBrowserEngine {  // one live renderer per tab; implementations wrap WPE WebKit etc.
 public:
  virtual ~IBrowserEngine() = default;
  virtual const char* name() const = 0;
  virtual bool simulated() const = 0;
  virtual int create(const std::string& url) = 0;  // returns a renderer handle, or < 0 on failure
  virtual void destroy(int handle) = 0;
  virtual long memoryKb(int handle) = 0;           // resident memory of that renderer
};

enum class TabState { Active, Suspended };  // Suspended: renderer destroyed, URL kept, reloaded on resume
struct Tab { int id; std::string url; TabState state; int handle; long long lastUsedMs; };

enum class OpenResult { Ok, BadUrl, TooManyTabs, EngineFailed };

class TabManager {
 public:
  TabManager(IBrowserEngine& e, BrowserBudget b, std::function<long long()> nowMs) : e_(e), b_(b), now_(std::move(nowMs)) {}
  OpenResult open(const std::string& input, int* idOut = nullptr, std::string* err = nullptr);  // new tab becomes foreground
  bool close(int id);
  bool switchTo(int id);  // resumes a suspended tab, then enforces the budget (may suspend others)
  void enforce();         // call periodically: suspends least-recently-used background tabs, discards oversized ones
  int foreground() const { return fg_; }
  const std::vector<Tab>& tabs() const { return tabs_; }
  long totalKb();
  int discarded() const { return discarded_; }  // tabs dropped for exceeding maxTabKb (their URL stays, state Suspended)
 private:
  Tab* find(int id);
  void suspend(Tab& t);
  bool resume(Tab& t);
  IBrowserEngine& e_;
  BrowserBudget b_;
  std::function<long long()> now_;
  std::vector<Tab> tabs_;
  int fg_ = -1, nextId_ = 1, discarded_ = 0;
};

}  // namespace fai
