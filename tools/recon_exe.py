"""Step-1 recon for Sacred.exe: sections, imports, packed-check, top strings."""
import pefile, re, sys, collections, os

P = r"E:\SteamLibrary\steamapps\common\Sacred Gold\Sacred.exe"
pe = pefile.PE(P, fast_load=False)

print("=== SECTIONS ===")
print(f"{'name':10} {'vaddr':>10} {'vsize':>10} {'rawsize':>10}  flags  entropy")
for s in pe.sections:
    name = s.Name.rstrip(b"\x00").decode("latin1", "replace")
    flags = []
    ch = s.Characteristics
    if ch & 0x20000000: flags.append("X")
    if ch & 0x40000000: flags.append("R")
    if ch & 0x80000000: flags.append("W")
    print(f"{name:10} {s.VirtualAddress:#010x} {s.Misc_VirtualSize:#010x} {s.SizeOfRawData:#010x}  {''.join(flags):4}  {s.get_entropy():.2f}")

print(f"\nEP RVA: {pe.OPTIONAL_HEADER.AddressOfEntryPoint:#x}")
print(f"ImageBase: {pe.OPTIONAL_HEADER.ImageBase:#x}")
print(f"Linker: {pe.OPTIONAL_HEADER.MajorLinkerVersion}.{pe.OPTIONAL_HEADER.MinorLinkerVersion}")
print(f"Subsystem: {pe.OPTIONAL_HEADER.Subsystem}  (3=CUI, 2=GUI)")

ts = pe.FILE_HEADER.TimeDateStamp
import datetime
print(f"TimeDateStamp: {ts:#x} = {datetime.datetime.utcfromtimestamp(ts).isoformat()}Z")

print("\n=== IMPORTS ===")
if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        dll = entry.dll.decode("latin1")
        funcs = [(imp.name.decode("latin1") if imp.name else f"ord{imp.ordinal}") for imp in entry.imports]
        print(f"\n  {dll}  ({len(funcs)} fns)")
        # show a sample, biased toward interesting ones
        interesting_kw = re.compile(r"(File|Read|Write|Create|Open|Find|Map|Heap|Process|Thread|Module|Library|Proc|Dlg|Window|Direct|Sound|Net|Socket|XML|granny|ijl|miles|mss|tincat)", re.I)
        hi = [f for f in funcs if interesting_kw.search(f)]
        sample = hi[:25] + ["..."] if len(hi) > 25 else hi
        for f in sample: print(f"    {f}")

print("\n=== STRINGS (filtered) ===")
data = open(P, "rb").read()
strs = re.findall(rb"[\x20-\x7e]{6,}", data)
# split into buckets
file_like = set()
dll_like = set()
debug_like = set()
url_like = set()
config_like = set()
fmt_like = set()
for raw in strs:
    s = raw.decode("latin1")
    sl = s.lower()
    if re.search(r"\.(bin|pak|cfg|ini|xml|res|dll|exe|txt|wav|mp3|bmp|tga|dds|gr2|grn)\b", sl):
        if sl.endswith(".dll"): dll_like.add(s)
        else: file_like.add(s)
    if "%" in s and re.search(r"%[0-9.\-+# ]*[sdiouxXfFeEgGcp]", s) and len(s) < 120:
        fmt_like.add(s)
    if re.search(r"(error|fail|assert|warn|debug|cannot|invalid|missing|unknown)", sl) and len(s) < 120:
        debug_like.add(s)
    if "://" in sl or "www." in sl or ".com" in sl:
        url_like.add(s)
    if re.search(r"^[A-Z][A-Z0-9_]{4,}$", s) and len(s) < 50:
        config_like.add(s)

def dump(title, st, n=40):
    print(f"\n-- {title} ({len(st)}) --")
    for x in sorted(st)[:n]:
        print(f"   {x}")

dump("DLL refs", dll_like)
dump("File refs", file_like, 80)
dump("Config/enum-like", config_like, 60)
dump("printf-format", fmt_like, 30)
dump("debug/error", debug_like, 40)
dump("urls", url_like)
