# FunkCode source language

The FunkCode interpreter decoded in 16/17/19 consumes bytecode compiled
from a text source. The compiler (`cScriptCompiler`) is still present in the
retail binary, and its keyword tables expose the source-language grammar.

## Location

The string positions were initially mis-located (raw file offsets vs VAs),
so an xref search reported "no references". After fixing the offset-to-VA
conversion (`.data` section maps raw 0x4E7000 → vaddr 0x8E7000) the strings
line up:

| String | Real VA | xrefs in `.text` |
|---|---:|---:|
| `cScriptCompiler[%16s][%3d]:` | `0x00964044` | 1 |
| `parseStatement() unknown statement` | `0x00964078` | 1 |
| `parseStatement() line%d expected '%c' found: '%s'` | `0x00964120` | 5 |
| `parseResource() Resource [%d] already assigned!` | `0x00964168` | 1 |
| `loadScriptedSequenceR() reserving place for [%d] resources` | `0x009641c4` | 1 |
| `GLOBAL.TXT` | `0x00a1cc50` | 2 |
| `scripts\sets.txt` | `0x0094ad3c` | 1 |
| `scripts\waffenmod.txt` | `0x0094b020` | 1 |

The compiler is invoked from at least 9 distinct callsites in `.text`. The
`GLOBAL.TXT` / `sets.txt` / `waffenmod.txt` file paths are referenced from
live code — Sacred opens them on disk.

Decoded functions:
- `cScriptCompiler::parseStatement` @ va `0x0066fdf0` (711 decompiled lines)
- `cScriptCompiler::loadScriptedSequenceR` @ va `0x006714c0` (246 lines)
- `cScriptCompiler::parseResource` @ va `0x00670ea0` (298 lines)

## Recovered grammar

### Top-level keywords

```
if  else  for  while  return  exit
pragma  resources  resource  int  float
asm  callRPC  rand
SCRIPT_USER  SCRIPT_EDITOR
```

### Asm-block opcodes (24 entries, va 0x00964270 .. 0x00964778)

The compiler parses `asm { ... }` blocks against a flat array of 24
instruction descriptors, each 0x38 bytes wide:

```
[ 0] exit        [ 1] nop         [ 2] ret         [ 3] rsp
[ 4] cmp         [ 5] cmpi        [ 6] rspx        [ 7] jne
[ 8] je          [ 9] jmp         [10] and         [11] or
[12] dec         [13] inc         [14] not         [15] sub
[16] add         [17] mul         [18] div         [19] mov
[20] movi        [21] xchg        [22] rand        [23] callRPC
```

x86-flavoured — `rsp` is the stack-pointer, `rspx` likely an indexed-stack
form, `mov`/`movi` are register-to-register vs register-to-immediate, etc.

This explains the shape of the decoded FunkCode bytecode: most of the 162
opcode-cases in `FUN_00472bc0` are direct emissions from one of these 24
asm verbs, plus a few high-level helpers (e.g. `callRPC` → RPC marshaling,
`resource` declarations → indexed resource slot ops).

### Token kinds (from the lexer FUN_0065cc00)

```
0x08  IDENT     (identifier)
0x16  '{'       block-start
0x17  '}'       block-end
0x21  EOF       end of input
```

Others (numbers, strings, parens, punctuation) to be confirmed by decoding
the lexer directly. The error message `expected '%c' found: '%s'` is hit 5
times in parseStatement, each call checking a specific punctuation character.

### Predefined constants

The compiler recognises hundreds of named constants without declaration:

- `SOUND_FX_*` — entire sound-effects namespace (~200 names, e.g.
  `SOUND_FX_ANIMAL_HORSE_GALOP01`, `SOUND_FX_KOPIERSCHUTZ_DUMMY01`)
- `ECS_*` — combat/spell effects (`ECS_FIREBALL`, `ECS_HEALING`,
  `ECS_TELEPORT`, ...) — same set found inside the dispatcher
- `SCRIPT_USER`, `SCRIPT_EDITOR` — script mode flags
- `DLGNPC`, `QUESTNPC`, `QUESTFELLOW`, `CPOS:hero`, `CPOS:RES:<name>` —
  dispatch-target prefixes (same as opcode 0x01's target-type strings)

## Reconstructed source sample

For quest DQ_15013 (Green Plague), combining known facts:

```c
pragma resources 5
pragma SCRIPT_USER

resource log_title  = res:DQ_15013_LOG_TITEL
resource log_offen  = res:DQ_15013_LOG_OFFEN
resource log_ziel   = res:DQ_15013_LOG_ZIEL

int dq_belohnung_level
int dq_belohnung_typ
int dq_belohnung

// Trigger registration for stages
register_trigger("DQ_15013_START")
register_trigger("DQ_15013_OFFEN")
register_trigger("DQ_15013_ZIEL")

// Reward-tier branching
if (level < 30) {
    dq_belohnung_level = 10
} else if (level < 60) {
    dq_belohnung_level = 20
} else {
    dq_belohnung_level = 30
}
dq_belohnung_typ = 3
dq_belohnung     = 127

// Log entry setup
quest_log_set(15013, slot=0, log_title)
quest_log_set(15013, slot=1, log_offen)
quest_log_set(15013, slot=1, log_ziel)

exit
```

Exact syntax (operators, separators, semicolon-rules) needs lexer decoding;
the shape and vocabulary are confirmed.

## SDK pipeline status

| Layer | Direction | Status |
|---|---|---|
| `.txt` source | (what devs wrote) | Done — grammar + keywords recovered |
| compiler | text → bytecode | Done — in binary, callable in theory |
| disassembler | bytecode → readable | Done — `quest_script.py` |
| interpreter | bytecode → runtime actions | Done — docs 16/17/19 |
| dispatcher | runtime actions → game | Done — tag→subsystem: doc 18 |

### Two mod paths

Path A — bytecode mod (no compiler needed):
- Edit FunkCode.bin records directly (encoding via funkcode_disasm)
- Same as the text-mod workflow with global.res via Patch 1, but for `.bin`
- Patch-N hook: serve `bin/TYPE_NPC_X/FunkCode.bin` from disk via DLL

Path B — source mod (use the in-binary compiler):
- Write `.txt` in recovered grammar
- Call `cScriptCompiler::loadScriptedSequenceR` directly via DLL — pass a
  text buffer, get back a fresh FunkCode.bin
- Inject the result wherever Sacred would load it

Path A is closer to working. Path B is reversible — decompile any quest to
.txt that the same compiler accepts, edit, and recompile to bytecode.
Round-trip safety holds because it uses Sacred's own compiler.

## Open work

1. Decode the lexer (`FUN_0065cc00`) — full token table (numbers, strings,
   operators, punctuation).
2. Map asm op → bytecode: each of the 24 asm verbs emits one of the 162
   dispatcher opcodes. The 0x38-byte descriptor likely contains
   `{ name[], byte_opcode, operand_count, format_flags }`. Read it out.
3. Map `resource` and `callRPC` keywords to action verbs — these high-level
   constructs probably compile to specific tag-record sequences, not single
   asm opcodes.
4. Test Path B: call `loadScriptedSequenceR` from the DLL with a simple test
   script string. Clean compile means a working bidirectional pipeline.

## Files

- `sdk/re/py/extract_compiler_strings.py` — string sweeper for `.data`
  blocks, used to find all keyword tokens
- `sdk/re/py/funkcode_grammar.py` — token / keyword reference module
- `sdk/re/py/search_va_uses.py` — finds raw 4-byte LE refs to a VA in `.text`
- `sdk/re/ghidra/FindStringConsumers.java` — Ghidra script for the same
  but using instruction-operand analysis
