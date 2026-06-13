# Step 1: find vanilla Dialog (tag 0x1f) records in FunkCode.bin and dump the
# exact TLV bytes + decoded payload (the emit template for Path A).
# TLV: [tag:u8][size:u16 BE incl 3-byte hdr][payload]. Payload opcode stream
# decoded by FUN_00472bc0: field id u8 then value; 1=ASCIIZ, 0xb=i32, 0x16=ASCIIZ,
# 2=i32, 3=u16, 4=pos, 0x20=cpos, 0=END  (per triggers_dialog_move.md).
import struct
BIN = r"E:\SteamLibrary\steamapps\common\Sacred Gold\bin\TYPE_NPC_VAMPIRELADY\FunkCode.bin"
d = open(BIN, "rb").read()

def decode_payload(p):
    # p = payload bytes after the 3-byte TLV header (starts with a flags byte?)
    out = []
    i = 0
    # leading flags byte (0x00 seen in npc_templates); tolerate it
    while i < len(p):
        fid = p[i]; i += 1
        if fid == 0x00:
            out.append("END"); break
        elif fid == 0x01 or fid == 0x16 or fid == 0x09:
            s = bytearray()
            while i < len(p) and p[i] != 0: s.append(p[i]); i += 1
            i += 1
            out.append("f%02x:'%s'" % (fid, s.decode('latin1', 'replace')))
        elif fid == 0x0b or fid == 0x02:
            if i+4 <= len(p):
                v = struct.unpack_from('<i', p, i)[0]; i += 4
                out.append("f%02x=%d" % (fid, v))
            else: out.append("f%02x:<trunc>" % fid); break
        elif fid == 0x03:
            if i+2 <= len(p):
                v = struct.unpack_from('<H', p, i)[0]; i += 2
                out.append("f%02x=%d" % (fid, v))
            else: break
        elif fid == 0x04:
            # position: i32 (-2 => name follows) then maybe ASCIIZ
            if i+4 <= len(p):
                v = struct.unpack_from('<i', p, i)[0]; i += 4
                if v == -2:
                    s = bytearray()
                    while i < len(p) and p[i] != 0: s.append(p[i]); i += 1
                    i += 1
                    out.append("f04:pos'%s'" % s.decode('latin1','replace'))
                else:
                    out.append("f04=%d" % v)
            else: break
        else:
            out.append("f%02x?" % fid)
            # unknown: stop to avoid mis-sync
            out.append("rest=" + p[i:i+12].hex())
            break
    return " ".join(out)

found = 0
i = 0
n = len(d)
while i + 3 <= n and found < 8:
    tag = d[i]
    size = (d[i+1] << 8) | d[i+2]      # u16 BE, includes 3-byte header
    if tag == 0x1f and 3 < size < 256 and i + size <= n:
        payload = d[i+3 : i+size]
        rec = d[i : i+size]
        print("@0x%06X tag=0x1f size=%d bytes=%s" % (i, size, rec.hex()))
        print("    payload: %s" % decode_payload(payload))
        found += 1
        i += size
    else:
        i += 1
print("\nfound %d Dialog(0x1f) records (showing first 8)" % found)
