"""Corpus configuration for the Sacred FunkCode toolchain.

Before this module every tool hardcoded

    BIN         = <root>/bin
    ALL_CLASSES = the 8 bin/TYPE_NPC_* directories

which covered ONLY the base campaign's single-player scripts. The real corpus
is four groups of script directories:

    <root>/bin/TYPE_NPC_<CLASS>/          base campaign, one dir per class   (8)
    <root>/bin/NetScript{,Camp}/          base campaign multiplayer scripts  (2)
    <root>/bin/Addon/TYPE_NPC_<CLASS>/    Underworld addon, per class        (8)
    <root>/bin/Addon/NetScript{,Camp}/    Underworld addon multiplayer       (2)

Every directory holds the same five blobs: FunkCode.bin, StartCode.bin,
QuestCode.bin, QuestPoolCode.bin, DefPos.bin, Vectoren.bin.

A "source" here is one such directory, addressed by a stable key:

    base:SERAPHIM  base:VAMPIRELADY  ...  base:NetScript  base:NetScriptCamp
    addon:SERAPHIM addon:VAMPIRELADY ...  addon:NetScript addon:NetScriptCamp

MEASURED FACTS about the corpus (md5 of FunkCode.bin, 2026-09-10, Steam
2.0.2.28 install; reproduce with `python funkcode_sources.py --md5`):

  * all EIGHT addon class dirs are BYTE-IDENTICAL
    (d02c20b1e7bfb9053e203dae9dbc2ba9, 2 504 248 B) and addon:NetScriptCamp is
    the same file again  =>  the Underworld addon has NO class-specific
    FunkCode at all.
  * base:NetScriptCamp is byte-identical to base:GLADIATOR
    (53b9d77922469e539e0d50373f29e53e, 3 967 103 B).
  * <root>/bin/sgf.bin is a copy of the addon FunkCode blob (same md5) — it is
    the runtime cache the engine writes, not a separate script source.
  * the 8 base class dirs are all different from each other (per-class
    prologue / class quests), which is what makes the class-diff pass
    in quest_index.py necessary.

Backwards compatibility: `BIN` and `ALL_CLASSES` keep their old meaning and
old values, so quest_dump.py / quest_script.py / quest_inventory.py behave
exactly as before when no source spec is given.
"""
import os, sys, hashlib, argparse, collections

# ---------------------------------------------------------------- roots ----
# <root>/sdk/re/py/funkcode_sources.py  ->  <root>
_HERE = os.path.dirname(os.path.abspath(__file__))
_DERIVED_ROOT = os.path.abspath(os.path.join(_HERE, "..", "..", ".."))

GAME_ROOT = os.environ.get("SACRED_ROOT") or _DERIVED_ROOT
if not os.path.isdir(os.path.join(GAME_ROOT, "bin")):
    # last-resort fallback: the historical hardcoded path
    GAME_ROOT = r"E:\SteamLibrary\steamapps\common\Sacred Gold"

BIN_ROOT = os.path.join(GAME_ROOT, "bin")
ADDON_ROOT = os.path.join(BIN_ROOT, "Addon")

# legacy names (do not rename — other scripts import these)
BIN = BIN_ROOT

CLASS_NAMES = ["SERAPHIM", "GLADIATOR", "MAGICIAN", "ELVE",
               "DARKELVE", "DAEMONIN", "VAMPIRELADY", "ZWERG"]
CLASS_DIRS = ["TYPE_NPC_" + c for c in CLASS_NAMES]
ALL_CLASSES = list(CLASS_DIRS)          # legacy name, legacy value
NET_DIRS = ["NetScript", "NetScriptCamp"]

# the class whose records are the reference set for every diff
BASELINE = "base:VAMPIRELADY"
ADDON_BASELINE = "addon:SERAPHIM"       # all addon class dirs are identical

SCRIPT_FILES = ["FunkCode.bin", "StartCode.bin", "QuestCode.bin",
                "QuestPoolCode.bin", "DefPos.bin", "Vectoren.bin"]


class Source(object):
    """One script directory in the corpus."""
    __slots__ = ("key", "campaign", "kind", "cls", "path")

    def __init__(self, key, campaign, kind, cls, path):
        self.key = key            # 'base:VAMPIRELADY'
        self.campaign = campaign  # 'base' | 'addon'
        self.kind = kind          # 'class' | 'net'
        self.cls = cls            # 'VAMPIRELADY' | 'NetScript' | 'NetScriptCamp'
        self.path = path          # absolute directory

    # legacy-friendly: the TYPE_NPC_* dir name for class sources, else the dir
    @property
    def legacy_name(self):
        return "TYPE_NPC_" + self.cls if self.kind == "class" else self.cls

    @property
    def short(self):
        return ("b:" if self.campaign == "base" else "a:") + self.cls

    def file(self, name="FunkCode.bin"):
        return os.path.join(self.path, name)

    def exists(self, name="FunkCode.bin"):
        return os.path.isfile(self.file(name))

    def read(self, name="FunkCode.bin"):
        with open(self.file(name), "rb") as fh:
            return fh.read()

    def __repr__(self):
        return "<Source %s %s>" % (self.key, self.path)


def _build_sources():
    out = collections.OrderedDict()
    for c in CLASS_NAMES:
        out["base:" + c] = Source("base:" + c, "base", "class", c,
                                  os.path.join(BIN_ROOT, "TYPE_NPC_" + c))
    for n in NET_DIRS:
        out["base:" + n] = Source("base:" + n, "base", "net", n,
                                  os.path.join(BIN_ROOT, n))
    for c in CLASS_NAMES:
        out["addon:" + c] = Source("addon:" + c, "addon", "class", c,
                                   os.path.join(ADDON_ROOT, "TYPE_NPC_" + c))
    for n in NET_DIRS:
        out["addon:" + n] = Source("addon:" + n, "addon", "net", n,
                                   os.path.join(ADDON_ROOT, n))
    return out


SOURCES = _build_sources()

# convenience groups -------------------------------------------------------
GROUPS = {
    "all":            list(SOURCES),
    "base":           [k for k, s in SOURCES.items() if s.campaign == "base"],
    "addon":          [k for k, s in SOURCES.items() if s.campaign == "addon"],
    "classes":        [k for k, s in SOURCES.items() if s.kind == "class"],
    "net":            [k for k, s in SOURCES.items() if s.kind == "net"],
    "base-classes":   ["base:" + c for c in CLASS_NAMES],
    "addon-classes":  ["addon:" + c for c in CLASS_NAMES],
    "base-net":       ["base:" + n for n in NET_DIRS],
    "addon-net":      ["addon:" + n for n in NET_DIRS],
    # the cheap corpus: one file per distinct FunkCode.bin content group
    # (all 8 addon class dirs are byte-identical, see module docstring)
    "canonical":      (["base:" + c for c in CLASS_NAMES]
                       + ["base:NetScript", "addon:SERAPHIM", "addon:NetScript"]),
}


def get(key):
    """Source for a key; accepts source keys, legacy TYPE_NPC_* dir names and
    bare class names (which mean the BASE campaign, as they always did)."""
    if key in SOURCES:
        return SOURCES[key]
    k = key.strip()
    if k.upper().startswith("TYPE_NPC_"):
        k = k[len("TYPE_NPC_"):]
    for cand in ("base:" + k.upper(), "base:" + k, "addon:" + k.upper(), "addon:" + k):
        if cand in SOURCES:
            return SOURCES[cand]
    raise KeyError("unknown source %r (known: %s)" % (key, ", ".join(SOURCES)))


def resolve(spec, default="base-classes"):
    """Turn a comma-separated spec into a list of Source objects.

    Accepted atoms: a group name ('all', 'base', 'addon', 'classes', 'net',
    'base-classes', 'addon-classes', 'base-net', 'addon-net', 'canonical'),
    a source key ('addon:NetScript'), a legacy class dir ('TYPE_NPC_ELVE') or
    a bare class name ('ELVE' == 'base:ELVE').  Empty spec -> `default`.
    Non-existent directories are dropped (the Addon is optional in some
    installs); order is preserved and duplicates removed.
    """
    if spec is None or (isinstance(spec, str) and not spec.strip()):
        spec = default
    if isinstance(spec, str):
        atoms = [a for a in (x.strip() for x in spec.split(",")) if a]
    else:
        atoms = list(spec)
    keys = []
    for a in atoms:
        if isinstance(a, Source):
            keys.append(a.key)
        elif a in GROUPS:
            keys.extend(GROUPS[a])
        else:
            keys.append(get(a).key)
    seen, out = set(), []
    for k in keys:
        if k in seen:
            continue
        seen.add(k)
        s = SOURCES[k]
        if s.exists():
            out.append(s)
    return out


def file_md5(source, name="FunkCode.bin"):
    s = source if isinstance(source, Source) else get(source)
    if not s.exists(name):
        return None
    h = hashlib.md5()
    with open(s.file(name), "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def content_groups(sources, name="FunkCode.bin"):
    """Group sources by identical file content -> OrderedDict md5 -> [keys]."""
    out = collections.OrderedDict()
    for s in sources:
        m = file_md5(s, name)
        out.setdefault(m, []).append(s.key)
    return out


def _main():
    ap = argparse.ArgumentParser(description="Sacred FunkCode corpus inspector")
    ap.add_argument("--sources", default="all", help="source spec (default: all)")
    ap.add_argument("--file", default="FunkCode.bin", help="blob to inspect")
    ap.add_argument("--md5", action="store_true", help="hash and group by content")
    args = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    srcs = resolve(args.sources, default="all")
    print("GAME_ROOT = %s" % GAME_ROOT)
    print("%-22s %-6s %-6s %12s  %s" % ("key", "camp", "kind", "size", args.file))
    for s in srcs:
        p = s.file(args.file)
        sz = os.path.getsize(p) if os.path.isfile(p) else -1
        print("%-22s %-6s %-6s %12s  %s" % (s.key, s.campaign, s.kind, sz, p))
    if args.md5:
        print("\n=== identical-content groups (%s) ===" % args.file)
        for m, keys in content_groups(srcs, args.file).items():
            print("  %s  %s" % (m, ", ".join(keys)))


if __name__ == "__main__":
    _main()
