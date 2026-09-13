"""Smoke-test a built ijl15.dll before it is shipped.

Sacred loads the SDK through the `ijl15.dll` proxy slot, so the one thing that
absolutely must hold is that our DLL still *looks like* Intel's JPEG library:
32-bit, a DLL, and exporting the six `ijl*` entry points, each forwarded to
`ijl15_real`. A linker setting or an edit to proxy.def can quietly break that,
and the symptom for a player is a game that refuses to start.

    python packaging/check_dll.py Release/ijl15.dll

Pure Python, no dumpbin, so it runs anywhere the build does.
"""
import struct
import sys

# The exports Sacred's import table asks for, and where each must forward.
REQUIRED = ["ijlGetLibVersion", "ijlInit", "ijlFree", "ijlRead", "ijlWrite", "ijlErrorStr"]
FORWARD_TO = "ijl15_real."
MIN_SIZE = 200 * 1024
MAX_SIZE = 16 * 1024 * 1024


class PE:
    def __init__(self, data):
        self.d = data
        if data[:2] != b"MZ":
            raise ValueError("not a PE file (no MZ)")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("not a PE file (no PE signature)")
        self.machine, nsec, _, _, _, opt_size, self.characteristics = \
            struct.unpack_from("<HHIIIHH", data, pe + 4)
        opt = pe + 24
        self.magic = struct.unpack_from("<H", data, opt)[0]
        ndirs = struct.unpack_from("<I", data, opt + (92 if self.magic == 0x10B else 108))[0]
        dirs = opt + (96 if self.magic == 0x10B else 112)
        self.dirs = [struct.unpack_from("<II", data, dirs + 8 * i) for i in range(ndirs)]
        sec = opt + opt_size
        self.sections = []
        for i in range(nsec):
            name, vsize, vaddr, rsize, raddr = struct.unpack_from("<8sIIII", data, sec + 40 * i)
            self.sections.append((vaddr, max(vsize, rsize), raddr))

    def off(self, rva):
        for vaddr, vsize, raddr in self.sections:
            if vaddr <= rva < vaddr + vsize:
                return raddr + (rva - vaddr)
        raise ValueError("rva %#x is in no section" % rva)

    def zstr(self, rva):
        o = self.off(rva)
        end = self.d.index(b"\0", o)
        return self.d[o:end].decode("latin1")

    def exports(self):
        """name -> forwarder string or None."""
        erva, esize = self.dirs[0]
        if not erva:
            return {}
        e = self.off(erva)
        nnames = struct.unpack_from("<I", self.d, e + 24)[0]
        afuncs, anames, aords = struct.unpack_from("<III", self.d, e + 28)
        out = {}
        for i in range(nnames):
            nrva = struct.unpack_from("<I", self.d, self.off(anames) + 4 * i)[0]
            name = self.zstr(nrva)
            ordinal = struct.unpack_from("<H", self.d, self.off(aords) + 2 * i)[0]
            frva = struct.unpack_from("<I", self.d, self.off(afuncs) + 4 * ordinal)[0]
            fwd = None
            if erva <= frva < erva + esize:          # inside the directory = a forwarder
                fwd = self.zstr(frva)
            out[name] = fwd
        return out


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    data = open(path, "rb").read()
    print("checking %s (%d bytes)" % (path, len(data)))
    problems = []

    if not (MIN_SIZE <= len(data) <= MAX_SIZE):
        problems.append("size %d is outside the sane range %d..%d"
                        % (len(data), MIN_SIZE, MAX_SIZE))
    pe = PE(data)
    if pe.machine != 0x14C:
        problems.append("machine %#x is not i386 -- Sacred is a 32-bit process" % pe.machine)
    if pe.magic != 0x10B:
        problems.append("optional header magic %#x is not PE32" % pe.magic)
    if not (pe.characteristics & 0x2000):
        problems.append("the image is not marked as a DLL")

    exp = pe.exports()
    print("  exports: %d" % len(exp))
    for name in REQUIRED:
        fwd = exp.get(name, "MISSING")
        if fwd == "MISSING":
            problems.append("export %s is missing" % name)
        elif not fwd:
            problems.append("export %s is not a forwarder (should point at %s%s)"
                            % (name, FORWARD_TO, name))
        elif not fwd.startswith(FORWARD_TO):
            problems.append("export %s forwards to '%s', expected '%s%s'"
                            % (name, fwd, FORWARD_TO, name))
        else:
            print("    %-18s -> %s" % (name, fwd))

    if problems:
        print("FAILED:")
        for p in problems:
            print("  - " + p)
        return 1
    print("OK: 32-bit DLL, all six ijl* exports forward to ijl15_real")
    return 0


if __name__ == "__main__":
    sys.exit(main())
