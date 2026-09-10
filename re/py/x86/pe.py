"""Minimal PE loader + capstone disassembly helpers for Sacred_decrypted.exe."""
import struct, sys
from capstone import *

EXE = r"E:\SteamLibrary\steamapps\common\Sacred Gold\sdk\Sacred_decrypted.exe"

class PE:
    def __init__(self, path=EXE):
        self.data = open(path, "rb").read()
        d = self.data
        e_lfanew = struct.unpack_from("<I", d, 0x3c)[0]
        assert d[e_lfanew:e_lfanew+4] == b"PE\0\0"
        coff = e_lfanew + 4
        nsec = struct.unpack_from("<H", d, coff+2)[0]
        optsz = struct.unpack_from("<H", d, coff+16)[0]
        opt = coff + 20
        self.imagebase = struct.unpack_from("<I", d, opt+28)[0]
        self.entry = struct.unpack_from("<I", d, opt+16)[0] + self.imagebase
        self.sections = []
        sh = opt + optsz
        for i in range(nsec):
            o = sh + i*40
            name = d[o:o+8].rstrip(b"\0").decode("latin1")
            vsize, va, rawsz, rawoff = struct.unpack_from("<IIII", d, o+8)
            chars = struct.unpack_from("<I", d, o+36)[0]
            self.sections.append(dict(name=name, va=va+self.imagebase, vsize=vsize,
                                      rawoff=rawoff, rawsz=rawsz, chars=chars))
    def sec(self, name):
        for s in self.sections:
            if s["name"] == name: return s
        return None
    def va2off(self, va):
        for s in self.sections:
            if s["va"] <= va < s["va"] + max(s["vsize"], s["rawsz"]):
                o = va - s["va"]
                if o < s["rawsz"]: return s["rawoff"] + o
        return None
    def read(self, va, n):
        o = self.va2off(va)
        if o is None: return None
        return self.data[o:o+n]
    def text(self):
        s = self.sec(".text")
        return s["va"], self.data[s["rawoff"]:s["rawoff"]+s["rawsz"]]

def md():
    m = Cs(CS_ARCH_X86, CS_MODE_32)
    m.detail = True
    return m

if __name__ == "__main__":
    p = PE()
    print("imagebase %08x entry %08x" % (p.imagebase, p.entry))
    for s in p.sections:
        print("%-10s va=%08x vsz=%08x raw=%08x rawsz=%08x chars=%08x" %
              (s["name"], s["va"], s["vsize"], s["rawoff"], s["rawsz"], s["chars"]))
