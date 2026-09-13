"""Build the release archive: everything a player drops into their game folder.

    python packaging/make_release.py --version v0.2.0 --dll Release/ijl15.dll

Produces `dist/SacredSDK-<version>.zip` laid out exactly as it must be extracted:

    install.cmd  uninstall.cmd  README.txt  LICENSE
    MODDING_GUIDE.md  MODDING_COOKBOOK.md  sdk.ini.example  VERSION.txt
    sdk\\ijl15.dll              <- install.cmd puts this in place as ijl15.dll
    sdk\\custom\\lua\\lib\\       <- the framework
    sdk\\custom\\lua\\examples\\  <- starter mods, inert until copied
    sdk\\docs\\                  <- the wiki pages, if they were checked out

WHAT IS DELIBERATELY NOT IN THE ARCHIVE
  * `ijl15_real.dll` -- that is the game's own Intel JPEG library. install.cmd
    renames the player's copy instead, so the archive ships no game files.
  * `custom/lua/_vanilla/` -- decompiled vanilla scripts, same reason.
  * anything generated (`custom/bin`, `custom/scripts`): the SDK rebuilds those
    on the first launch.
"""
import argparse
import os
import shutil
import subprocess
import sys
import zipfile
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)          # the repo root


def copy_into(stage, rel_src, rel_dst=None, optional=False):
    src = os.path.join(ROOT, rel_src)
    dst = os.path.join(stage, rel_dst or rel_src)
    if not os.path.exists(src):
        if optional:
            print("  (skipped, not present: %s)" % rel_src)
            return False
        raise SystemExit("missing: %s" % src)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.isdir(src):
        shutil.copytree(src, dst, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("_vanilla", "*.bin", "__pycache__", ".git*"))
    else:
        shutil.copy2(src, dst)
    print("  + %s" % (rel_dst or rel_src))
    return True


def git(*args):
    try:
        return subprocess.run(["git", "-C", ROOT] + list(args), capture_output=True,
                              text=True, check=True).stdout.strip()
    except Exception:
        return "unknown"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True, help="e.g. v0.2.0")
    ap.add_argument("--dll", default=os.path.join("Release", "ijl15.dll"))
    ap.add_argument("--out", default="dist")
    ap.add_argument("--docs", default="wiki", help="directory of .md pages to ship as sdk/docs")
    args = ap.parse_args()

    version = args.version
    name = "SacredSDK-%s" % version
    out_dir = os.path.join(ROOT, args.out)
    stage = os.path.join(out_dir, name)
    if os.path.exists(stage):
        shutil.rmtree(stage)
    os.makedirs(stage, exist_ok=True)
    print("staging %s" % stage)

    # the DLL, under sdk/ so extracting the archive can never overwrite the
    # game's own ijl15.dll before install.cmd has renamed it
    dll = args.dll if os.path.isabs(args.dll) else os.path.join(ROOT, args.dll)
    if not os.path.isfile(dll):
        raise SystemExit("no DLL at %s -- build it first" % dll)
    os.makedirs(os.path.join(stage, "sdk"), exist_ok=True)
    shutil.copy2(dll, os.path.join(stage, "sdk", "ijl15.dll"))
    print("  + sdk/ijl15.dll (%d bytes)" % os.path.getsize(dll))

    # the framework
    copy_into(stage, os.path.join("custom", "lua", "lib"), os.path.join("sdk", "custom", "lua", "lib"))
    copy_into(stage, os.path.join("custom", "lua", "examples"), os.path.join("sdk", "custom", "lua", "examples"))
    copy_into(stage, os.path.join("custom", "README.md"), os.path.join("sdk", "custom", "README.md"))

    # top-level files the player actually opens
    copy_into(stage, os.path.join("packaging", "install.cmd"), "install.cmd")
    copy_into(stage, os.path.join("packaging", "uninstall.cmd"), "uninstall.cmd")
    copy_into(stage, os.path.join("packaging", "README.txt"), "README.txt")
    copy_into(stage, "LICENSE", "LICENSE")
    copy_into(stage, "MODDING_GUIDE.md", "MODDING_GUIDE.md", optional=True)
    copy_into(stage, "MODDING_COOKBOOK.md", "MODDING_COOKBOOK.md", optional=True)
    copy_into(stage, "sdk.ini.example", "sdk.ini.example")

    # the wiki, if it was checked out next to us
    docs = args.docs if os.path.isabs(args.docs) else os.path.join(ROOT, args.docs)
    if os.path.isdir(docs):
        dst = os.path.join(stage, "sdk", "docs")
        os.makedirs(dst, exist_ok=True)
        n = 0
        for f in sorted(os.listdir(docs)):
            if f.endswith(".md"):
                shutil.copy2(os.path.join(docs, f), os.path.join(dst, f))
                n += 1
        print("  + sdk/docs/ (%d pages)" % n)
    else:
        print("  (no docs directory at %s -- the archive ships without sdk/docs)" % docs)

    with open(os.path.join(stage, "VERSION.txt"), "w", encoding="utf-8") as f:
        f.write("SacredSDK %s\n" % version)
        f.write("commit  %s\n" % git("rev-parse", "--short", "HEAD"))
        f.write("built   %s\n" % datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"))
        f.write("target  Sacred Gold 2.0.2.28 (2006-10-13), Steam and GOG\n")
    print("  + VERSION.txt")

    zip_path = os.path.join(out_dir, name + ".zip")
    if os.path.exists(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dirpath, _, filenames in os.walk(stage):
            for f in filenames:
                full = os.path.join(dirpath, f)
                z.write(full, os.path.relpath(full, stage))
    size = os.path.getsize(zip_path)
    print("\n%s  (%.1f MB)" % (zip_path, size / 1024 / 1024))
    # so a workflow can pick it up without guessing the name
    gh_out = os.environ.get("GITHUB_OUTPUT")
    if gh_out:
        with open(gh_out, "a", encoding="utf-8") as f:
            f.write("zip=%s\n" % zip_path.replace(os.sep, "/"))
            f.write("name=%s\n" % (name + ".zip"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
