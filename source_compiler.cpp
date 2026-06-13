// SacredSDK — call Sacred's built-in cScriptCompiler.
//
// The compiler entry (FUN_00671ad0 in our decrypted Ghidra project) has
// zero static xrefs in retail Sacred. The function itself is intact and
// self-contained: it opens a file path passed as arg, tokenises with
// FUN_0065cc00, parses with cScriptCompiler::parseStatement, emits FunkCode
// bytecode via cScriptCompiler::loadScriptedSequenceR. No game-state
// dependency.
//
// What `this` needs to be
// -----------------------
// FUN_00671ad0 reads/writes the first 8 bytes of `this`:
//   *(int*)(this + 0) = 0;   // initialised at top of FUN_00671ad0
//   *(int*)(this + 4) = 0;
// Beyond that, intermediate state lives on the stack frame, not the
// instance. So a zero-init buffer is sufficient — no vtable required, no
// constructor call needed.

#include "sdk.h"
#include <cstdio>
#include <cstring>

namespace sdk { namespace source_compiler {

volatile long g_runs      = 0;
volatile long g_successes = 0;
char g_last_message[256]  = "(not run yet)";

// Address of FUN_00671ad0 inside Sacred.exe (image-base + RVA).
constexpr uintptr_t COMPILE_RVA = 0x00671AD0 - 0x00400000;

// __thiscall signature: ECX = this, one stack arg = file path. MSVC's
// __fastcall is binary-compatible (ECX=arg1, EDX=arg2 ignored, stack=arg3).
typedef int (__fastcall *compile_fn_t)(void* this_ptr,
                                       void* /*edx_dummy*/,
                                       const char* file_path);

// ScriptCompiler "instance" — 16 KB zero-init buffer.  loadScriptedSequenceR
// writes the resource-pool pointers to `this+0x142c` / `this+0x1430`, and
// other state lives at offsets we haven't fully mapped yet.  We over-provision
// to be safe.  Reused across invocations.
alignas(16) static uint8_t g_compiler_inst[0x4000] = {0};

// Layout hints (from cScriptCompiler::loadScriptedSequenceR decompile):
//   +0x142c  void**  resource-pool data ptr
//   +0x1430  void**  resource-pool end  ptr
// `(end - data) >> 2` gives the resource count.
constexpr size_t RESPOOL_DATA_OFF = 0x142c;
constexpr size_t RESPOOL_END_OFF  = 0x1430;

// Resolve <game_dir>\<rel> into out.
static void resolve_path(const char* rel, char out[MAX_PATH]) {
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(g_attach.exe_module, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\'); if (slash) *slash = 0;
    _snprintf_s(out, MAX_PATH, _TRUNCATE, "%s\\%s", exe, rel);
}

// Walk the post-compile instance buffer and log every contiguous non-zero
// run. Pure forensics: we still don't know the actual bytecode-output offset,
// so we discover it empirically — re-run with several probes and watch which
// ranges grow with input complexity.
//
// Also dumps the entire buffer to disk for offline inspection.
static void scan_and_dump_inst(const char* tag) {
    // Find non-zero ranges.
    const size_t N = sizeof(g_compiler_inst);
    int ranges = 0;
    size_t i = 0;
    while (i < N) {
        if (g_compiler_inst[i] != 0) {
            size_t start = i;
            while (i < N && g_compiler_inst[i] != 0) i++;
            size_t len = i - start;
            // Show first 16 bytes of the range for shape-recognition.
            char hex[64] = {0};
            for (size_t k = 0; k < (len < 16 ? len : 16); k++) {
                char h[4]; _snprintf_s(h, _TRUNCATE, "%02x ", g_compiler_inst[start+k]);
                strncat_s(hex, _TRUNCATE, h, _TRUNCATE);
            }
            sdk_log("[compile] inst[+0x%04zx..+0x%04zx] %zu bytes: %s%s",
                    start, i, len, hex, len > 16 ? "..." : "");
            ranges++;
            if (ranges > 20) {
                sdk_log("[compile] (more than 20 ranges — clipping)");
                break;
            }
        } else {
            i++;
        }
    }
    if (ranges == 0) {
        sdk_log("[compile] inst buffer ALL ZERO after compile");
    }

    // Dump the whole inst to disk for offline poking.
    char path[MAX_PATH];
    char rel[MAX_PATH];
    _snprintf_s(rel, _TRUNCATE, "sdk\\logs\\compile_inst_%s.bin", tag);
    resolve_path(rel, path);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") == 0 && f) {
        fwrite(g_compiler_inst, 1, N, f);
        fclose(f);
        sdk_log("[compile] wrote %zu bytes -> %s", N, path);
    }
}

static void set_msg(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_last_message, sizeof(g_last_message), fmt, ap);
    va_end(ap);
    sdk_log("[compile] %s", g_last_message);
}

// Forward decl for the inst-dump tag override (see compile_file signature).
int compile_file_tagged(const char* file_path, const char* tag);

int compile_file(const char* file_path) {
    return compile_file_tagged(file_path, "manual");
}

int compile_file_tagged(const char* file_path, const char* tag) {
    InterlockedIncrement(&g_runs);

    if (!g_attach.exe_module) {
        set_msg("no exe module — aborting");
        return 0;
    }
    // Sanity-check the target hasn't been mangled (compiler functions are
    // in .text which is decrypted at runtime). We need to be invoked after
    // .text decryption — same gating as text_logger.
    uintptr_t base = (uintptr_t)g_attach.exe_module;
    uint8_t* code = (uint8_t*)(base + COMPILE_RVA);
    if (code[0] != 0x6A && code[0] != 0x55 && code[0] != 0x53) {
        // Expected MSVC prologue starts (push -1 / push ebp / push ebx).
        // Anything else means .text is still encrypted or layout shifted.
        set_msg("FUN_00671ad0 prologue 0x%02x unexpected — abort", code[0]);
        return 0;
    }

    // Zero the instance again (compile_file may be called multiple times).
    memset(g_compiler_inst, 0, sizeof(g_compiler_inst));

    compile_fn_t compile = (compile_fn_t)code;
    int result = 0;
    long opens_before = fs_override::g_total_opens;
    fs_override::g_verbose = 1;     // capture EVERY CreateFileA path
    __try {
        result = compile(g_compiler_inst, nullptr, file_path);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        fs_override::g_verbose = 0;
        set_msg("SEH: exception 0x%08x inside compiler",
                GetExceptionCode());
        return 0;
    }
    fs_override::g_verbose = 0;
    long opens_after = fs_override::g_total_opens;
    sdk_log("[compile] CreateFileA opens during call: %ld",
            opens_after - opens_before);

    // Inspect the compiler-instance buffer after the call.
    void* head_ptr = *(void**)(g_compiler_inst + 0);
    size_t head_size = *(size_t*)(g_compiler_inst + 4);
    void* respool_data = *(void**)(g_compiler_inst + RESPOOL_DATA_OFF);
    void* respool_end  = *(void**)(g_compiler_inst + RESPOOL_END_OFF);
    size_t respool_n = respool_data && respool_end
        ? ((uintptr_t)respool_end - (uintptr_t)respool_data) / 4
        : 0;

    set_msg("'%s' -> result=%d  head=%p/%zu  respool=%p..%p (%zu entries)",
            file_path, result, head_ptr, head_size,
            respool_data, respool_end, respool_n);
    if (result) InterlockedIncrement(&g_successes);

    // Forensic post-mortem of the instance buffer: where did the compiler
    // write? Each probe tag gets its own dump file so we can diff side by side.
    scan_and_dump_inst(tag);

    return result;
}

static void write_text(const char* full_path, const char* src) {
    FILE* f = nullptr;
    if (fopen_s(&f, full_path, "wb") == 0 && f) {
        fwrite(src, 1, strlen(src), f);
        fclose(f);
        sdk_log("[compile] wrote %s (%zu chars)", full_path, strlen(src));
    }
}

// Sequence of progressively larger probes. Each run picks the NEXT one
// (g_runs counter drives the index) so the user can click multiple times
// and watch each grammar test in turn.
struct Probe {
    const char* tag;        // short id used in compile_inst_<tag>.bin
    const char* rel_path;
    const char* source;
    const char* note;
};

// Ordered from minimal to "should clearly emit bytecode" — diffing the
// resulting compile_inst_<tag>.bin files tells us which buffer offset is
// the actual emit destination.
static const Probe PROBES[] = {
    {
        "01_empty",
        "custom\\scripts\\test_01_empty.txt",
        "pragma resources 0\n"
        "exit\n",
        "pragma+exit only — baseline"
    },
    {
        "02_one_res",
        "custom\\scripts\\test_02_one_res.txt",
        "pragma resources 1\n"
        "resource HQ_3_1_4_Log_Title\n"
        "exit\n",
        "1 resource — fills resource pool"
    },
    {
        "03_two_res",
        "custom\\scripts\\test_03_two_res.txt",
        "pragma resources 2\n"
        "resource HQ_3_1_4_Log_Title\n"
        "resource HQ_3_1_4_Log_Header\n"
        "exit\n",
        "2 resources — pool grows"
    },
    {
        "04_var",
        "custom\\scripts\\test_04_var.txt",
        "pragma resources 0\n"
        "int dq_test\n"
        "dq_test = 42\n"
        "exit\n",
        "int decl + assign — bytecode should appear"
    },
    {
        "05_arith",
        "custom\\scripts\\test_05_arith.txt",
        "pragma resources 0\n"
        "int a\n"
        "int b\n"
        "a = 10\n"
        "b = a + 5\n"
        "exit\n",
        "two ints + arithmetic — bigger bytecode"
    },
    {
        "06_if",
        "custom\\scripts\\test_06_if.txt",
        "pragma resources 0\n"
        "int x\n"
        "x = 7\n"
        "if x > 0\n"
        "  x = 99\n"
        "exit\n",
        "control flow — if-branch"
    },
    {
        "07_asm",
        "custom\\scripts\\test_07_asm.txt",
        "pragma resources 0\n"
        "asm {\n"
        "  movi  rspx[0]  123\n"
        "  ret\n"
        "}\n"
        "exit\n",
        "inline asm — direct opcodes"
    },
};

void smoke_test() {
    static const size_t N = sizeof(PROBES) / sizeof(PROBES[0]);
    size_t idx = (size_t)g_runs % N;
    const Probe& p = PROBES[idx];

    char dir[MAX_PATH], full[MAX_PATH];
    resolve_path("custom\\scripts", dir);
    CreateDirectoryA(dir, nullptr);
    resolve_path(p.rel_path, full);
    write_text(full, p.source);
    sdk_log("[compile] probe #%zu '%s' — %s", idx, p.tag, p.note);

    // Pass ABSOLUTE path to FUN_00671ad0 — Sacred's compiler appears to do
    // its own file open via FUN_0065aea0 with an exact path, no internal
    // base-dir resolution. With a relative path the open fails, the function
    // bails to the early-exit branch and returns AL=1 anyway (always),
    // leaving the instance buffer untouched (that's the "ALL ZERO" we saw).
    compile_file_tagged(full, p.tag);
}

}} // namespace sdk::source_compiler
