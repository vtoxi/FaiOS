#!/usr/bin/env python3
"""Sanity check for the test suite: break production code on purpose and confirm the tests notice.

Each mutation edits one file, rebuilds test_core, runs it, then restores the file. A mutation the tests do NOT
catch means the tests are weaker than they look. Run inside Ubuntu:
    python3 build/scripts/mutation-check.py [build-dir]     (default /tmp/faios-out; must already be configured)
Exit code 0 = every mutation was caught.
"""
import os
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/faios-out"

MUTATIONS = [
    # (file, exact original text, replacement, what the mutation breaks)
    ("core/vpn.cpp", "bool verified = fresh && routed_;", "bool verified = fresh;", "claim protection without checking routing"),
    ("core/vpn.cpp", "const bool never = false;", "", None),  # placeholder removed below if absent
    ("core/vpn.cpp", "if (prof_.killSwitch) {  // fail closed", "if (false) {  // fail closed", "never install the kill switch"),
    ("core/vpn.cpp", "if (st_.trafficBlocked || (haveProf_ && prof_.killSwitch)) { std::string e; be_.setKillSwitch(nullptr, e); }", "", "never lift the kill switch on user disconnect"),
    ("core/vpn.cpp", "std::min(60000LL, 1000LL << shift)", "1000LL << shift", "remove the reconnect backoff cap"),
    ("core/vpn.cpp", "if (now > deadlineMs_)", "if (false)", None),
    ("core/vpn.cpp", "o.set(\"addresses\", cidrArray(p.cfg.addresses))", "o.set(\"pk\", Json::makeString(p.cfg.privateKey)).set(\"addresses\", cidrArray(p.cfg.addresses))", "write the private key into the plain profile file"),
    ("core/wireguard.cpp", "key == \"preup\" || key == \"postup\"", "key == \"preup_x\" || key == \"postup_x\"", "accept PreUp/PostUp script hooks"),
    ("core/wireguard.cpp", "err(n, \"PrivateKey is not a valid 32-byte base64 key\");", "err(n, \"PrivateKey is not valid: \" + val);", "echo a bad private key in the error text"),
    ("core/wg_commands.cpp", "hook output priority 0; policy drop;", "hook output priority 0; policy accept;", "kill switch default-accept instead of default-drop"),
    ("core/wg_commands.cpp", "set.stdinData = wgSetconf(c, endpointIps);", "set.argv.push_back(c.privateKey); set.stdinData = wgSetconf(c, endpointIps);", "put the private key on the command line"),
    ("core/wg_commands.cpp", "if (v6 ? full6 : full4) {", "if (false) {", "skip full-tunnel routing rules"),
    ("core/network.cpp", "if (vpnProtected && !vpnDns.empty()) return vpnDns;", "", "let local DNS win over a verified VPN"),
    ("core/ip.cpp", "if (digits == 1 && val == 0) return false;", "", "accept ambiguous leading-zero IPv4 octets"),
    ("core/vpn.cpp", "if (!c.secretStdin && !x.err.empty()) err += \": \" + firstLine(x.err);", "if (!x.err.empty()) err += \": \" + firstLine(x.err);", "report tool output for secret-carrying commands"),
]


def run(cmd):
    return subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)


def main():
    missed = 0
    for rel, old, new, why in MUTATIONS:
        if why is None:
            continue  # reserved slots
        path = os.path.join(REPO, rel)
        original = open(path, encoding="utf-8").read()
        if old not in original:
            print(f"?? text not found, mutation skipped: {why}")
            missed += 1
            continue
        try:
            open(path, "w", encoding="utf-8", newline="\n").write(original.replace(old, new, 1))
            if run(["cmake", "--build", OUT, "--target", "test_core"]).returncode != 0:
                print(f"?? build broke, mutation not meaningful: {why}")
                missed += 1
                continue
            last = run([os.path.join(OUT, "test_core")]).stdout.strip().splitlines()[-1:]
            line = last[0] if last else "(no output)"
            if "FAILED" in line:
                print(f"caught    : {why}  ->  {line}")
            else:
                print(f"NOT CAUGHT: {why}  ->  {line}")
                missed += 1
        finally:
            open(path, "w", encoding="utf-8", newline="\n").write(original)
    run(["cmake", "--build", OUT, "--target", "test_core"])
    final = run([os.path.join(OUT, "test_core")]).stdout.strip().splitlines()[-1:]
    print("restored; final run:", final[0] if final else "(no output)")
    return 1 if missed else 0


sys.exit(main())
