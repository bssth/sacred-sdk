// engine/console.h — the game's own developer console, as an SDK command surface.
//
// Sacred ships a working console window (UI_WND_CONSOLE) in the retail build.
// Nothing has to be unlocked to open it: the engine's in-game key handler
// (FUN_00618090) opens it on the character '`' (0x60) or 0xDC while Ctrl is not
// held, and the console closes itself on the same keys or Esc. The character
// comes from WM_CHAR, so it depends on the active keyboard layout — on a
// Russian layout the '`' key types 'ё' and does nothing; switch to English.
// The DAT_0182ee38 & 0x2000000 flag gates only the CHEAT command family.
//
// Enter hands the line to the command dispatcher FUN_00615F30,
// `__thiscall(cEngine*, const char* line) -> AL handled`. When that returns 0
// the engine prints "Error! Try HELP for help...". We detour the dispatcher:
// a line whose first word is `sdk` is ours, everything else goes to the
// original untouched.
//
// Output: one line is one cEventUI_string (0x174 bytes, text at +0x74, 256
// bytes) with code 0x26 addressed to UI_WND_CONSOLE, handed to
// cKernel::receive_event — exactly what the HELP command does. The window keeps
// only its last 8 lines, so a command's reply should fit in 8.
#pragma once
#include <cstdint>

namespace sdk { namespace engine { namespace console {

// Post-decrypt. Requires a TRUSTED build profile; `[console] sdk_commands=0`
// leaves the dispatcher untouched. Idempotent.
void install();
bool installed();

// Append one line to the console window. ENGINE THREAD ONLY — call it from a
// command handler, which is where the vanilla commands print from too.
// Text longer than 255 bytes is cut. Returns false if the engine call faulted.
bool print(const char* text);
bool print_f(const char* fmt, ...);

// Register `sdk <name> [args]`. `help` is one short line for `sdk help`.
// Fixed table; returns false when full or the name is taken.
using Handler = void (*)(const char* args);
bool add(const char* name, const char* help, Handler fn);

}}} // namespace sdk::engine::console
