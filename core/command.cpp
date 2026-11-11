#include "command.h"

#ifdef __linux__
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <csignal>
#endif

namespace fai {

std::string Command::describe() const {
  std::string o;
  for (auto& a : argv) o += (o.empty() ? "" : " ") + a;
  return o;
}

#ifdef __linux__
CommandResult PosixRunner::run(const Command& c) {
  CommandResult r;
  if (c.argv.empty()) return r;
  int in[2], out[2], err[2];
  if (pipe(in) || pipe(out) || pipe(err)) return r;
  pid_t pid = fork();
  if (pid < 0) return r;
  if (pid == 0) {  // child
    dup2(in[0], 0); dup2(out[1], 1); dup2(err[1], 2);
    for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1]}) close(fd);
    std::vector<char*> args;
    for (auto& a : c.argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
  }
  close(in[0]); close(out[1]); close(err[1]);
  signal(SIGPIPE, SIG_IGN);
  // Write stdin, then drain stdout/stderr. Small payloads only (configs, rulesets), so a simple loop is enough.
  size_t off = 0;
  if (c.stdinData.empty()) close(in[1]), in[1] = -1;
  bool outOpen = true, errOpen = true;
  while (outOpen || errOpen || in[1] >= 0) {
    pollfd fds[3] = {{out[0], POLLIN, 0}, {err[0], POLLIN, 0}, {in[1], POLLOUT, 0}};
    if (!outOpen) fds[0].fd = -1;
    if (!errOpen) fds[1].fd = -1;
    if (in[1] < 0) fds[2].fd = -1;
    if (poll(fds, 3, 10000) <= 0) break;  // 10 s without progress: give up
    char buf[4096];
    if (fds[0].revents & (POLLIN | POLLHUP)) { ssize_t n = read(out[0], buf, sizeof buf); if (n > 0) r.out.append(buf, size_t(n)); else outOpen = false; }
    if (fds[1].revents & (POLLIN | POLLHUP)) { ssize_t n = read(err[0], buf, sizeof buf); if (n > 0) r.err.append(buf, size_t(n)); else errOpen = false; }
    if (in[1] >= 0 && (fds[2].revents & (POLLOUT | POLLERR))) {
      ssize_t n = write(in[1], c.stdinData.data() + off, c.stdinData.size() - off);
      if (n > 0) off += size_t(n);
      if (n <= 0 || off >= c.stdinData.size()) { close(in[1]); in[1] = -1; }
    }
  }
  if (in[1] >= 0) close(in[1]);
  close(out[0]); close(err[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  r.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return r;
}
#else
CommandResult PosixRunner::run(const Command&) { return CommandResult(); }
#endif

}  // namespace fai
