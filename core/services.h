// FaiOS core services: logging, storage, config, notifications, power, service lifecycle.
// No UI/OS-toolkit dependencies; hardware-backed versions replace the Sim* ones later.
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fai {

// ---- Logging -------------------------------------------------------------
enum class Level { Debug, Info, Warn, Error };
const char* levelName(Level l);
struct LogEntry { Level level; std::string tag, msg; };

class Logger {
 public:
  Level min = Level::Info;                        // entries below this are dropped
  std::function<void(const LogEntry&)> sink;      // optional (stderr, file, ...)
  explicit Logger(size_t capacity = 200) : cap_(capacity) {}
  void log(Level l, const std::string& tag, const std::string& msg);
  const std::deque<LogEntry>& entries() const { return ring_; }  // newest last, bounded
 private:
  size_t cap_;
  std::deque<LogEntry> ring_;
};

// ---- Storage -------------------------------------------------------------
// Keys are relative names ("config.ini", "data/x"). Absolute paths and ".." are rejected.
class IStorage {
 public:
  virtual ~IStorage() = default;
  virtual bool read(const std::string& key, std::string& out) = 0;
  virtual bool write(const std::string& key, const std::string& data) = 0;
  virtual bool exists(const std::string& key) = 0;
};

class MemStorage : public IStorage {
 public:
  bool read(const std::string& key, std::string& out) override;
  bool write(const std::string& key, const std::string& data) override;
  bool exists(const std::string& key) override { return m_.count(key) > 0; }
 private:
  std::map<std::string, std::string> m_;
};

class FileStorage : public IStorage {  // writes are atomic (temp file + rename)
 public:
  explicit FileStorage(std::string root) : root_(std::move(root)) {}
  bool read(const std::string& key, std::string& out) override;
  bool write(const std::string& key, const std::string& data) override;
  bool exists(const std::string& key) override;
 private:
  std::string root_;
};

// ---- Config (key=value, '#' comments) ---------------------------------------
class Config {
 public:
  explicit Config(IStorage& s, std::string name = "config.ini") : s_(s), name_(std::move(name)) {}
  bool load();  // false if file missing; keeps current values
  bool save();
  std::string getString(const std::string& k, const std::string& def = "") const;
  int getInt(const std::string& k, int def) const;
  bool getBool(const std::string& k, bool def) const;
  void setString(const std::string& k, const std::string& v);
  void setInt(const std::string& k, int v) { setString(k, std::to_string(v)); }
  void setBool(const std::string& k, bool v) { setString(k, v ? "true" : "false"); }
 private:
  IStorage& s_;
  std::string name_;
  std::map<std::string, std::string> kv_;
};

// ---- Notifications ---------------------------------------------------------
struct Notification { int id; std::string title, body; };

class Notifications {
 public:
  int add(const std::string& title, const std::string& body);  // returns id
  bool dismiss(int id);
  void clear() { list_.clear(); }
  const std::vector<Notification>& list() const { return list_; }
  size_t count() const { return list_.size(); }
 private:
  std::vector<Notification> list_;
  int next_ = 1;
};

// ---- Power -----------------------------------------------------------------
class IPower {
 public:
  virtual ~IPower() = default;
  virtual int batteryPercent() const = 0;
  virtual bool charging() const = 0;
  virtual int brightness() const = 0;  // 0..100
  virtual void setBrightness(int pct) = 0;
};

class SimPower : public IPower {  // SIMULATED values
 public:
  int batteryPercent() const override { return 78; }
  bool charging() const override { return false; }
  int brightness() const override { return b_; }
  void setBrightness(int pct) override { b_ = pct < 0 ? 0 : pct > 100 ? 100 : pct; }
 private:
  int b_ = 60;
};

// ---- Service lifecycle -----------------------------------------------------
class Service {
 public:
  virtual ~Service() = default;
  virtual const char* name() const = 0;
  virtual bool start() = 0;
  virtual void stop() = 0;
};

class FnService : public Service {  // service from lambdas
 public:
  FnService(std::string n, std::function<bool()> start, std::function<void()> stop = {})
      : n_(std::move(n)), start_(std::move(start)), stop_(std::move(stop)) {}
  const char* name() const override { return n_.c_str(); }
  bool start() override { return start_(); }
  void stop() override { if (stop_) stop_(); }
 private:
  std::string n_;
  std::function<bool()> start_;
  std::function<void()> stop_;
};

class ServiceManager {
 public:
  explicit ServiceManager(Logger& log) : log_(log) {}
  void add(std::unique_ptr<Service> s);
  bool startAll();  // in add order; on failure stops the already-started ones and returns false
  void stopAll();   // reverse order
  bool running(const std::string& name) const;
 private:
  struct Entry { std::unique_ptr<Service> svc; bool up = false; };
  Logger& log_;
  std::vector<Entry> svcs_;
};

// ---- System status snapshot (what the status bar needs) -----------------------
struct SystemStatus {
  int signal = 0, battery = 0, brightness = 0, badge = 0;  // badge = unread messages + notifications
  bool charging = false, wifi = false, bluetooth = false;
};

}  // namespace fai
