"""Sacred Gold resource-name hash.

Discovered by decompiling FUN_0080e780 @ va:0x0080e780 (the string-name path
of the global.res lookup). The char-transform helper FUN_0084bce6 is MSVC's
toupper() in the default C-locale fast path.

Algorithm (faithful 32-bit reproduction):

    h = 0  (uint32)
    for c in name:
        c' = toupper_ascii(c)
        h = ((c' + h * 113) & 0xFFFFFFFF) as int32
        h = (h % 999_999_991) as uint32       # C signed remainder, sign of dividend
    return h & 0x7FFFFFFF

Constants:
    multiplier   0x71            = 113
    modulus      0x3B9AC9F7      = 999_999_991 (prime, just below 10^9)
    bit-31 mask  0x7FFFFFFF      = strip 31st bit (selects pool in caller)

Verified: 3017/22493 hits against global.res when fed an uppercase-identifier
scrape of the game's binary tree. The remaining 19k names are design-doc
identifiers absent from shipped binaries.
"""

MOD = 0x3B9AC9F7
MUL = 113


def sacred_hash(name: str) -> int:
    """Compute the Sacred Gold resource hash for a symbolic name."""
    h = 0
    for c in name:
        oc = ord(c)
        if 0x61 <= oc <= 0x7A:           # ASCII 'a'..'z'
            oc -= 0x20
        prod = (h * MUL) & 0xFFFFFFFF
        s = (oc + prod) & 0xFFFFFFFF
        si = s - 0x100000000 if s >= 0x80000000 else s
        if si >= 0:
            r = si % MOD
        else:
            r = -((-si) % MOD)
        h = r & 0xFFFFFFFF
    return h & 0x7FFFFFFF


def hash_for_id(int_id: int) -> int:
    """Numeric ids pass through 0080e780's str(id)+hash path before reaching
    0080eaf0 unless their bit 31 is set. This helper makes that explicit.
    Verified against 823/823 community names.csv entries (17000..17822)."""
    return sacred_hash(str(int_id))


# Self-test
if __name__ == "__main__":
    cases = [
        ("DQ1_TOETE_NPC_ZIEL18",     2123824365),
        ("DQ5_TOETE_NPC_ZIEL16",       94443938),
        ("RB_5082_LOG_TITLE",         636419051),
        ("NQ_UW9521_LOG_SIEG",          5003676),
        ("CITY_DRACO_5",              346830888),
        ("ID_NPC_MERC",              1269826129),
    ]
    print("self-test:")
    for name, expected in cases:
        got = sacred_hash(name)
        ok  = "OK " if got == expected else "FAIL"
        print(f"  [{ok}] {name:<32} -> {got:<10} (expected {expected})")
