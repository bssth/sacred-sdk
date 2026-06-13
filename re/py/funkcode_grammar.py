"""FunkCode source-language tokens — recovered from cScriptCompiler in
Sacred.exe (decrypted), .data section around va 0x00964000.

The compiler is intact in the binary (xrefs verified). Confirmed entry
points:
   cScriptCompiler::parseStatement       @ va 0x0066fdf0  (711 lines)
   cScriptCompiler::loadScriptedSequenceR@ va 0x006714c0  (246 lines)
   cScriptCompiler::parseResource        @ va 0x00670ea0  (298 lines)

Token category map below mirrors what parseStatement's string-compare chain
checks. The `asm` table is a flat array of 24 entries × 0x38 bytes starting
at va 0x00964268; each entry has a name and a byte-encoding for the FunkCode
bytecode opcode it emits.
"""

# Top-level keywords (high-level constructs)
HIGH_LEVEL = {
    "if",
    "else",
    "for",
    "while",
    "return",     # return statement
    "exit",       # exit script
    "pragma",     # directives
    "resources",  # `pragma resources N` — pre-allocate N resource slots
    "resource",   # type / declarator
    "int",
    "float",
    "asm",        # opens an inline-assembly block emitting raw bytecode
    "callRPC",    # remote procedure call (multiplayer)
    "rand",       # random
}

# Inline-asm op-table (parsed inside `asm { ... }`).
# Each entry: ("token", opcode_index_in_asm_table)
# Stride in .data = 0x38 bytes. The first entry at va 0x00964270 = "exit".
ASM_OPS = [
    "exit",       # 0
    "nop",        # 1
    "ret",        # 2
    "rsp",        # 3  (push?)
    "cmp",        # 4  compare
    "cmpi",       # 5  compare-int
    "rspx",       # 6  ?
    "jne",        # 7
    "je",         # 8
    "jmp",        # 9
    "and",        # 10
    "or",         # 11
    "dec",        # 12
    "inc",        # 13
    "not",        # 14
    "sub",        # 15
    "add",        # 16
    "mul",        # 17
    "div",        # 18
    "mov",        # 19
    "movi",       # 20
    "xchg",       # 21
    "rand",       # 22
    "callRPC",    # 23
]

# Predefined constants. The compiler accepts hundreds of `SOUND_FX_*` names
# directly as identifiers (they map to specific u32 ids). Same probably for
# `ECS_*` spell ids (we found them in the interpreter dispatcher earlier).
KNOWN_CONST_PREFIXES = (
    "SOUND_FX_",   # ~hundreds, e.g. SOUND_FX_ANIMAL_HORSE01
    "ECS_",        # ECS_FIREBALL, ECS_HEALING, ECS_TELEPORT, ...
    "SCRIPT_",     # SCRIPT_USER, SCRIPT_EDITOR
    "DLGNPC",
    "QUESTNPC",
    "QUESTFELLOW",
    "CPOS:hero",
    "CPOS:RES:",
)

# Punctuation tokens. These come from the LEXER (FUN_0065cc00). Tokens
# observed during parseStatement / loadScriptedSequenceR:
#   0x08  identifier
#   0x16  '{'  (block start — parseStatement recurses)
#   0x17  '}'  (block end — exits the recursion loop)
#   0x21  EOF  (loadScriptedSequenceR's outer loop exits on this)
TOKEN_KINDS = {
    0x08: "IDENT",
    0x16: "LBRACE",     # '{'
    0x17: "RBRACE",     # '}'
    0x21: "EOF",
    # others to be confirmed: NUMBER, STRING_LIT, ';', ',', '(', ')', '+', etc.
}

EXAMPLE_PSEUDOCODE = """\
# What a FunkCode .txt source likely looked like (reconstructed)

pragma resources 5
pragma SCRIPT_USER

resource log_title  = res:HQ_3_1_4_Log_Title
resource log_header = res:HQ_3_1_4_Log_Header
resource log_qstart = res:HQ_3_1_4_Log_Qstart

int dq_belohnung_level
int dq_belohnung_typ
int dq_belohnung

if (some_condition) {
    dq_belohnung_level = 10
} else if (other_condition) {
    dq_belohnung_level = 20
} else {
    dq_belohnung_level = 30
}

dq_belohnung_typ = 3
dq_belohnung     = 127

quest_log_set(quest_id=15013, slot=0, log_title)
quest_log_set(quest_id=15013, slot=1, log_qstart)

# Inline bytecode for performance-critical or low-level effects:
asm {
    movi  rspx[0]   123
    cmpi  rspx[0]   100
    jne   skip
    callRPC  SOUND_FX_FIREBALL
skip:
    ret
}

exit
"""

if __name__ == "__main__":
    print(f"High-level keywords ({len(HIGH_LEVEL)}):")
    for k in sorted(HIGH_LEVEL): print(f"  {k}")
    print(f"\nAsm-block opcodes ({len(ASM_OPS)}):")
    for i, op in enumerate(ASM_OPS):
        print(f"  [{i:>2}]  {op}")
    print(f"\nKnown lexer token kinds: {TOKEN_KINDS}")
    print("\n--- example pseudo-code ---")
    print(EXAMPLE_PSEUDOCODE)
