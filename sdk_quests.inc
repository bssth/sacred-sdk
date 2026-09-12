// sdk_quests.inc — included by runtime_triggers.cpp, inside namespace
// sdk::runtime_triggers, after sdk_sections.inc. SDK-owned entries in the
// engine's QUEST REGISTRY, so our quests go through the engine's own lifecycle
// instead of an SDK imitation of it.
//
// The registry (MECHANICS §1.3, loader FUN_0046f9b0) is a vector of 0x124-byte
// entries at DAT_00aabf18 (begin) / ..1c (end) / ..20 (capacity):
//
//   +0x000 i32 quest id                 +0x10c i32 QIS_Trigger<id> section index
//   +0x004 char name[64]                +0x110 i32 QIS_OnEnter<id>
//   +0x104 i32 QuestCode start          +0x114 i32 QIS_OnSetUp<id>  (zeroed after it runs)
//   +0x108 i32 QuestCode length         +0x118 i32 QIS_OnExit<id>
//   +0x120 u32 flags: bit0 set up, bit1 done, bit2 entered, bit3 in SetUp,
//                     bit31 re-resolve the five names on load
//                                       +0x11c i32 QIS_OnLose<id>
//
// What an entry buys us (RE_lifecycle_journal.md §3-§6):
//   SetUpQuest   (tag 0x15) -> FUN_0046db20: flags |= 1, run OnSetUp once, run Trigger
//   TriggerQuest (tag 0x14) -> FUN_0048f030: needs bit0; runs Trigger; if that
//                  section runs to its end -> FUN_0046c160 creates the JOURNAL
//                  ENTRY itself (category 3 for ids 1..99, else 4), registers the
//                  compass column, plays the entry fanfare, flags |= 4, runs OnEnter
//   ExitQuest    (0x0f) / LoseQuest (0x36) -> FUN_0046cbe0: journal state 100/101,
//                  the solved / failed look and sound, run OnExit / OnLose
//   WorkFunktion's owner gate: a section whose +0x48 is our quest id stays dead
//   until the quest is set up — the vanilla way to arm a quest's own sections.
//
// This does at run time what the bake route would do to Vectoren.bin, using the
// same block swap sdk_sections.inc already uses for the section table: allocate
// a bigger vector from the engine's own operator new, copy the entries, append
// ours, re-point the three pointers. Reasons to do it here rather than in the
// file: our sections live in the SDK store, not in FunkCode.bin, so a baked
// registry entry could only point at a stub; the quest bodies are Lua, which
// only exists when the SDK is loaded anyway; and nothing on disk changes.
//
// The five QIS indices are resolved from our own section table slot (SDK
// section i sits at index g_sec_vanilla + i), refreshed whenever the section
// table is rebuilt (g_sec_gen). +0x10c may stay 0: TriggerQuest resolves
// "QIS_Trigger<id>" by name on the spot when it is 0 (FUN_0048f030:134-174).
//
// The engine rebuilds the registry from Vectoren.bin on every world load, and
// the savegame loader FUN_00465690 matches saved entries to live ones BY QUEST
// ID and push_backs the ones it cannot find. Both cases are handled the same
// way: quests_inject() rebuilds whenever the live vector is not the one we
// wrote, and absorbs any live entry that carries one of our ids — so a state
// (set up / entered / done) restored from a savegame is kept, never reset.

constexpr uintptr_t QREG_BEGIN_VA = 0x00AABF18;
constexpr uintptr_t QREG_END_VA   = 0x00AABF1C;
constexpr uintptr_t QREG_CAP_VA   = 0x00AABF20;
constexpr size_t    QREG_STRIDE   = 0x124;
constexpr int       SDK_QUEST_MAX = 32;

// registry flags (+0x120)
constexpr uint32_t QF_SETUP   = 1u;
constexpr uint32_t QF_DONE    = 2u;
constexpr uint32_t QF_ENTERED = 4u;

struct SdkQuest { int32_t qid; char name[64]; };
static SdkQuest  g_quest[SDK_QUEST_MAX];
static int       g_quest_n        = 0;
static uintptr_t g_quest_block    = 0;      // our registry block (0 = not injected yet)
static unsigned  g_quest_vanilla  = 0;      // engine entries below ours
static int       g_quest_injected = 0;      // how many of ours are in the block
static unsigned  g_quest_secgen   = 0xFFFFFFFFu;   // g_sec_gen the indices were resolved for

static int quest_slot(int32_t qid) {
    for (int i = 0; i < g_quest_n; i++) if (g_quest[i].qid == qid) return i;
    return -1;
}

// The engine's section index of one of our sections, or 0 (= the null section)
// while the section table is not ours. Our sections are appended in order, so
// SDK section i is table entry g_sec_vanilla + i; the engine's own by-name scan
// (FUN_00460590) would return exactly this, because no vanilla section carries
// a QIS name for an id that is not in the shipped registry.
static int quest_sec_index(const char* name) {
    if (!g_sec_block) return 0;
    int i = sec_find(name);
    if (i < 0 || i >= g_sec_injected) return 0;
    return (int)g_sec_vanilla + i;
}

// Fill +0x10c..+0x11c from the section table. OnSetUp is left alone once the
// quest is set up: the engine zeroes +0x114 after running it (FUN_0046db20:50)
// and writing it back would make a second SetUpQuest run it again.
static void quest_resolve_qis(uint8_t* e, int32_t qid) {
    char nm[80];
    uint32_t flags = *(uint32_t*)(e + 0x120);
    _snprintf_s(nm, _TRUNCATE, "QIS_Trigger%d", qid);
    *(int32_t*)(e + 0x10c) = quest_sec_index(nm);
    _snprintf_s(nm, _TRUNCATE, "QIS_OnEnter%d", qid);
    *(int32_t*)(e + 0x110) = quest_sec_index(nm);
    _snprintf_s(nm, _TRUNCATE, "QIS_OnExit%d", qid);
    *(int32_t*)(e + 0x118) = quest_sec_index(nm);
    _snprintf_s(nm, _TRUNCATE, "QIS_OnLose%d", qid);
    *(int32_t*)(e + 0x11c) = quest_sec_index(nm);
    if (!(flags & QF_SETUP)) {
        _snprintf_s(nm, _TRUNCATE, "QIS_OnSetUp%d", qid);
        *(int32_t*)(e + 0x114) = quest_sec_index(nm);
    }
}

static uint8_t g_quest_saved[SDK_QUEST_MAX][QREG_STRIDE];   // absorbed live entries

static void quests_inject() {
    if (g_quest_n == 0) return;
    __try {
        uintptr_t b = *(uintptr_t*)QREG_BEGIN_VA, e = *(uintptr_t*)QREG_END_VA;
        if (!b || e < b) return;
        unsigned n = (unsigned)((e - b) / QREG_STRIDE);
        if (n < 2) return;                                   // no script loaded yet
        bool ours = (b == g_quest_block) && (n == g_quest_vanilla + (unsigned)g_quest_injected);
        if (ours && g_quest_injected == g_quest_n) {
            if (g_quest_secgen != g_sec_gen) {               // the section table moved
                for (int i = 0; i < g_quest_n; i++)
                    quest_resolve_qis((uint8_t*)(b + (size_t)(g_quest_vanilla + (unsigned)i) * QREG_STRIDE),
                                      g_quest[i].qid);
                g_quest_secgen = g_sec_gen;
                sdk_log("[quests] QIS indices re-resolved for %d quests (section gen %u)",
                        g_quest_n, g_sec_gen);
            }
            return;
        }

        // Pass 1: which live entries are ours (by quest id)? Entry 0 is the
        // engine's null entry and is always kept.
        bool have[SDK_QUEST_MAX] = { false };
        unsigned keep = 0;
        for (unsigned i = 0; i < n; i++) {
            const uint8_t* s = (const uint8_t*)(b + (size_t)i * QREG_STRIDE);
            int k = (i == 0) ? -1 : quest_slot(*(const int32_t*)s);
            if (k >= 0 && !have[k]) { memcpy(g_quest_saved[k], s, QREG_STRIDE); have[k] = true; }
            else if (k < 0) keep++;
        }

        unsigned total = keep + (unsigned)g_quest_n;
        uint8_t* blk = (uint8_t*)kOpNew((size_t)total * QREG_STRIDE);
        if (!blk) { sdk_log("[quests] operator new(%u) failed", (unsigned)(total * QREG_STRIDE)); return; }

        unsigned w = 0;
        for (unsigned i = 0; i < n; i++) {
            const uint8_t* s = (const uint8_t*)(b + (size_t)i * QREG_STRIDE);
            if (i != 0 && quest_slot(*(const int32_t*)s) >= 0) continue;   // ours: appended below
            memcpy(blk + (size_t)w * QREG_STRIDE, s, QREG_STRIDE);
            w++;
        }
        for (int i = 0; i < g_quest_n; i++) {
            uint8_t* d = blk + (size_t)(keep + (unsigned)i) * QREG_STRIDE;
            if (have[i]) memcpy(d, g_quest_saved[i], QREG_STRIDE);         // keep the live state
            else         memset(d, 0, QREG_STRIDE);
            *(int32_t*)d = g_quest[i].qid;
            strncpy_s((char*)(d + 4), 64, g_quest[i].name, _TRUNCATE);
            quest_resolve_qis(d, g_quest[i].qid);
        }

        *(uintptr_t*)QREG_BEGIN_VA = (uintptr_t)blk;
        *(uintptr_t*)QREG_END_VA   = (uintptr_t)(blk + (size_t)total * QREG_STRIDE);
        *(uintptr_t*)QREG_CAP_VA   = (uintptr_t)(blk + (size_t)total * QREG_STRIDE);
        g_quest_block = (uintptr_t)blk; g_quest_vanilla = keep;
        g_quest_injected = g_quest_n; g_quest_secgen = g_sec_gen;
        sdk_log("[quests] registry %u -> %u entries (SDK quests #%u..#%u, %u absorbed)",
                n, total, keep, total - 1,
                (unsigned)(n - keep));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sdk_log("[quests] inject faulted");
    }
}

// Called from heartbeat(), after sections_tick() — the QIS indices follow the
// section table, so the sections must be injected first.
static void quests_tick() {
    if (g_sec_patched) quests_inject();
}

// The live entry for `qid`, or nullptr. Reads the engine's own vector, so it
// sees vanilla quests and whatever state the engine has put in ours.
static uint8_t* quest_entry(int32_t qid, int* index_out) {
    uintptr_t b = *(uintptr_t*)QREG_BEGIN_VA, e = *(uintptr_t*)QREG_END_VA;
    if (!b || e < b) return nullptr;
    unsigned n = (unsigned)((e - b) / QREG_STRIDE);
    for (unsigned i = 0; i < n; i++) {
        uint8_t* s = (uint8_t*)(b + (size_t)i * QREG_STRIDE);
        if (*(int32_t*)s == qid) { if (index_out) *index_out = (int)i; return s; }
    }
    return nullptr;
}

// sacred.quest_register(id, name) -> true
// Puts `id` in the engine's quest registry (or updates the name of one we
// already own), so SetUpQuest / TriggerQuest / ExitQuest / LoseQuest records
// work on it exactly as they do on a shipped quest. The entry appears on the
// next heartbeat and is re-created after every world load and savegame load.
// Sections named QIS_OnSetUp<id>, QIS_Trigger<id>, QIS_OnEnter<id>,
// QIS_OnExit<id> and QIS_OnLose<id> are picked up automatically.
static int l_sacred_quest_register(lua_State* L) {
    int32_t qid = (int32_t)luaL_checkinteger(L, 1);
    const char* name = luaL_optstring(L, 2, "SDK quest");
    if (qid <= 0) return luaL_error(L, "quest_register: id must be positive");
    int i = quest_slot(qid);
    bool fresh = (i < 0);
    if (fresh) {
        if (g_quest_n >= SDK_QUEST_MAX) return luaL_error(L, "quest_register: too many SDK quests");
        i = g_quest_n;
        g_quest[i].qid = qid;
    }
    strncpy_s(g_quest[i].name, name, _TRUNCATE);
    // The journal suppressor (hide_vanilla_quests) keeps every entry whose id
    // the SDK claims; a registry quest of ours is one of them.
    sdk_qid_add((uint32_t)qid);
    if (fresh) { g_quest_n++; sdk_log("[quests] register %d '%s'", qid, g_quest[i].name); }
    quests_inject();                      // now, so a SetUpQuest this tick finds it
    lua_pushboolean(L, 1);
    return 1;
}

// sacred.quest_state(id) -> table | nil
// { index, name, flags, setup, entered, done, trigger, on_enter, on_setup,
//   on_exit, on_lose, sdk } — the LIVE registry entry, whatever put it there.
static int l_sacred_quest_state(lua_State* L) {
    int32_t qid = (int32_t)luaL_checkinteger(L, 1);
    int idx = -1;
    uint8_t* e = nullptr;
    __try { e = quest_entry(qid, &idx); } __except (EXCEPTION_EXECUTE_HANDLER) { e = nullptr; }
    if (!e) { lua_pushnil(L); return 1; }
    uint32_t flags = 0;
    char name[64] = { 0 };
    int32_t qis[5] = { 0, 0, 0, 0, 0 };
    __try {
        flags = *(uint32_t*)(e + 0x120);
        memcpy(name, e + 4, 63);
        qis[0] = *(int32_t*)(e + 0x10c); qis[1] = *(int32_t*)(e + 0x110);
        qis[2] = *(int32_t*)(e + 0x114); qis[3] = *(int32_t*)(e + 0x118);
        qis[4] = *(int32_t*)(e + 0x11c);
    } __except (EXCEPTION_EXECUTE_HANDLER) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushinteger(L, idx);                       lua_setfield(L, -2, "index");
    lua_pushstring(L, name);                       lua_setfield(L, -2, "name");
    lua_pushinteger(L, (lua_Integer)flags);        lua_setfield(L, -2, "flags");
    lua_pushboolean(L, (flags & QF_SETUP) != 0);   lua_setfield(L, -2, "setup");
    lua_pushboolean(L, (flags & QF_ENTERED) != 0); lua_setfield(L, -2, "entered");
    lua_pushboolean(L, (flags & QF_DONE) != 0);    lua_setfield(L, -2, "done");
    lua_pushinteger(L, qis[0]);                    lua_setfield(L, -2, "trigger");
    lua_pushinteger(L, qis[1]);                    lua_setfield(L, -2, "on_enter");
    lua_pushinteger(L, qis[2]);                    lua_setfield(L, -2, "on_setup");
    lua_pushinteger(L, qis[3]);                    lua_setfield(L, -2, "on_exit");
    lua_pushinteger(L, qis[4]);                    lua_setfield(L, -2, "on_lose");
    lua_pushboolean(L, quest_slot(qid) >= 0);      lua_setfield(L, -2, "sdk");
    return 1;
}

// sacred.quest_flags(id [, set, clear]) -> flags | nil
// Reads, and optionally edits, the registry flags of one of OUR quests (bit0
// set up, bit1 done, bit2 entered). Clearing done|entered is what lets a test
// quest be played again without restarting the game. Vanilla ids are read-only.
static int l_sacred_quest_flags(lua_State* L) {
    int32_t qid = (int32_t)luaL_checkinteger(L, 1);
    uint32_t set = (uint32_t)luaL_optinteger(L, 2, 0);
    uint32_t clr = (uint32_t)luaL_optinteger(L, 3, 0);
    if ((set || clr) && quest_slot(qid) < 0)
        return luaL_error(L, "quest_flags: %d is not an SDK quest", (int)qid);
    uint32_t f = 0;
    bool ok = false;
    __try {
        uint8_t* e = quest_entry(qid, nullptr);
        if (e) {
            f = *(uint32_t*)(e + 0x120);
            if (set || clr) { f = (f & ~clr) | set; *(uint32_t*)(e + 0x120) = f; }
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)f);
    return 1;
}
