"""From walker_dispatch_table.txt extract every unique FUN_XXXXXXXX address
that the outer walker dispatches to. Emit them as a hex list ready to feed
to DecompileFunc.java for batch decompilation."""
import re, sys, os
try: sys.stdout.reconfigure(encoding="utf-8")
except Exception: pass

TXT = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\logs\walker_dispatch_table.txt"
src = open(TXT, encoding="utf-8").read()
funs = sorted(set(re.findall(r"FUN_([0-9a-f]{8})", src)))
print(" ".join("0x" + f for f in funs))
