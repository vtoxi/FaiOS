#include "services.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fai {
namespace fs = std::filesystem;

const char* levelName(Level l) {
  static const char* const n[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  return n[int(l)];
}

void Logger::log(Level l, const std::string& tag, const std::string& msg) {
  if (l < min) return;
  LogEntry e{l, tag, msg};
  if (cap_ && ring_.size() >= cap_) ring_.pop_front();
  if (cap_) ring_.push_back(e);
  if (sink) sink(e);
}

static bool safeKey(const std::string& k) {
  return !k.empty() && k[0] != '/' && k[0] != '\\' && k.find("..") == std::string::npos && k.find(':') == std::string::npos;
}

bool MemStorage::read(const std::string& key, std::string& out) {
  auto it = m_.find(key);
  if (!safeKey(key) || it == m_.end()) return false;
  out = it->second;
  return true;
}
bool MemStorage::write(const std::string& key, const std::string& data) {
  if (!safeKey(key)) return false;
  m_[key] = data;
  return true;
}

bool FileStorage::read(const std::string& key, std::string& out) {
  if (!safeKey(key)) return false;
  std::ifstream f(fs::path(root_) / key, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool FileStorage::write(const std::string& key, const std::string& data) {
  if (!safeKey(key)) return false;
  std::error_code ec;
  fs::path dst = fs::path(root_) / key, tmp = dst;
  tmp += ".tmp";
  fs::create_directories(dst.parent_path(), ec);
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << data;
    if (!f.flush()) return false;
  }
  fs::rename(tmp, dst, ec);  // atomic replace on POSIX
  if (ec) { fs::remove(tmp, ec); return false; }
  return true;
}

bool FileStorage::exists(const std::string& key) {
  std::error_code ec;
  return safeKey(key) && fs::exists(fs::path(root_) / key, ec);
}

static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

bool Config::load() {
  std::string text;
  if (!s_.read(name_, text)) return false;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    line = trim(line);
    size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos || eq == 0) continue;
    kv_[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
  }
  return true;
}

bool Config::save() {
  std::string text = "# FaiOS config\n";
  for (auto& [k, v] : kv_) text += k + "=" + v + "\n";
  return s_.write(name_, text);
}

std::string Config::getString(const std::string& k, const std::string& def) const {
  auto it = kv_.find(k);
  return it == kv_.end() ? def : it->second;
}

int Config::getInt(const std::string& k, int def) const {
  auto it = kv_.find(k);
  if (it == kv_.end()) return def;
  char* end = nullptr;
  long v = std::strtol(it->second.c_str(), &end, 10);
  return (end == it->second.c_str() || *end) ? def : int(v);  // junk -> default
}

bool Config::getBool(const std::string& k, bool def) const {
  auto it = kv_.find(k);
  if (it == kv_.end()) return def;
  if (it->second == "true" || it->second == "1") return true;
  if (it->second == "false" || it->second == "0") return false;
  return def;
}

void Config::setString(const std::string& k, const std::string& v) {
  std::string key = k, val = v;
  std::replace(key.begin(), key.end(), '=', '_');  // keep the file parseable
  for (auto* s : {&key, &val}) std::replace_if(s->begin(), s->end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
  if (!key.empty()) kv_[key] = val;
}

int Notifications::add(const std::string& title, const std::string& body) {
  list_.push_back({next_, title, body});
  return next_++;
}

bool Notifications::dismiss(int id) {
  auto it = std::find_if(list_.begin(), list_.end(), [id](const Notification& n) { return n.id == id; });
  if (it == list_.end()) return false;
  list_.erase(it);
  return true;
}

void ServiceManager::add(std::unique_ptr<Service> s) { svcs_.push_back({std::move(s), false}); }

bool ServiceManager::startAll() {
  for (auto& e : svcs_) {
    if (e.up) continue;
    if (e.svc->start()) {
      e.up = true;
      log_.log(Level::Info, "svc", std::string("started ") + e.svc->name());
    } else {
      log_.log(Level::Error, "svc", std::string("failed to start ") + e.svc->name());
      stopAll();
      return false;
    }
  }
  return true;
}

void ServiceManager::stopAll() {
  for (auto it = svcs_.rbegin(); it != svcs_.rend(); ++it)
    if (it->up) {
      it->svc->stop();
      it->up = false;
      log_.log(Level::Info, "svc", std::string("stopped ") + it->svc->name());
    }
}

bool ServiceManager::running(const std::string& name) const {
  for (auto& e : svcs_)
    if (e.up && name == e.svc->name()) return true;
  return false;
}

}  // namespace fai
