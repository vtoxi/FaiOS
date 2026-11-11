// Running system commands WITHOUT a shell. Arguments are an argv vector, so nothing is ever interpreted by sh.
// Secrets travel on stdin only (never argv, which other processes can read, and never logs).
#pragma once
#include <string>
#include <vector>

namespace fai {

struct Command {
  std::vector<std::string> argv;
  std::string stdinData;       // may hold secrets; never printed
  bool secretStdin = false;    // stdin carries a secret: the tool's output must not be reported either (it may echo it)
  bool ignoreFailure = false;  // e.g. cleanup of something that may not exist
  std::string describe() const;  // argv joined by spaces; deliberately omits stdinData
};

struct CommandResult {
  int exitCode = -1;  // -1 = could not start
  std::string out, err;
  bool ok() const { return exitCode == 0; }
};

class ICommandRunner {
 public:
  virtual ~ICommandRunner() = default;
  virtual CommandResult run(const Command& c) = 0;
};

class PosixRunner : public ICommandRunner {  // fork/exec, Linux. Needs root for ip/wg/nft. NOT yet exercised against a real tunnel.
 public:
  CommandResult run(const Command& c) override;
};

}  // namespace fai
