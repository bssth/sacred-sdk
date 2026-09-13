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

// ---------------------------------------------------------------------------
// 2.29 change 4 / 2.30 change 4 — chat crash.
// FUN_0084C0D3 is the engine's wcsncpy(dst, src, count): copy up to `count`
// UTF-16 units, stop at the terminator, zero-fill the rest. A malformed network
// chat message hands it a garbage `src` and the copy loop faults. Thorium puts
// an IsBadReadPtr(src, count) check in front of the loop.
//
// Site (6 bytes, whole instructions):
//   0084C0E1  74 28          je   0084C10B        ; count == 0 -> return
//   0084C0E3  8B 54 24 10    mov  edx, [esp+10h]  ; edx = src
//   0084C0E7                 (loop head: mov ax, [edx])
// becomes  E9 <stub> 90.
//
// Stub. Both jump-back targets are derived from the site, not hardcoded: the
// return is site+2+int8(0x28) and the loop head is site+6.
//    0  0F 84 rel32        je   return             ; flags still from `test ecx, ecx`
//    6  8B 54 24 10        mov  edx, [esp+10h]     ; replayed from the site
//   10  51 52              push ecx; push edx      ; IsBadReadPtr clobbers both
//   12  51 52              push ecx; push edx      ; (lp = src, ucb = count)
//   14  FF 15 abs32        call [kernel32!IsBadReadPtr]
//   20  5A 59              pop  edx; pop ecx
//   22  85 C0              test eax, eax
//   24  0F 84 rel32        je   loop_head          ; readable -> run the copy
//   30  66 C7 07 00 00     mov  word ptr [edi], 0  ; unreadable -> empty string
//   35  E9 rel32           jmp  return
//
// One deliberate difference from Thorium: on the unreadable path his stub
// returned with `dst` untouched. Two of the three callers copy into an
// uninitialised stack buffer, so that left garbage with no terminator for the
// next string routine to run off the end of. Writing an empty string first is
// strictly safer and changes nothing on the readable path.
//
// Default OFF until a live run confirms it: FUN_0084C0D3 is not chat-only, its
// other two callers copy 8-unit strings, so a mistake here would show up well
// beyond multiplayer chat.
// ---------------------------------------------------------------------------
static const ImportRef k_chat_imports[] = {
    { "kernel32.dll", "IsBadReadPtr" },
};
static const uint8_t k_chat_stub[] = {
    0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,        // je   return
    0x8B, 0x54, 0x24, 0x10,                    // mov  edx, [esp+10h]
    0x51, 0x52,                                // push ecx; push edx
    0x51, 0x52,                                // push ecx; push edx
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,        // call [IsBadReadPtr]
    0x5A, 0x59,                                // pop edx; pop ecx
    0x85, 0xC0,                                // test eax, eax
    0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,        // je   loop_head
    0x66, 0xC7, 0x07, 0x00, 0x00,              // mov  word ptr [edi], 0
    0xE9, 0x00, 0x00, 0x00, 0x00,              // jmp  return
};
static const Fixup k_chat_stub_fx[] = {
    {  2, Fix::Rel32SiteJcc8, 0 },   // je   -> site + 2 + 0x28
    { 16, Fix::Abs32IatSlot,  0 },   // call -> &IAT[IsBadReadPtr]
    { 26, Fix::Rel32SitePlus, 6 },   // je   -> site + 6 (loop head)
    { 36, Fix::Rel32SiteJcc8, 0 },   // jmp  -> site + 2 + 0x28
};
static const Stub k_chat_stubs[] = {
    { 0, (uint16_t)sizeof(k_chat_stub), k_chat_stub, k_chat_stub_fx, 4, 0 },
};
static const uint8_t k_chat_expect[] = { 0x74, 0x28, 0x8B, 0x54, 0x24, 0x10 };
static const uint8_t k_chat_bytes [] = { 0xE9, 0x00, 0x00, 0x00, 0x00, 0x90 };
static const Fixup   k_chat_fx[] = {
    { 1, Fix::Rel32ToStub, 0 },
};
static const Site k_chat_sites[] = {
    { 0x0084C0E1u, 6, k_chat_expect, k_chat_bytes, k_chat_fx, 1 },
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

{ "fix_chat_crash", "Chat: survive a bad string pointer", "fixes",
  "Thorium (SacredVault), unofficial patch 2.29 change 4 / 2.30 change 4", nullptr,
  false, k_chat_imports, 1, k_chat_stubs, 1, k_chat_sites, 1 },

#endif // SDK_PATCH_ROWS
