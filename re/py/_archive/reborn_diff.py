"""Compare vanilla Sacred.exe (or our decrypted splice) against SacredReborn.exe.

We want to know:
  1. Section layout — did they strip .bind / .rsrc / .text encryption?
  2. Imports — did they add MSVCR or something to call new code?
  3. Image base / entry point.
  4. Code overlap — is their .text just our decrypted .text + small patches,
     or did they relocate?
  5. Quick string diff: any new SR_HD_*, SacredReborn-specific tokens.
"""
import struct, os, sys
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

A = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"
B = r"C:\Users\bssth\Downloads\SacredReborn.exe"
GS = r"C:\Users\bssth\Downloads\SacredReborn Gameserver.exe"
CFG = r"C:\Users\bssth\Downloads\SacredReborn config.exe"

def parse_pe(path):
    """Minimal PE header parser. Returns dict with sections, entry, image base, imports."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"MZ":
        raise ValueError(f"{path}: not MZ")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew+4] != b"PE\x00\x00":
        raise ValueError(f"{path}: not PE")
    coff = e_lfanew + 4
    nsec, ts = struct.unpack_from("<HI", data, coff + 2)
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    assert magic == 0x10B, f"{path}: not PE32"
    ep_rva       = struct.unpack_from("<I", data, opt + 16)[0]
    image_base   = struct.unpack_from("<I", data, opt + 28)[0]
    sect_align   = struct.unpack_from("<I", data, opt + 32)[0]
    size_image   = struct.unpack_from("<I", data, opt + 56)[0]
    n_data_dirs  = struct.unpack_from("<I", data, opt + 92)[0]
    imp_rva, imp_sz = struct.unpack_from("<II", data, opt + 104)
    sec_table = opt + opt_size
    secs = []
    for i in range(nsec):
        off = sec_table + i * 40
        name = data[off:off+8].rstrip(b"\x00").decode("latin1", errors="replace")
        vsize, vaddr, rsize, raddr, _, _, _, char = struct.unpack_from(
            "<IIIIIIII", data, off + 8)
        secs.append({"name": name, "vaddr": vaddr, "vsize": vsize,
                     "raddr": raddr, "rsize": rsize, "char": char})

    # imports
    imports = []
    def rva_to_off(rva):
        for s in secs:
            if s["vaddr"] <= rva < s["vaddr"] + s["vsize"]:
                return s["raddr"] + (rva - s["vaddr"])
        return None
    if imp_rva:
        cur = rva_to_off(imp_rva)
        while cur:
            ilt, ts, fc, name_rva, iat = struct.unpack_from("<IIIII", data, cur)
            if not (ilt or name_rva or iat): break
            o = rva_to_off(name_rva)
            if o is None: break
            end = data.find(b"\x00", o)
            dll = data[o:end].decode("latin1", errors="replace")
            imports.append(dll)
            cur += 20

    return {
        "size":       len(data),
        "ep_rva":     ep_rva,
        "image_base": image_base,
        "size_image": size_image,
        "sections":   secs,
        "imports":    imports,
        "raw":        data,
    }

print("=== PE headers ===\n")
for lbl, p in [("VANILLA (decrypted splice)", A),
               ("REBORN exe",                 B),
               ("REBORN gameserver",          GS),
               ("REBORN config",              CFG)]:
    try:
        x = parse_pe(p)
    except Exception as e:
        print(f"-- {lbl}: parse failed: {e}\n")
        continue
    print(f"-- {lbl} --")
    print(f"   path        : {p}")
    print(f"   file size   : {x['size']:,}")
    print(f"   image base  : 0x{x['image_base']:08x}")
    print(f"   ep_rva      : 0x{x['ep_rva']:08x}")
    print(f"   size_image  : 0x{x['size_image']:08x}")
    print(f"   sections    :")
    for s in x["sections"]:
        print(f"     {s['name']:<8}  vaddr=0x{s['vaddr']:08x}  vsize=0x{s['vsize']:08x}  "
              f"raddr=0x{s['raddr']:08x}  rsize=0x{s['rsize']:08x}  char=0x{s['char']:08x}")
    print(f"   imports ({len(x['imports'])}):")
    for d in x["imports"]:
        print(f"     - {d}")
    print()
