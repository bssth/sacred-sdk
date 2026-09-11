// core/config.h — the one config reader.
//
// Replaces the ad-hoc parsers that had grown independently:
//   * hooks.cpp        load_config()      — file-local static, fills ForceConfig
//   * dllmain.cpp      sdk_ini_flag()     — file-local static, not even in sdk.h
// Still to migrate: the Settings.cfg LANGUAGE reader inlined in patches.cpp's
// global.res path resolver — game_setting("LANGUAGE", "us") is its replacement.
//
// Two files, two syntaxes, one store:
//   sdk.ini        `key=value`, optional `[section]` headers
//   Settings.cfg   `KEY : VALUE`  (Sacred's own config, exposed under "game")
//
// BACK-COMPAT, load-bearing: the old sdk.ini parser ignored `[section]` lines as
// "no '=' in it" noise, so the shipped sdk.ini works purely by flat scan. Lookup
// therefore tries "section.key" FIRST and then the bare "key", which keeps every
// existing user's sdk.ini working whether or not it has section headers.
//
// No heap, no STL: a fixed table in .bss, like LogRing. init() runs in DllMain
// before any worker thread exists, so reads need no lock; reload() takes one.
#pragma once
#include <cstdint>
#include <cstddef>

namespace sdk { namespace config {

// Parse sdk.ini + Settings.cfg from the game directory. Idempotent.
void init();

// Re-read both files. Returns the number of keys now held. For the overlay.
int reload();

bool        get_bool(const char* section, const char* key, bool def);
int         get_int (const char* section, const char* key, int def);
uint32_t    get_hex (const char* section, const char* key, uint32_t def);  // accepts 0x…
const char* get_str (const char* section, const char* key, const char* def);
bool        has     (const char* section, const char* key);

// Sacred's own Settings.cfg, e.g. game_setting("LANGUAGE", "us").
// ReBorn's configurator writes SR_HD_ENABLE / SR_HD_WIDTH / SR_HD_HEIGHT here,
// which is why we read this file at all rather than only our own ini.
const char* game_setting(const char* key, const char* def);

int         count();
const char* status();
const char* game_dir();   // directory containing Sacred.exe, no trailing slash

}} // namespace sdk::config
