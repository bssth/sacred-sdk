"""Syntax-check every Lua file the SDK ships.

A mod with a syntax error only shows up as a line in sdk_loaded.log after the
game has started, so the framework's own tree is checked here instead: one
`luac -p` per file, no execution.

    python packaging/check_lua.py custom/lua
    python packaging/check_lua.py custom/lua --luac /usr/bin/luac5.4

Needs a Lua 5.4 compiler on PATH (`luac5.4`, `luac`, or `lua5.4 -`). The SDK
embeds Lua 5.4, so 5.3 and older will reject integer division and bitwise
operators that are perfectly valid here.
"""
import argparse
import os
import shutil
import subprocess
import sys

CANDIDATES = ["luac5.4", "luac54", "luac", "luac.exe"]


def find_luac(explicit):
    if explicit:
        return explicit
    for c in CANDIDATES:
        p = shutil.which(c)
        if p:
            return p
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", default="custom/lua")
    ap.add_argument("--luac", help="path to luac (default: found on PATH)")
    args = ap.parse_args()

    luac = find_luac(args.luac)
    if not luac:
        print("no luac found on PATH (tried: %s)" % ", ".join(CANDIDATES))
        print("install Lua 5.4 -- on Ubuntu: sudo apt-get install -y lua5.4")
        return 2

    files = []
    for dirpath, dirnames, filenames in os.walk(args.root):
        dirnames[:] = [d for d in dirnames if d != "_vanilla"]   # decompiled dumps, huge
        for f in sorted(filenames):
            if f.endswith(".lua"):
                files.append(os.path.join(dirpath, f))
    if not files:
        print("no .lua files under %s" % args.root)
        return 1

    print("checking %d Lua file(s) under %s with %s" % (len(files), args.root, luac))
    bad = 0
    for path in files:
        r = subprocess.run([luac, "-p", path], capture_output=True, text=True)
        if r.returncode != 0:
            bad += 1
            msg = (r.stderr or r.stdout).strip().splitlines()
            print("  FAIL %s" % path)
            for line in msg[:3]:
                print("       " + line)
    if bad:
        print("%d file(s) failed to parse" % bad)
        return 1
    print("OK: all %d files parse" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
