"""Quick structural peek at scripts/us/global.res — used by the in-game text resolver
to turn res:1024 / res:HQ_3_1_4_Log_Title into displayable strings.
"""
import os, struct, collections, re

P = r"E:\SteamLibrary\steamapps\common\Sacred Gold\scripts\us\global.res"
data = open(P, "rb").read()
print(f"size: {len(data)} bytes")
print(f"\nfirst 128 bytes hex:")
for i in range(0, 128, 16):
    h = " ".join(f"{x:02x}" for x in data[i:i+16])
    a = "".join(chr(x) if 32<=x<127 else "." for x in data[i:i+16])
    print(f"  {i:04x}  {h:<48}  {a}")

print(f"\nlast 64 bytes hex:")
for i in range(max(0, len(data)-64), len(data), 16):
    h = " ".join(f"{x:02x}" for x in data[i:i+16])
    a = "".join(chr(x) if 32<=x<127 else "." for x in data[i:i+16])
    print(f"  {i:08x}  {h:<48}  {a}")

# Byte histogram
hist = collections.Counter(data)
print(f"\nbyte histogram (top 10):")
for b, c in hist.most_common(10):
    print(f"  0x{b:02x} ({chr(b) if 32<=b<127 else '?'}) {c:>9}  ({c/len(data)*100:.1f}%)")
print(f"distinct byte values: {sum(1 for v in hist.values() if v>0)}/256")

# Strings — both ASCII and possibly wide (UTF-16)
ascii_runs = re.findall(rb"[\x20-\x7e]{6,}", data)
print(f"\nASCII runs (>=6 chars): {len(ascii_runs)}")
print(f"  first 15:")
for s in ascii_runs[:15]:
    off = data.find(s)
    print(f"    @ {off:#08x}  {s.decode('latin1')[:80]}")

# Check for UTF-16 LE strings (every other byte is 0)
utf16_test = data[:4096]
zeros_at_odd = sum(1 for i in range(1, len(utf16_test), 2) if utf16_test[i] == 0)
print(f"\nUTF-16 indicator: {zeros_at_odd}/{len(utf16_test)//2} odd-position zero bytes "
      f"({zeros_at_odd/(len(utf16_test)//2)*100:.0f}%) — high suggests UTF-16 LE")

# Try finding a known token like HQ_3_1_4_Log_Title as ASCII
needles = [b"HQ_3_1_4_Log_Title", b"NQ_Log_Qend", b"DQ_NUM", b"Belohnung_Whiskey",
           b"1024", b"17631"]
print(f"\nNeedle search (ASCII):")
for n in needles:
    pos = data.find(n)
    print(f"  '{n.decode()}' : {f'@{pos:#x}' if pos>=0 else 'NOT FOUND'}")

# Same as UTF-16 LE
print(f"\nNeedle search (UTF-16 LE):")
for n in needles:
    w = b"".join(bytes([c, 0]) for c in n)
    pos = data.find(w)
    print(f"  '{n.decode()}' : {f'@{pos:#x}' if pos>=0 else 'NOT FOUND'}")
