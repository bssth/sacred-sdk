# Sacred resource-name hash — cracked

After splicing the decrypted `.text` back into `Sacred.exe` and running Ghidra
auto-rename, the two functions that drive `global.res` lookup were finally
readable. The string-name path (`FUN_0080e780`) contains the hash function
verbatim.

## The functions

```
0x0080e780  FUN_0080e780(name) -> id_or_NULL    # accepts a name buffer, hashes it, defers to 0x0080eaf0
0x0080eaf0  FUN_0080eaf0(hash) -> entry_ptr     # the actual lookup against an in-memory dictionary
0x006726f0  bit-31 dispatcher                   # if (id & 0x80000000) eaf0(id & 0x7fffffff); else e780(id)
0x0080e680  PE-resource chained-XOR loader      # populates one of the pools at boot
0x0084bce6  toupper() — MSVC default-locale fast path
```

The `Sacred.exe` import-by-name flow goes:

```
caller --resource("FOO_BAR") --> 0x0080e780
                                    │
                                    │ hash = 0
                                    │ for c in "FOO_BAR":
                                    │     hash = ((toupper(c) + hash*113) & 0xFFFFFFFF) signed % 999_999_991
                                    │ hash &= 0x7FFFFFFF
                                    │
                                    └──> 0x0080eaf0(hash) ──> entry / "GetResource HASH %d failed"
```

## Algorithm (faithful)

The key subtlety: `uVar6 * 0x71` overflows 32-bit, the result is cast back to
signed `int` for the modulo, and C's signed `%` keeps the dividend's sign.
A pure mathematical `(c + h*113) % MOD` formulation produces **zero** hits;
the 32-bit overflow version produces thousands.

```python
MOD = 0x3B9AC9F7    # 999_999_991, prime
MUL = 113

def sacred_hash(name: str) -> int:
    h = 0
    for c in name:
        oc = ord(c)
        if 0x61 <= oc <= 0x7A:        # ASCII 'a'..'z'
            oc -= 0x20
        prod = (h * MUL) & 0xFFFFFFFF
        s    = (oc + prod) & 0xFFFFFFFF
        si   = s - 0x100000000 if s >= 0x80000000 else s
        if si >= 0: r =  si % MOD
        else:       r = -((-si) % MOD)
        h = r & 0xFFFFFFFF
    return h & 0x7FFFFFFF
```

Stored in `sdk/tools/sacred_hash.py` as the reusable module.

## Validation

Self-test vectors recovered from a passive scrape of the binary tree (uppercase
ASCII identifiers >= 3 chars, found via `[A-Z_][A-Z0-9_]{2,63}` over `bin/`
and `scripts/`):

| name                       | hash       | text in global.res                            |
|----------------------------|-----------:|-----------------------------------------------|
| `DQ1_TOETE_NPC_ZIEL18`     | 2123824365 | "Undead Mage"                                  |
| `DQ5_BRINGE_NPC_ZIEL16`    |  624127448 | "Veterinarian"                                 |
| `RB_5082_LOG_TITLE`        |  636419051 | "Clear the Northern Core Region."              |
| `NQ_UW9521_LOG_SIEG`       |    5003676 | "The Master Baker received his spice..."       |
| `CITY_DRACO_5`             |  346830888 | "Porto Draco"                                  |
| `ID_NPC_MERC`              | 1269826129 | "Mick the Swift"                               |

Coverage of the 22,493 hash-form ids in `global.res`:
- 3,017 (13.4 %) recovered by passive binary scrape (`hash_verify.py`)
- 1,847 additional via template brute-force (`hash_expand.py`),
  using incremental hashing (left-fold associativity) so 16M name
  trials take ~5s in pure Python
- ~17k names remain — almost certainly handcrafted region/object IDs
  that need either a leaked .qst source dump or a smarter dictionary.

Full cracked map written to `sdk/tools/hash_names.csv`.

## Naming conventions inferred

| prefix          | meaning                                              |
|-----------------|------------------------------------------------------|
| `DQ<N>_`        | Dynamische Quest type, index N (instance pool)        |
| `DQ_<NNNN>_`    | one specific dynamic quest, id NNNN                   |
| `RB_<NNNN>_`    | Runebook quest                                        |
| `NQ_<NNNN>_`    | normal (handcrafted) quest                            |
| `NQ_UW<NNNN>_`  | normal quest, Underworld expansion                    |
| `_TOETE_`       | "kill" objective                                      |
| `_BRINGE_`      | "bring" objective                                     |
| `_ESKORTIERE_`  | "escort" objective                                    |
| `_BEFREIE_`     | "rescue" objective                                    |
| `_ZIEL<M>_`     | target slot M                                         |
| `_AUFTRAGGEBER` | quest giver NPC                                       |
| `_OFFEN`        | open / not-yet-completed state log entry              |
| `_SIEG`         | victory / completion log                              |
| `_LOG_TITLE`    | log book title                                        |
| `_LOG_HEADER`   | log book subtitle                                     |
| `_PRENPC_`      | dialogue line before NPC is engaged                   |
| `CITY_<NAME>_<N>` | city name variant N (multiple-language fallbacks)   |
| `NPC_AUDIO_<TAG>_<a>_<b>_<c>` | voice-line subtitle                     |

## Why this matters

- Quest text is now fully decodable: any reference to `res:HHH` in script files
  resolves to a meaningful string with a meaningful name.
- The reverse direction works too: once we want to mod a string, we hash its
  intended new name and inject `(hash, replacement_text)` into the loaded pool.
- The same hash almost certainly drives the FunkCode tag dictionary's
  symbolic references (we'll verify in the next pass).

## Stringify rule for numeric ids

`FUN_0080e780` accepts a numeric integer in callers that pass via the
bit-31-clear branch of `FUN_006726f0`. The id is **decimal-stringified**
inside `FUN_005f6290` before hashing — so `resource(17000)` ultimately calls
`FUN_0080eaf0(hash("17000"))`.

Verified empirically: every one of the 823 entries in the community
`names.csv` (ids 17000..17822) matches `global.res` under
`sacred_hash(str(id))`. Helper: `tools/sacred_hash.py::hash_for_id(int)`.

Complete lookup model:

```
resource_lookup(id):
    if id & 0x80000000:                    # high bit means "hashed already"
        return dict.get(id & 0x7FFFFFFF)
    else:
        return dict.get(sacred_hash(str(id)))
```

## Open questions

1. **The other ~17k names.** Our 3,017+1,847 hits came from binaries and
   template brute-force — the rest live only in design docs / removed assets.
   A live `FUN_0080eaf0` log hook (next milestone) will trivially cover
   whatever the game actually queries during play.
2. **PE BINARY resource pool.** The bit-31 dispatcher in `FUN_006726f0` selects
   between two pools. We've identified one (`global.res`); the other is loaded
   by `FUN_0080e680` from PE `BINARY` resources via chained-XOR (last word
   XOR `0x45AD`, then propagating). Extraction script still to write.
3. **Is the dispatcher's bit-31 the same as the hash's bit-31 mask?** Strongly
   suspect yes — the hash strips bit 31, and the dispatcher uses bit 31 of an
   already-numeric id to switch pools. So an integer with bit 31 set is "this
   is a hashed name in pool A," and without bit 31 it's "this is a literal id
   in pool B." Verify by tracing a few callers.
