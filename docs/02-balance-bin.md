# 02 — `bin\Balance.bin`

24 328-byte file holding global balance tables (difficulty multipliers, region tables, …). Loaded by `Sacred.exe` early at startup.

## Three variants

| Variant | Source | SHA-256 (head) |
|---|---|---|
| `steam` | `E:\…\bin\Balance.bin` | `e8dbe434cec8250e…` |
| `vanilla` | `SacredUtils\Resources\game\balance\BalanceVanilla.bin` | identical to steam |
| `veteran` | `SacredUtils\Resources\game\balance\BalanceVeteran.bin` | `3972cc9399eea651…` |

Steam ships unmodified vanilla. Veteran differs from vanilla by only 40 bytes in two clusters.

## Veteran diff (40 bytes total)

All differences fall on 4-byte boundaries inside the float array. Decoded as IEEE-754 LE:

| Offset | Vanilla | Veteran |
|---|---|---|
| `0x072C` | 1.10 | 6.11 |
| `0x0730` | 1.25 | 6.26 |
| `0x0734` | 1.40 | 6.41 |
| `0x0738` | 1.50 | 6.51 |
| `0x0740` | 1.00 | 6.01 |
| `0x0774` | 1.35 | 9.61 |
| `0x0778` | 1.77 | 10.03 |
| `0x077C` | 2.26 | 10.52 |
| `0x0780` | 2.83 | 11.09 |
| `0x0788` | 1.00 | 9.26 |

Two monotonic ramps, ~×6 and ~×4–7 lift:

- cluster A (0x72C–0x740): per-difficulty global damage / HP multiplier (Silver → Niob)
- cluster B (0x774–0x788): a second per-difficulty curve (XP gain? loot quality?) — to be confirmed by bisecting

A "difficulty mod" is therefore a 40-byte binary patch, expressible as a JSON mod-overlay format later.

## Structural sketch

```
0x0000  u32  magic/version  = 0x00001320   (= 4896 — purpose unknown)
0x0004  …    dense float32 array — most of the file (~6000 floats)
              with embedded sub-records of period 8 / 12 / 60 bytes
              (period heuristic top scores: (42,8), (33,12), (26,60), (25,52))
0x5758  …    string table — region names (UI_REGION_*)
                @ 0x5758 UI_REGION_SOUTHKERN
                @ 0x5798 UI_REGION_NORTHKERN
                @ 0x57D8 UI_REGION_SUMPF      (= swamp)
                @ 0x5818 UI_REGION_WUSTE      (= desert)
                @ 0x5858 UI_REGION_NORD
                @ 0x5898 UI_REGION_LAVA
                @ 0x58D8 UI_REGION_SHADDARNUR
                @ 0x5918 UI_REGION_UW_UPPER   (Underworld upper)
                @ 0x5958 UI_REGION_UW_LOWER   (Underworld lower)
0x5980… end   zero padding to 24 328 bytes
```

Strings are 8-aligned and 0x40 bytes apart — likely each region is a fixed 0x40-byte struct `{ char name[K]; … fields }` referenced from somewhere earlier.

## Tooling

`sdk\tools\balance_diff.py` — diff/hash/profile script. Reads steam + SacredUtils vanilla + veteran and emits this report.

## Next probes

1. Bisect cluster A — modify one float at a time in a side-by-side copy, run, observe.
2. Find the function in `Sacred.exe` that reads offset `0x1320`/`0x72C` — gives struct typings. (Callers identified post-decrypt: `FUN_00803920`, `FUN_00711ee0` — see [09-ghidra-and-encrypted-text.md](09-ghidra-and-encrypted-text.md).)
3. Decide on a mod-overlay format that can express "Veteran" as a 10-line JSON.
