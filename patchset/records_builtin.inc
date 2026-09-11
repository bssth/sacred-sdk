// patchset/records_builtin.inc — the built-in engine patches.
//
// Ports of Thorium's (SacredVault) unofficial Sacred patch 2.29 / 2.30. His
// published listings give the original and replacement x86 for the GERMAN 2.28
// build; the addresses below are the corresponding sites in the Steam/GOG
// "MASTER Oct 13 2006" build, obtained by relocating through the build delta map
// (sdk/re/py/x86/reloc_build.py) and then verified byte for byte against
// sdk/Sacred_decrypted.exe — in every case our vanilla bytes match the "Alter
// Code" of his listing exactly. The same addresses are pins in
// engine/build_profile.cpp, so a live run proves them again before we write.
//
// Included TWICE by patchset.cpp: once with SDK_PATCH_DATA defined to emit the
// byte/site tables, once with SDK_PATCH_ROWS defined to emit the Record rows
// inside the array initializer. (A single-pass include cannot work: an array
// initializer body may not contain declarations.)

#ifdef SDK_PATCH_DATA

// ---------------------------------------------------------------------------
// 2.30 change 7 — do not blank the screen on WM_ACTIVATEAPP.
// Alt-tabbing away and back made Sacred clear its own frame. The `je` that took
// the clear path is simply removed.
// ---------------------------------------------------------------------------
static const uint8_t k_act_expect[] = { 0x74, 0x3A };
static const uint8_t k_act_bytes [] = { 0x90, 0x90 };
static const Site k_act_sites[] = {
    { 0x00812D8Cu, 2, k_act_expect, k_act_bytes, nullptr, 0 },
};

// ---------------------------------------------------------------------------
// 2.30 change 9 — drop the console's "Error! Try HELP for help..." reply.
// The conditional jump that skipped the error path becomes unconditional, so an
// unrecognised command is silently ignored instead of printing an error. This is
// the prerequisite for hanging our own command handler off the console.
//   0F 85 rel32  (jne target)  ->  E9 rel32' (jmp same target) + one NOP
// The target is decoded from the ORIGINAL bytes at apply time (Rel32SiteJcc32),
// so the record carries no second hardcoded address.
// ---------------------------------------------------------------------------
static const uint8_t k_cerr_expect[] = { 0x0F, 0x85, 0xF8, 0x01, 0x00, 0x00 };
static const uint8_t k_cerr_bytes [] = { 0xE9, 0x00, 0x00, 0x00, 0x00, 0x90 };
static const Fixup   k_cerr_fx[] = {
    { 1, Fix::Rel32SiteJcc32, 0 },
};
static const Site k_cerr_sites[] = {
    { 0x0061981Bu, 6, k_cerr_expect, k_cerr_bytes, k_cerr_fx, 1 },
};

// ---------------------------------------------------------------------------
// 2.30 change 4 — allow more than one Sacred instance (byte-patch variant).
// The engine creates the SACRED_INSTANCE mutex, then checks GetLastError for
// ERROR_ALREADY_EXISTS and shows "Sacred is already running!". Two edits remove
// the check. NOTE: an IAT hook on CreateMutexA is the better mechanism — it does
// not touch .text at all — so this record ships default-off and exists for
// parity testing and for anyone who wants the engine-side behaviour.
// ---------------------------------------------------------------------------
static const uint8_t k_mi1_expect[] = { 0x74, 0x0D };
static const uint8_t k_mi1_bytes [] = { 0x90, 0x90 };
static const uint8_t k_mi2_expect[] = { 0x75, 0x40 };
static const uint8_t k_mi2_bytes [] = { 0xEB, 0x40 };
static const Site k_mi_sites[] = {
    { 0x00817B4Eu, 2, k_mi1_expect, k_mi1_bytes, nullptr, 0 },
    { 0x00817B5Bu, 2, k_mi2_expect, k_mi2_bytes, nullptr, 0 },
};

// ---------------------------------------------------------------------------
// 2.29 change 6 / 2.30 change 6 — debugger-freeze fix, surgical form.
// FUN_00811440 busy-waits on SetForegroundWindow/SetFocus; under a debugger the
// call legitimately fails and the loop never converges, hanging the machine.
// Thorium removes the `je` so the function returns AL=0 immediately.
//
// CONFLICT: our legacy patches.cpp patch6 stubs the whole enclosing function to
// return AL=1 instead, because a comment there records that returning 0 once
// produced an access violation at 0x0064537c. Both cannot be applied. patch6
// stays the default; this record is off unless someone opts in.
// ---------------------------------------------------------------------------
static const uint8_t k_dbg_expect[] = { 0x74, 0x09 };
static const uint8_t k_dbg_bytes [] = { 0x90, 0x90 };
static const Site k_dbg_sites[] = {
    { 0x0081145Fu, 2, k_dbg_expect, k_dbg_bytes, nullptr, 0 },
};

#endif // SDK_PATCH_DATA

#ifdef SDK_PATCH_ROWS

{ "fix_no_clear_on_activate", "Keep the frame on alt-tab", "fixes",
  "Thorium (SacredVault), unofficial patch 2.30 change 7", nullptr,
  true,  nullptr, 0, nullptr, 0, k_act_sites,  1 },

{ "fix_console_error_msg", "Console: no 'Error! Try HELP'", "fixes",
  "Thorium (SacredVault), unofficial patch 2.30 change 9", nullptr,
  false, nullptr, 0, nullptr, 0, k_cerr_sites, 1 },

{ "fix_multi_instance_bytes", "Multi-instance (byte patch)", "fixes",
  "Thorium (SacredVault), unofficial patch 2.30 change 4", "hooks.multi_instance",
  false, nullptr, 0, nullptr, 0, k_mi_sites,   2 },

{ "fix_debugger_freeze", "Debugger-freeze fix (surgical)", "fixes",
  "Thorium (SacredVault), unofficial patch 2.29 change 6", "legacy_patch6",
  false, nullptr, 0, nullptr, 0, k_dbg_sites,  1 },

#endif // SDK_PATCH_ROWS
