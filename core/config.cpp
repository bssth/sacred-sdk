// core/config.cpp — see config.h.
#include "../sdk.h"
#include "config.h"
#include <cstring>
#include <cstdlib>

namespace sdk { namespace config {

namespace {

constexpr int MAX_ENTRIES = 320;
constexpr int KEY_MAX     = 96;
constexpr int VAL_MAX     = 160;

struct Entry {
    char key[KEY_MAX];   // lowercased, "section.key" or bare "key"
    char val[VAL_MAX];
};

Entry            g_tab[MAX_ENTRIES];
int              g_n = 0;
bool             g_inited = false;
char             g_game_dir[MAX_PATH] = {0};
char             g_status[192]        = "config: not loaded";
CRITICAL_SECTION g_cs;
bool             g_cs_init = false;

void lower_inplace(char* s) {
    for (; *s; ++s) if (*s >= 'A' && *s <= 'Z') *s = char(*s - 'A' + 'a');
}

void trim(char* s) {
    char* p = s;
    while (*p == ' ' || *p == '\t') ++p;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = 0;
}

void put(const char* key, const char* val) {
    if (g_n >= MAX_ENTRIES) return;
    char k[KEY_MAX];
    strncpy_s(k, sizeof(k), key, _TRUNCATE);
    lower_inplace(k);
    for (int i = 0; i < g_n; ++i) {              // last writer wins
        if (strcmp(g_tab[i].key, k) == 0) {
            strncpy_s(g_tab[i].val, sizeof(g_tab[i].val), val, _TRUNCATE);
            return;
        }
    }
    strncpy_s(g_tab[g_n].key, sizeof(g_tab[g_n].key), k, _TRUNCATE);
    strncpy_s(g_tab[g_n].val, sizeof(g_tab[g_n].val), val, _TRUNCATE);
    ++g_n;
}

const char* find(const char* section, const char* key) {
    char q[KEY_MAX];
    if (section && *section) {
        _snprintf_s(q, sizeof(q), _TRUNCATE, "%s.%s", section, key);
        lower_inplace(q);
        for (int i = 0; i < g_n; ++i)
            if (strcmp(g_tab[i].key, q) == 0) return g_tab[i].val;
    }
    // Fall back to the bare key: a sectionless sdk.ini (or a key written above
    // the first [section] header) must keep working exactly as it used to.
    strncpy_s(q, sizeof(q), key, _TRUNCATE);
    lower_inplace(q);
    for (int i = 0; i < g_n; ++i)
        if (strcmp(g_tab[i].key, q) == 0) return g_tab[i].val;
    return nullptr;
}

// mode: 0 = sdk.ini (`key=value`, `[section]`), 1 = Settings.cfg (`KEY : VALUE`)
int parse_file(const char* path, int mode, const char* forced_section) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return -1;
    char line[512];
    char section[64] = {0};
    if (forced_section) strncpy_s(section, sizeof(section), forced_section, _TRUNCATE);
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0] == ';' || line[0] == '#') continue;

        if (mode == 0 && line[0] == '[') {
            char* end = strchr(line, ']');
            if (end) {
                *end = 0;
                strncpy_s(section, sizeof(section), line + 1, _TRUNCATE);
                trim(section);
            }
            continue;
        }

        char* sep = (mode == 0) ? strchr(line, '=') : strchr(line, ':');
        if (!sep) continue;
        *sep = 0;
        char* k = line;
        char* v = sep + 1;
        trim(k); trim(v);
        if (!k[0]) continue;

        char full[KEY_MAX];
        if (section[0]) _snprintf_s(full, sizeof(full), _TRUNCATE, "%s.%s", section, k);
        else            strncpy_s(full, sizeof(full), k, _TRUNCATE);
        put(full, v);
        ++n;
    }
    fclose(f);
    return n;
}

void resolve_game_dir() {
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = 0;
    strncpy_s(g_game_dir, sizeof(g_game_dir), exe, _TRUNCATE);
}

int load_all() {
    g_n = 0;
    if (!g_game_dir[0]) resolve_game_dir();

    char p_ini[MAX_PATH], p_cfg[MAX_PATH];
    _snprintf_s(p_ini, sizeof(p_ini), _TRUNCATE, "%s\\sdk.ini", g_game_dir);
    _snprintf_s(p_cfg, sizeof(p_cfg), _TRUNCATE, "%s\\Settings.cfg", g_game_dir);

    int a = parse_file(p_ini, 0, nullptr);
    int b = parse_file(p_cfg, 1, "game");

    _snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
                "sdk.ini: %s, Settings.cfg: %s, %d keys",
                a < 0 ? "missing" : "loaded", b < 0 ? "missing" : "loaded", g_n);
    sdk_log("[cfg] %s (dir=%s)", g_status, g_game_dir);
    return g_n;
}

} // namespace

void init() {
    if (g_inited) return;
    g_inited = true;
    if (!g_cs_init) { InitializeCriticalSection(&g_cs); g_cs_init = true; }
    load_all();
}

int reload() {
    if (!g_inited) { init(); return g_n; }
    EnterCriticalSection(&g_cs);
    int n = load_all();
    LeaveCriticalSection(&g_cs);
    return n;
}

bool has(const char* section, const char* key) { return find(section, key) != nullptr; }

const char* get_str(const char* section, const char* key, const char* def) {
    const char* v = find(section, key);
    return v ? v : def;
}

int get_int(const char* section, const char* key, int def) {
    const char* v = find(section, key);
    if (!v || !*v) return def;
    return (int)strtol(v, nullptr, 0);      // base 0 => understands 0x…
}

uint32_t get_hex(const char* section, const char* key, uint32_t def) {
    const char* v = find(section, key);
    if (!v || !*v) return def;
    return (uint32_t)strtoul(v, nullptr, 0);
}

bool get_bool(const char* section, const char* key, bool def) {
    const char* v = find(section, key);
    if (!v || !*v) return def;
    if (_stricmp(v, "true") == 0 || _stricmp(v, "yes") == 0 || _stricmp(v, "on") == 0) return true;
    if (_stricmp(v, "false") == 0 || _stricmp(v, "no") == 0 || _stricmp(v, "off") == 0) return false;
    return strtol(v, nullptr, 0) != 0;
}

const char* game_setting(const char* key, const char* def) {
    const char* v = find("game", key);
    return v ? v : def;
}

int         count()    { return g_n; }
const char* status()   { return g_status; }
const char* game_dir() { if (!g_game_dir[0]) resolve_game_dir(); return g_game_dir; }

}} // namespace sdk::config
