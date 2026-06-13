// Dump the runtime-decrypted .text section of Sacred.exe.
//
// Sacred.exe ships with `.text` encrypted (Shannon entropy 8.000 on disk).
// Its entry point is in the `.bind` section, which contains a SafeDisc-style
// stub that decrypts `.text` in memory before transferring control. By the
// time `DllMain` of our `ijl15.dll` proxy runs, the stub hasn't executed yet
// (DLL initialisers run before EP). So we spawn a worker thread, wait for
// the decryption to complete, and copy `.text` to disk.
//
// Detection: every 200 ms we sample 16 KB from the middle of `.text` and
// compute Shannon entropy. Encrypted bytes are uniform-random (H ≈ 8.0).
// Real x86 code has H ≈ 5.5–6.5. We trigger the dump as soon as H drops
// below 7.0 (with a hard cap of 30 polling rounds = 6 s).

#include "sdk.h"
#include <cmath>
#include <cstdio>

namespace sdk { namespace dumptext {

// Sacred.exe's .text section, established from PE headers (recon + Ghidra):
//   raw file offset : 0x1000
//   raw size        : 0x48F000
//   virtual address : ImageBase(0x400000) + 0x1000 = 0x401000
// We dump exactly raw_size bytes so the dump can be spliced back into the
// EXE file 1-for-1.
static constexpr uintptr_t TEXT_VA   = 0x00401000;
static constexpr size_t    TEXT_SIZE = 0x0048F000;

static double entropy(const unsigned char* p, size_t n) {
    size_t hist[256] = {0};
    for (size_t i = 0; i < n; i++) hist[p[i]]++;
    double H = 0.0;
    for (int i = 0; i < 256; i++) {
        if (!hist[i]) continue;
        double pi = double(hist[i]) / double(n);
        H -= pi * std::log2(pi);
    }
    return H;
}

static void resolve_dump_path(char* out, size_t outlen) {
    char exe_path[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    char* slash = strrchr(exe_path, '\\');
    if (slash) *slash = 0;
    char dir[MAX_PATH];
    _snprintf_s(dir, _TRUNCATE, "%s\\sdk\\logs", exe_path);
    CreateDirectoryA(dir, NULL);
    _snprintf_s(out, outlen, _TRUNCATE, "%s\\text_dump.bin", dir);
}

static DWORD WINAPI worker(LPVOID) {
    // Sample window in the middle of .text — picked to be deep enough that
    // very-late patching from the stub still has had time to settle.
    const unsigned char* sample = reinterpret_cast<unsigned char*>(TEXT_VA + 0x100000);
    const size_t SAMPLE_LEN = 16 * 1024;

    bool decrypted = false;
    double last_H = 8.0;
    int round = 0;
    // Poll more aggressively than the original (50 ms vs 200 ms) so we fire
    // our .text patches as soon as possible after the .bind stub finishes —
    // there's a race with Sacred's own code reaching e.g. FUN_0080e680.
    constexpr int POLL_MS    = 50;
    constexpr int MAX_ROUNDS = 200;          // 200 * 50 ms = 10 s budget

    while (round++ < MAX_ROUNDS) {
        Sleep(POLL_MS);
        last_H = entropy(sample, SAMPLE_LEN);
        if (round % 4 == 0 || last_H < 7.0) {
            sdk_log("[dump] poll #%d entropy=%.3f", round, last_H);
        }
        if (last_H < 7.0) {
            decrypted = true;
            break;
        }
    }

    if (!decrypted) {
        sdk_log("[dump] WARNING: .text entropy still %.3f after %d rounds — dumping anyway", last_H, round);
    }

    // Dump BEFORE installing patches so text_dump.bin captures the truly
    // decrypted-but-unmodified .text.
    //
    // RED-HERRING NOTE: between 16:56 and 17:30 we chased a 0x0024537c access
    // violation through this bisect. Root cause turned out to be **missing
    // Settings.cfg** — the user had lost their tweaked config (no-movies,
    // video compat flags). Sacred crashed in the intro-video init path. The
    // patches/hooks/text_logger were innocent. Keep this order anyway —
    // pre-patch dump is cheap and useful for offline analysis.
    char path[MAX_PATH];
    resolve_dump_path(path, sizeof(path));
    {
        FILE* f = nullptr;
        fopen_s(&f, path, "wb");
        if (!f) {
            sdk_log("[dump] cannot open '%s' for write", path);
            return 1;
        }
        const unsigned char* base = reinterpret_cast<unsigned char*>(TEXT_VA);
        size_t written = fwrite(base, 1, TEXT_SIZE, f);
        fclose(f);
        sdk_log("[dump] wrote %zu / %zu bytes to %s  (final entropy %.3f) [pre-patch]",
                written, TEXT_SIZE, path, last_H);
    }

    if (decrypted) {
        sdk_log("[dump] decryption detected — installing patches");
        patches::install();
        text_logger::install();
        // sacred_log_mirror::install();  // still disabled (needs SuspendThread)
        // Engine trigger/dialog hooks: (re)install here too, AFTER decryption.
        // The bake worker also calls this (via take_state) but can win the race
        // against decryption and abort on encrypted bytes; this post-decrypt
        // call guarantees they install. Idempotent if the bake already did it.
        runtime_triggers::ensure_hooks_installed();
    } else {
        sdk_log("[dump] entropy never dropped — SKIPPING patches::install()/text_logger::install() to avoid corruption");
    }
    return 0;
}

void start() {
    HANDLE h = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

}} // namespace sdk::dumptext
