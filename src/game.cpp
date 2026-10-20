#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "game.h"
#include "settings.h"
#include "entity_names.h"
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>

void* g_Client;
void* g_Engine;
void* g_EntList;
void* g_Trace;
void* g_Surface;
void* g_Panel;
void* g_VGuiInput;
void* g_ModelInfo;
void* g_ClientMode;
void* g_Cvar;

int o_ModelIndex = -1;
int o_Flags = -1, o_NpcLife = -1, o_Velocity = -1;
int o_Health, o_Origin, o_TeamNum, o_Mins, o_Maxs, o_ViewOffset, o_LifeState;
int o_Armor = -1;
int o_ActiveWeapon = -1;
int o_PunchAngle = -1, o_PunchAngleVel = -1;

const AimExcludeEntry kAimExcludeEntries[] = {
    { AX_Alyx,            "alyx",              L"Alyx" },
    { AX_Barney,          "barney",            L"Barney" },
    { AX_Kleiner,         "kleiner",           L"Kleiner" },
    { AX_Eli,             "eli",               L"Eli" },
    { AX_Mossman,         "mossman",           L"Mossman" },
    { AX_Monk,            "monk",              L"Father Grigori" },
    { AX_Dog,             "dog",               L"Dog" },
    { AX_Vortigaunt,      "vortigaunt",        L"Vortigaunt" },
    { AX_Citizen,         "citizen",           L"Citizen / Rebel" },
    { AX_Gman,            "gman",              L"G-Man" },
    { AX_Rpg,             "rpg",               L"RPG missile" },
    { AX_CombineBall,     "combineball",       L"Combine ball" },
    { AX_CrossbowBolt,    "crossbow",          L"Crossbow bolt" },
    { AX_Grenade,         "grenade",           L"Grenade" },
    { AX_HunterFlechette, "flechette",         L"Hunter flechette" },
    { AX_AR2Orb,          "ar2",               L"AR2 energy orb" },
    { AX_Ragdoll,         "ragdoll",           L"Ragdoll" },
    { AX_Zombie,          "zombie",            L"Zombie" },
    { AX_Headcrab,        "headcrab",          L"Headcrab" },
    { AX_Antlion,        "antlion",           L"Antlion" },
    { AX_Combine,         "combine",           L"Combine soldier" },
    { AX_Metrocop,        "metropolice",       L"Metrocop" },
    { AX_Manhack,         "manhack",           L"Manhack" },
    { AX_Hunter,          "hunter",            L"Hunter" },
    { AX_Barnacle,        "barnacle",          L"Barnacle" },
    { AX_Scanner,         "scanner",           L"Scanner" },
    { AX_Turret,          "turret",            L"Turret" },
};
const int kAimExcludeEntryCount = static_cast<int>(sizeof(kAimExcludeEntries) / sizeof(kAimExcludeEntries[0]));

bool g_MenuOpen;
bool g_CmdReset;
bool g_EjectRequested = false;
HINSTANCE g_Self;

void requestEject() {
    g_EjectRequested = true;
    g_MenuOpen = false;

    if (g_Engine)
        VCall<void, const char*>(g_Engine, 7, "cl_mouseenable 1");
}

namespace {
using RecvVarProxyFn = void (__cdecl*)(const void*, void*, void*);
struct RecvProxyDataPrefix {
    const void* recvProp;
    int intValue; /* DVariant payload follows the RecvProp pointer. */
};
static RecvProp* g_MoveTypeRecvProp = nullptr;
static void* g_OriginalMoveTypeProxy = nullptr;
static volatile LONG g_LocalMoveType = -1;
static constexpr int kMoveTypeLadder = 9;  

static void __cdecl hookedMoveTypeProxy(const void* data, void* entity, void* out) {
    RecvVarProxyFn original = reinterpret_cast<RecvVarProxyFn>(g_OriginalMoveTypeProxy);
    if (original) original(data, entity, out);

    if (!data || !entity || !g_EntList || !g_Engine) return;
    const RecvProxyDataPrefix* prefix = reinterpret_cast<const RecvProxyDataPrefix*>(data);
    __try {
        void* local = Ent(LocalIndex());
        if (local && entity == local)
            InterlockedExchange(&g_LocalMoveType, prefix->intValue);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
}

bool installLocalMoveTypeHook() {
    if (g_MoveTypeRecvProp && g_MoveTypeRecvProp->m_ProxyFn == reinterpret_cast<void*>(hookedMoveTypeProxy))
        return true;
    RecvProp* prop = netvar::FindProp("DT_BaseEntity", "movetype");
    if (!prop || !prop->m_ProxyFn) return false;
    g_OriginalMoveTypeProxy = prop->m_ProxyFn;
    g_MoveTypeRecvProp = prop;
    InterlockedExchange(&g_LocalMoveType, -1);
    prop->m_ProxyFn = reinterpret_cast<void*>(hookedMoveTypeProxy);
    return true;
}

void removeLocalMoveTypeHook() {
    if (g_MoveTypeRecvProp &&
        g_MoveTypeRecvProp->m_ProxyFn == reinterpret_cast<void*>(hookedMoveTypeProxy)) {
        g_MoveTypeRecvProp->m_ProxyFn = g_OriginalMoveTypeProxy;
    }
    g_MoveTypeRecvProp = nullptr;
    g_OriginalMoveTypeProxy = nullptr;
    InterlockedExchange(&g_LocalMoveType, -1);
}

int getLocalMoveType() {
    return static_cast<int>(InterlockedCompareExchange(&g_LocalMoveType, 0, 0));
}

bool isLocalOnLadder() {
    if (!InGame()) {
        InterlockedExchange(&g_LocalMoveType, -1);
        return false;
    }
    return getLocalMoveType() == kMoveTypeLadder;
}

const wchar_t* const kBoneNames[kBoneCount] = {L"Head", L"Chest", L"Stomach", L"Pelvis", L"Center"};

void* getIface(const char* module, const char* name) {
    HMODULE m = GetModuleHandleA(module);
    if (!m) return nullptr;
    auto fn = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(m, "CreateInterface"));
    return fn ? fn(name, nullptr) : nullptr;
}

namespace {
// Source 2013 ICvar::FindVar uses vtable slot 12

static constexpr int kICvarFindVarSlot = 12;
static constexpr size_t kConCommandBaseSizeX86 = 0x18;
static constexpr size_t kConVarFlagsOffsetX86 = 0x14;
static constexpr size_t kConVarParentOffsetX86 = 0x1C;
static constexpr size_t kConVarStringOffsetX86 = 0x24;
static constexpr int kFcvarCheat = 1 << 14;
static volatile LONG g_FullBrightFlagBypassCount = 0;

class DirectIConVar {
public:
    virtual void SetValue(const char*) = 0;
    virtual void SetValue(float) = 0;
    virtual void SetValue(int) = 0;
};

static void* findConVarObject(const char* name) {
    if (!g_Cvar || !name || !*name) return nullptr;
    return VCall<void*, const char*>(g_Cvar, kICvarFindVarSlot, name);
}

static DirectIConVar* getConVarSetter(void* convar) {
    if (!convar) return nullptr;
#if defined(_M_IX86)
    // The secondary IConVar vptr is at +0x18
    return reinterpret_cast<DirectIConVar*>(reinterpret_cast<uint8_t*>(convar) + kConCommandBaseSizeX86);
#else
    return nullptr;
#endif
}
}

bool setClientFullBrightDirect(int value) {
    if ((value != 0 && value != 1) || !g_Cvar) return false;
#if defined(_M_IX86)
    bool updated = false;
    __try {
        char* convar = reinterpret_cast<char*>(findConVarObject("mat_fullbright"));
        DirectIConVar* setter = getConVarSetter(convar);
        if (convar && setter) {
            char* parent = *reinterpret_cast<char**>(convar + kConVarParentOffsetX86);
            if (!parent) parent = convar;

            int* childFlags = reinterpret_cast<int*>(convar + kConVarFlagsOffsetX86);
            int* parentFlags = reinterpret_cast<int*>(parent + kConVarFlagsOffsetX86);
            const int oldChildFlags = *childFlags;
            const int oldParentFlags = *parentFlags;

            // Restore the flags in __finally, even if the engine throws

            __try {
                *childFlags = oldChildFlags & ~kFcvarCheat;
                if (parentFlags != childFlags)
                    *parentFlags = oldParentFlags & ~kFcvarCheat;
                setter->SetValue(value);
                updated = true;
                InterlockedIncrement(&g_FullBrightFlagBypassCount);
            } __finally {
                if (parentFlags != childFlags)
                    *parentFlags = oldParentFlags;
                *childFlags = oldChildFlags;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        updated = false;
    }
    if (!updated) return false;

    char actual[32]{};
    if (!getConVarStringDirect("mat_fullbright", actual, sizeof(actual))) return false;
    char* end = nullptr;
    const long parsed = strtol(actual, &end, 10);
    return end != actual && parsed == value;
#else
    return false;
#endif
}

bool setClientConVarStringDirect(const char* name, const char* value, bool allowLocalCheatFlag) {
    if (!name || !*name || !value || !g_Cvar) return false;
#if defined(_M_IX86)
    bool updated = false;
    __try {
        char* convar = reinterpret_cast<char*>(findConVarObject(name));
        DirectIConVar* setter = getConVarSetter(convar);
        if (convar && setter) {
            if (allowLocalCheatFlag) {

                char* parent = *reinterpret_cast<char**>(convar + kConVarParentOffsetX86);
                if (!parent) parent = convar;
                int* childFlags = reinterpret_cast<int*>(convar + kConVarFlagsOffsetX86);
                int* parentFlags = reinterpret_cast<int*>(parent + kConVarFlagsOffsetX86);
                const int oldChildFlags = *childFlags;
                const int oldParentFlags = *parentFlags;
                __try {
                    *childFlags = oldChildFlags & ~kFcvarCheat;
                    if (parentFlags != childFlags) *parentFlags = oldParentFlags & ~kFcvarCheat;
                    setter->SetValue(value);
                    updated = true;
                } __finally {
                    if (parentFlags != childFlags) *parentFlags = oldParentFlags;
                    *childFlags = oldChildFlags;
                }
            } else {
                setter->SetValue(value);
                updated = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        updated = false;
    }
    if (!updated) return false;
    char actual[128]{};
    if (!getConVarStringDirect(name, actual, sizeof(actual))) return false;
    return strcmp(actual, value) == 0;
#else
    (void)allowLocalCheatFlag;
    return false;
#endif
}

bool setClientConVarIntDirect(const char* name, int value) {
    if (!name || !*name || !g_Cvar) return false;
#if defined(_M_IX86)
    bool updated = false;
    __try {
        char* convar = reinterpret_cast<char*>(findConVarObject(name));
        DirectIConVar* setter = getConVarSetter(convar);
        if (convar && setter) {

            setter->SetValue(value);
            updated = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        updated = false;
    }
    if (!updated) return false;
    char actual[32]{};
    if (!getConVarStringDirect(name, actual, sizeof(actual))) return false;
    char* end = nullptr;
    const long parsed = strtol(actual, &end, 10);
    while (end && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    return end != actual && end && *end == '\0' && parsed == value;
#else
    return false;
#endif
}

long getClientFullBrightFlagBypassCount() {
    return InterlockedCompareExchange(&g_FullBrightFlagBypassCount, 0, 0);
}

bool getConVarStringDirect(const char* name, char* out, size_t outSize) {
    if (!out || outSize < 2) return false;
    out[0] = '\0';
    __try {
        char* convar = reinterpret_cast<char*>(findConVarObject(name));
        if (!convar) return false;
#if defined(_M_IX86)

        char* parent = *reinterpret_cast<char**>(convar + kConVarParentOffsetX86);
        if (!parent) parent = convar;
        const char* value = *reinterpret_cast<const char**>(parent + kConVarStringOffsetX86);
        if (!value) return false;
        strncpy_s(out, outSize, value, _TRUNCATE);
        return out[0] != '\0';
#else
        return false;
#endif
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
        return false;
    }
}

static void copyVisible(char* out, size_t n, const char* src, size_t srcN) {
    size_t j = 0;
    for (size_t i = 0; i < srcN && src[i] && j + 1 < n; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c < 32) continue;
        out[j++] = (char)c;
    }
    out[j] = 0;
}

const char* getEntityModelPath(void* e);

void friendlyName(void* e, ClientClass* cc, bool player, int idx, char* out, size_t n) {
    if (!out || n == 0) return;
    out[0] = 0;
    if (player) {
        PlayerInfo info{};
        if (g_Engine && VCall<bool, int, PlayerInfo*>(g_Engine, 8, idx, &info))
            copyVisible(out, n, info.name, sizeof info.name);
        if (!out[0]) snprintf(out, n, "Player");
        return;
    }

    const char* className = (cc && cc->m_pNetworkName) ? cc->m_pNetworkName : nullptr;
    const char* modelPath = getEntityModelPath(e);
    beautifyEntityName(className, modelPath, out, n);
}

static bool hasTokenNoCase(const char* text, const char* token);

static bool isRagdollEntity(void* entity, const char* displayName = nullptr) {
    if (displayName && hasTokenNoCase(displayName, "ragdoll")) return true;
    if (!entity) return false;
    ClientClass* cc = ClassOf(entity);
    const char* className = cc ? cc->m_pNetworkName : nullptr;
    const char* modelPath = getEntityModelPath(entity);
    return hasTokenNoCase(className, "ragdoll") || hasTokenNoCase(modelPath, "ragdoll");
}

bool isFriendlyNpc(const Target& t) {
    if (t.name[0] && hasTokenNoCase(t.name, "ragdoll")) return false;
    if (t.index > 0 && isRagdollEntity(Ent(t.index), t.name)) return false;
    if (t.player) return false;
    char lower[64]{};
    for (size_t i = 0; t.name[i] && i + 1 < sizeof lower; i++) {
        char c = t.name[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    static const char* kFriends[] = {
        "alyx", "barney", "kleiner", "eli_vance", "male_eli", "dr. eli vance", "mossman", "monk", "grigori",
        "dog", "vortigaunt", "magnusson", "odessa", "citizen", "rebel",
        "g-man", "gman", "refugee", "medic", "hostage",

        "male_", "female_", "group01", "group02", "group03", "group04",
        "group05", "group06", "group07", "group08", "group09", "group10",
        "npc_citizen", "npc_alyx", "npc_barney", "npc_kleiner", "npc_eli",
        "npc_mossman", "npc_monk", "npc_dog", "npc_vortigaunt",
        "cnpc_alyx", "cnpc_barney", "cnpc_citizen", "cnpc_kleiner",
        "cnpc_eli", "cnpc_mossman", "cnpc_monk", "cnpc_dog", "cnpc_vortigaunt",
        "cnpc_fisherman", "cnpc_magnusson", "npc_fisherman"
    };
    for (const char* f : kFriends)
        if (strstr(lower, f)) return true;

    if (strncmp(lower, "male", 4) == 0 || strncmp(lower, "female", 6) == 0)
        return true;

    if (t.index > 0) {
        void* e = Ent(t.index);
        if (e) {
            ClientClass* cc = ClassOf(e);
            const char* className = cc ? cc->m_pNetworkName : nullptr;
            const char* modelPath = getEntityModelPath(e);
            for (const char* f : kFriends) {
                if (hasTokenNoCase(className, f) || hasTokenNoCase(modelPath, f))
                    return true;
            }
        }
    }
    return false;
}

static bool hasTokenNoCase(const char* text, const char* token) {
    if (!text || !token || !*token) return false;
    const size_t n = strlen(token);
    for (const char* p = text; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i]) {
            char a = p[i], b = token[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
            if (a != b) break;
            ++i;
        }
        if (i == n) return true;
    }
    return false;
}

static bool isReadableModelPathString(const char* text) {
    if (!text) return false;
    const uintptr_t address = reinterpret_cast<uintptr_t>(text);
    if (address < 0x10000u || address > 0x7FFEFFFFu) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(text, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const DWORD protection = mbi.Protect & 0xFF;
    if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
        protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
        return false;
    const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    if (regionEnd <= address) return false;
    size_t limit = static_cast<size_t>(regionEnd - address);
    if (limit > 512) limit = 512;
    __try {
        for (size_t i = 0; i < limit; ++i)
            if (text[i] == '\0') return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

const char* getEntityModelPath(void* e) {
    if (!e || !g_ModelInfo) return nullptr;
    struct Entry { void* entity; void* modelInfo; const char* path; DWORD bucket; };
    static thread_local Entry cache[32768]{};

    const DWORD bucket = GetTickCount() / 50u;
    const size_t slot = (reinterpret_cast<uintptr_t>(e) >> 4) & (_countof(cache) - 1);
    Entry& entry = cache[slot];
    void* modelInfo = g_ModelInfo;
    if (entry.entity == e && entry.modelInfo == modelInfo && entry.bucket == bucket)
        return entry.path;

    const char* result = nullptr;
    if (!IsSaneClientEntity(e)) {
        entry.entity = e; entry.modelInfo = modelInfo; entry.path = nullptr; entry.bucket = bucket;
        return nullptr;
    }

    __try {
        void* rend = reinterpret_cast<char*>(e) + 4;
        if (IsReadableProcessRange(rend, sizeof(void*))) {
            void* model = VCall<void*>(rend, 9);
            if (model && IsReadableProcessRange(model, sizeof(void*))) {
                const char* path = VCall<const char*, void*>(modelInfo, 3, model);
                if (isReadableModelPathString(path)) result = path;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { result = nullptr; }

    entry.entity = e;
    entry.modelInfo = modelInfo;
    entry.path = result;
    entry.bucket = bucket;
    return result;
}

bool isAimEntityExcluded(void* e, unsigned excludeMask) {
    if (!e || excludeMask == 0) return false;
    ClientClass* cc = ClassOf(e);
    const char* className = cc ? cc->m_pNetworkName : nullptr;
    const char* modelPath = getEntityModelPath(e);

    auto hit = [&](const char* a, const char* b = nullptr, const char* c = nullptr) -> bool {
        if (a && (hasTokenNoCase(className, a) || hasTokenNoCase(modelPath, a))) return true;
        if (b && (hasTokenNoCase(className, b) || hasTokenNoCase(modelPath, b))) return true;
        if (c && (hasTokenNoCase(className, c) || hasTokenNoCase(modelPath, c))) return true;
        return false;
    };

    if ((excludeMask & AX_Alyx) && hit("alyx", "npc_alyx", "cnpc_alyx")) return true;
    if ((excludeMask & AX_Barney) && hit("barney", "npc_barney", "cnpc_barney")) return true;
    if ((excludeMask & AX_Kleiner) && hit("kleiner", "npc_kleiner", "cnpc_kleiner")) return true;
    if ((excludeMask & AX_Eli) && (hit("npc_eli", "cnpc_eli", "models/eli") || hit("eli_vance", "male_eli"))) return true;
    if ((excludeMask & AX_Mossman) && hit("mossman", "npc_mossman", "cnpc_mossman")) return true;
    if ((excludeMask & AX_Monk) && hit("monk", "grigori", "npc_monk")) return true;
    if ((excludeMask & AX_Dog) && hit("npc_dog", "cnpc_dog", "models/dog")) return true;
    if ((excludeMask & AX_Vortigaunt) && hit("vortigaunt", "npc_vortigaunt", "cnpc_vortigaunt")) return true;
    if ((excludeMask & AX_Citizen) && hit("citizen", "npc_citizen", "cnpc_citizen")) return true;
    if ((excludeMask & AX_Citizen) && (hit("male_", "female_") || hit("group01", "group02", "group03"))) return true;
    if ((excludeMask & AX_Gman) && hit("gman", "g-man", "npc_gman")) return true;
    if ((excludeMask & AX_Rpg) && hit("rpgmissile", "rpg_missile", "w_missile")) return true;
    if ((excludeMask & AX_CombineBall) && hit("combineball", "combine_ball", "combineenergyball")) return true;
    if ((excludeMask & AX_CrossbowBolt) && hit("crossbowbolt", "crossbow_bolt", "models/crossbow_bolt")) return true;
    if ((excludeMask & AX_Grenade) && hit("grenade_frag", "fraggrenade", "smg1_grenade")) return true;
    if ((excludeMask & AX_HunterFlechette) && hit("hunterflechette", "hunter_flechette", "w_hunter_flechette")) return true;
    if ((excludeMask & AX_AR2Orb) && hit("ar2altfire", "ar2_alt", "energy_ball")) return true;
    if ((excludeMask & AX_Ragdoll) && hit("ragdoll", "CRagdoll", "serverragdoll")) return true;
    if ((excludeMask & AX_Zombie) && hit("zombie", "zombine", "poisonzombie")) return true;
    if ((excludeMask & AX_Headcrab) && hit("headcrab", "npc_headcrab", "cnpc_headcrab")) return true;
    if ((excludeMask & AX_Antlion) && hit("antlion", "npc_antlion", "cnpc_antlion")) return true;
    if ((excludeMask & AX_Combine) && hit("combine_s", "combine_soldier", "combine_elite")) return true;
    if ((excludeMask & AX_Combine) && hit("npc_combine", "cnpc_combine", "combine_prisonguard")) return true;
    if ((excludeMask & AX_Metrocop) && hit("metropolice", "metrocop", "npc_metropolice")) return true;
    if ((excludeMask & AX_Manhack) && hit("manhack", "npc_manhack", "cnpc_manhack")) return true;
    if ((excludeMask & AX_Hunter) && hit("npc_hunter", "cnpc_hunter", "hunter")) return true;
    if ((excludeMask & AX_Barnacle) && hit("barnacle", "npc_barnacle", "cnpc_barnacle")) return true;
    if ((excludeMask & AX_Scanner) && hit("scanner", "npc_cscanner", "clawscanner")) return true;
    if ((excludeMask & AX_Turret) && hit("turret", "npc_turret", "floor_turret")) return true;
    return false;
}

static bool looksLikeVehicleClassOrModel(ClientClass* cc, void* e) {
    if (cc && cc->m_pRecvTable &&
        (netvar::Inherits(cc, "DT_PropVehicleDriveable") ||
         netvar::Inherits(cc, "DT_BaseFourWheelVehicle") ||
         netvar::Inherits(cc, "DT_PropJeep") ||
         netvar::Inherits(cc, "DT_PropVehicleAirboat") ||
         netvar::Inherits(cc, "DT_Vehicle"))) return true;

    const char* name = cc ? cc->m_pNetworkName : nullptr;
    static const char* const vehTokens[] = {
        "prop_vehicle", "vehicle_jeep", "vehicle_airboat", "prop_jeep",
        "prop_airboat", "CPropVehicle", "C_PropVehicle", "CPropJeep", "C_PropJeep",
        "CPropVehicleAirboat", "C_PropVehicleAirboat", "CPropVehicleDriveable",
        "C_PropVehicleDriveable", "jeep", "airboat"
    };
    for (const char* token : vehTokens)
        if (hasTokenNoCase(name, token)) return true;

    const char* model = getEntityModelPath(e);
    static const char* const modelTokens[] = {
        "models/buggy.mdl", "models/vehicle.mdl", "models/airboat.mdl",
        "models/jeep", "models/airboat", "buggy.mdl", "airboat.mdl"
    };
    for (const char* token : modelTokens)
        if (hasTokenNoCase(model, token)) return true;
    return false;
}

static bool looksLikeNpcClassOrModel(ClientClass* cc, void* e) {

    if (cc && cc->m_pRecvTable &&
        (netvar::Inherits(cc, "DT_AI_BaseNPC") ||
         netvar::Inherits(cc, "DT_BaseNPC") ||
         netvar::Inherits(cc, "DT_BaseCombatCharacter"))) return true;

    const char* name = cc ? cc->m_pNetworkName : nullptr;
    static const char* const npcTokens[] = {
        "npc_", "cnpc_", "ai_basenpc", "basecombatcharacter",
        "zombie", "headcrab", "antlion", "combine", "metropolice", "metrocop",
        "vortigaunt", "alyx", "barney", "kleiner", "mossman", "magnusson",
        "odessa", "grigori", "manhack", "scanner", "turret", "rollermine",
        "barnacle", "ichthyosaur", "strider", "gunship", "helicopter", "dropship",
        "hunter", "stalker", "gman", "g-man", "citizen", "rebel", "refugee",
        "dog", "advisor", "breen", "bullsquid"
    };
    for (const char* token : npcTokens)
        if (hasTokenNoCase(name, token)) return true;

    const char* model = getEntityModelPath(e);
    static const char* const modelTokens[] = {
        "models/humans/", "models/combine_", "models/zombie", "models/headcrab",
        "models/antlion", "models/vortigaunt", "models/alyx", "models/barney",
        "models/kleiner", "models/eli", "models/mossman", "models/monk",
        "models/dog", "models/magnusson", "models/odessa", "models/metrocop",
        "models/manhack", "models/roller", "models/strider", "models/gunship",
        "models/hunter", "models/stalker", "models/barnacle", "models/ichthyosaur",
        "models/gman", "models/g-man", "models/breen", "models/advisor"
    };
    for (const char* token : modelTokens)
        if (hasTokenNoCase(model, token)) return true;
    return false;
}

void relationColor(EntityRelation r, int& outR, int& outG, int& outB) {
    switch (r) {
    case ER_Friendly: outR = 60;  outG = 220; outB = 90;  break;   
    case ER_Hostile:  outR = 230; outG = 55;  outB = 55;  break;   
    case ER_Item:     outR = 80;  outG = 170; outB = 255; break;   
    case ER_Vehicle:  outR = 255; outG = 150; outB = 40;  break;   
    default:          outR = 160; outG = 160; outB = 160; break;   
    }
}

static bool matchAnyToken(const char* hay, const char* const* tokens, int count) {
    if (!hay || !*hay) return false;
    for (int i = 0; i < count; ++i) {
        if (hasTokenNoCase(hay, tokens[i])) return true;
    }
    return false;
}

EntityRelation classifyRelation(void* e, int idx, const Target* optTarget) {
    if (!e) return ER_Neutral;

    ClientClass* cc = ClassOf(e);
    const char* className = cc ? cc->m_pNetworkName : nullptr;
    const char* modelPath = getEntityModelPath(e);
    const char* display = (optTarget && optTarget->name[0]) ? optTarget->name : nullptr;

    if (isRagdollEntity(e, display)) return ER_Neutral;

    static const char* kVehicles[] = {
        "prop_vehicle", "c_prop_vehicle", "vehicle", "airboat", "jeep", "buggy",
        "prisoner_pod", "apc", "npc_vehicledriver", "cpropvehicle", "prop_vehicle_jeep",
        "prop_vehicle_airboat", "prop_vehicle_prisoner_pod", "prop_vehicle_apc",
        "models/buggy", "models/airboat", "jalopy"
    };
    if (matchAnyToken(className, kVehicles, (int)(sizeof kVehicles / sizeof kVehicles[0])) ||
        matchAnyToken(modelPath, kVehicles, (int)(sizeof kVehicles / sizeof kVehicles[0])))
        return ER_Vehicle;

    static const char* kItems[] = {
        "item_item_crate", "item_beacon_crate", "item_ammo_crate", "item_dynamic_resupply",
        "item_healthkit", "item_healthvial", "item_health_vial", "item_battery", "item_suit",
        "models/items/healthvial", "models/items/health_vial", "models/healthvial", "models/health_vial",
        "item_ammo", "item_box", "item_rpg_round", "item_ar2", "item_box_srounds",
        "item_box_mrounds", "item_box_lrounds", "item_box_buckshot", "item_flare_round",
        "weapon_", "cbasecombatweapon", "cweapon",
        "w_shotgun", "w_smg", "w_pistol", "w_irifle", "w_rpg", "w_357",
        "models/items/item_item_crate", "models/items/healthkit", "models/items/battery",
        "models/items/boxsrounds", "models/items/boxmrounds", "models/items/boxlrounds",
        "models/items/boxbuckshot", "models/items/ar2_grenade", "models/items/combine_rifle"
    };
    if (matchAnyToken(className, kItems, (int)(sizeof kItems / sizeof kItems[0])) ||
        matchAnyToken(modelPath, kItems, (int)(sizeof kItems / sizeof kItems[0])))
        return ER_Item;

    static const char* kFriendly[] = {
        "npc_alyx", "npc_barney", "npc_kleiner", "npc_eli", "npc_mossman", "npc_monk",
        "npc_gman", "npc_dog", "npc_vortigaunt", "npc_fisherman", "npc_magnusson",
        "npc_citizen", "npc_odessa", "cnpc_alyx", "cnpc_barney", "cnpc_citizen",
        "cnpc_kleiner", "cnpc_eli", "cnpc_mossman", "cnpc_monk", "cnpc_dog",
        "cnpc_vortigaunt", "cnpc_fisherman", "cnpc_magnusson", "cnpc_gman",
        "alyx", "barney", "kleiner", "npc_eli", "cnpc_eli", "models/eli", "eli_vance", "male_eli", "dr. eli vance", "mossman", "monk", "grigori", "dog",
        "vortigaunt", "magnusson", "odessa", "citizen", "rebel", "refugee", "medic",
        "hostage", "g-man", "gman", "male_", "female_", "group01", "group02", "group03",
        "group04", "group05", "group06", "group07", "group08", "group09", "group10",
        "models/alyx", "models/barney", "models/kleiner", "models/eli", "models/mossman",
        "models/monk", "models/dog", "models/vortigaunt", "models/humans"
    };
    if (matchAnyToken(className, kFriendly, (int)(sizeof kFriendly / sizeof kFriendly[0])) ||
        matchAnyToken(modelPath, kFriendly, (int)(sizeof kFriendly / sizeof kFriendly[0])) ||
        matchAnyToken(display, kFriendly, (int)(sizeof kFriendly / sizeof kFriendly[0])))
        return ER_Friendly;

    static const char* kHostile[] = {
        "npc_combine", "npc_metropolice", "npc_stalker", "npc_hunter", "npc_strider",
        "npc_helicopter", "npc_combinegunship", "npc_combinedropship", "npc_turret",
        "npc_manhack", "npc_cscanner", "npc_clawscanner", "npc_rollermine",
        "npc_zombie", "npc_fastzombie", "npc_poisonzombie", "npc_zombine",
        "npc_headcrab", "npc_fastheadcrab", "npc_poisonheadcrab",
        "npc_antlion", "npc_antlionguard", "npc_antlionguardian", "npc_antliongrub",
        "npc_barnacle", "npc_ichthyosaur", "npc_bullsquid", "npc_houndeye",
        "npc_assassin", "npc_launcher", "npc_sniper", "npc_enemy",
        "cnpc_combine", "cnpc_metropolice", "cnpc_zombie", "cnpc_headcrab", "cnpc_antlion",
        "combine", "metropolice", "zombie", "zombine", "headcrab", "antlion", "manhack",
        "hunter", "strider", "gunship", "dropship", "rollermine", "stalker", "barnacle",
        "models/combine", "models/zombie", "models/headcrab", "models/antlion",
        "models/manhack", "models/hunter", "models/gibs/gunship", "models/combine_strider",
        "models/combine_advisor", "models/police", "models/soldier"
    };
    if (matchAnyToken(className, kHostile, (int)(sizeof kHostile / sizeof kHostile[0])) ||
        matchAnyToken(modelPath, kHostile, (int)(sizeof kHostile / sizeof kHostile[0])) ||
        matchAnyToken(display, kHostile, (int)(sizeof kHostile / sizeof kHostile[0])))
        return ER_Hostile;

    if (optTarget && optTarget->player) return ER_Hostile;
    if (idx > 0 && idx <= MaxClients()) {

        return ER_Hostile;
    }

    return ER_Neutral;
}

bool classify(void* e, int idx, Target& t) {
    if (!e || Dormant(e)) return false;
    ClientClass* cc = ClassOf(e);
    if (!cc) return false;
    const int maxClients = MaxClients();
    bool player = idx >= 1 && maxClients > 0 && idx <= maxClients;
    const bool vehicle = !player && looksLikeVehicleClassOrModel(cc, e);
    bool npc = !player && !vehicle && looksLikeNpcClassOrModel(cc, e);
    if (!player && !npc && !vehicle) return false;

    int hp = o_Health >= 0 ? Field<int>(e, o_Health) : -1;
    if (player) {
        if (o_Health >= 0 && hp <= 0) return false;
        if (o_LifeState >= 0 && Field<uint8_t>(e, o_LifeState) != 0) return false;
    } else if (vehicle) {

        if (hp <= 0) hp = -1;
    } else {

        if (o_NpcLife >= 0 && Field<uint8_t>(e, o_NpcLife) != 0) return false;
        if (hp <= 0) hp = -1;  
    }
    t.ent = e; t.index = idx; t.player = player; t.health = hp;
    friendlyName(e, cc, player, idx, t.name, sizeof t.name);
    if (vehicle && (!t.name[0] || !strcmp(t.name, "CBaseAnimating") || !strcmp(t.name, "CPhysicsProp"))) {
        const char* model = getEntityModelPath(e);
        if (hasTokenNoCase(model, "airboat")) strncpy(t.name, "Airboat", sizeof(t.name) - 1);
        else if (hasTokenNoCase(model, "buggy") || hasTokenNoCase(model, "jeep") || hasTokenNoCase(model, "vehicle"))
            strncpy(t.name, "Jeep", sizeof(t.name) - 1);
        else if (hasTokenNoCase(cc->m_pNetworkName, "airboat")) strncpy(t.name, "Airboat", sizeof(t.name) - 1);
        else if (hasTokenNoCase(cc->m_pNetworkName, "jeep") || hasTokenNoCase(cc->m_pNetworkName, "buggy"))
            strncpy(t.name, "Jeep", sizeof(t.name) - 1);
        else
            strncpy(t.name, "Vehicle", sizeof(t.name) - 1);
        t.name[sizeof(t.name) - 1] = 0;
    }
    t.origin = Field<Vec3>(e, o_Origin);
    t.mins = Field<Vec3>(e, o_Mins);
    t.maxs = Field<Vec3>(e, o_Maxs);
    t.aimPos = t.origin + (t.mins + t.maxs) * 0.5f;
    return true;
}

char* setupBones(void* e, Matrix3x4* bones) {
    void* rend = reinterpret_cast<char*>(e) + 4;
    void* mdl = VCall<void*>(rend, 9);
    if (!mdl || !g_ModelInfo) return nullptr;
    char* hdr = VCall<char*, void*>(g_ModelInfo, 28, mdl);
    if (!hdr || Field<int>(hdr, 0) != 0x54534449) return nullptr;
    int nb = Field<int>(hdr, 156);
    if (nb < 1 || nb > kMaxBones) return nullptr;
    if (!VCall<bool, Matrix3x4*, int, int, float>(rend, 16, bones, kMaxBones, 0x7FF00, GetTickCount() * 0.001f)) return nullptr;
    return hdr;
}

static bool transformHitboxCenter(const Matrix3x4* bones, int boneCount, const char* box, Vec3& out) {
    if (!bones || !box) return false;
    const int bone = Field<int>(const_cast<char*>(box), 0);
    if (bone < 0 || bone >= boneCount) return false;

    const float minX = Field<float>(const_cast<char*>(box), 8);
    const float minY = Field<float>(const_cast<char*>(box), 12);
    const float minZ = Field<float>(const_cast<char*>(box), 16);
    const float maxX = Field<float>(const_cast<char*>(box), 20);
    const float maxY = Field<float>(const_cast<char*>(box), 24);
    const float maxZ = Field<float>(const_cast<char*>(box), 28);
    const float mx = (minX + maxX) * 0.5f;
    const float my = (minY + maxY) * 0.5f;
    const float mz = (minZ + maxZ) * 0.5f;
    const Matrix3x4& m = bones[bone];
    out = Vec3(m.m[0][0] * mx + m.m[0][1] * my + m.m[0][2] * mz + m.m[0][3],
               m.m[1][0] * mx + m.m[1][1] * my + m.m[1][2] * mz + m.m[1][3],
               m.m[2][0] * mx + m.m[2][1] * my + m.m[2][2] * mz + m.m[2][3]);
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

static bool findHitgroupPoint(const Matrix3x4* bones, int boneCount, char* boxes,
                              int hitboxCount, int wantedGroup, bool lowest,
                              Vec3& out) {
    bool found = false;
    float bestZ = lowest ? 3.402823466e+38F : -3.402823466e+38F;
    for (int i = 0; i < hitboxCount; ++i) {
        char* box = boxes + kHitboxStride * i;
        const int group = Field<int>(box, 4);  
        if (group != wantedGroup) continue;
        Vec3 point;
        if (!transformHitboxCenter(bones, boneCount, box, point)) continue;
        if (!found || (lowest ? point.z < bestZ : point.z > bestZ)) {
            found = true;
            bestZ = point.z;
            out = point;
        }

        if (!lowest && wantedGroup != 3) break;
    }
    return found;
}

static bool findGenericHitboxPoint(const Target& target, const Matrix3x4* bones,
                                   int boneCount, char* boxes, int hitboxCount,
                                   int which, Vec3& out) {
    if (!bones || !boxes || hitboxCount < 1 || hitboxCount > 256 ||
        which < 0 || which >= kBoneCount) return false;

    Vec3 points[256] = {};
    bool valid[256] = {};
    int validCount = 0;
    float minZ = 3.402823466e+38F;
    float maxZ = -3.402823466e+38F;
    for (int i = 0; i < hitboxCount; ++i) {
        Vec3 p;
        if (!transformHitboxCenter(bones, boneCount, boxes + kHitboxStride * i, p))
            continue;
        points[i] = p;
        valid[i] = true;
        ++validCount;
        if (p.z < minZ) minZ = p.z;
        if (p.z > maxZ) maxZ = p.z;
    }
    if (validCount == 0) return false;

    const float height = maxZ - minZ;
    float targetZ = minZ + height * 0.5f;
    if (which == 0) targetZ = maxZ;                          
    else if (which == 1) targetZ = minZ + height * 0.68f;  
    else if (which == 2) targetZ = minZ + height * 0.48f;  
    else if (which == 3) targetZ = minZ;                   

    const Vec3 reference = target.aimPos;
    float bestMetric = 3.402823466e+38F;
    int best = -1;
    for (int i = 0; i < hitboxCount; ++i) {
        if (!valid[i]) continue;
        float metric;
        if (which == 4) {
            const float dx = points[i].x - reference.x;
            const float dy = points[i].y - reference.y;
            const float dz = points[i].z - reference.z;
            metric = dx * dx + dy * dy + dz * dz;
        } else {
            const float dz = points[i].z - targetZ;
            metric = dz * dz;
        }
        if (metric < bestMetric) {
            bestMetric = metric;
            best = i;
        }
    }
    if (best < 0) return false;
    out = points[best];
    return true;
}

static bool computeAimPoint(const Target& t, int which, Vec3& out) {
    if (which < 0 || which >= kBoneCount) return false;

    struct AimBoneCache {
        void* entity;
        DWORD bucket;
        char* header;
        Matrix3x4 bones[kMaxBones];
    };
    static AimBoneCache cache{};
    const DWORD bucket = GetTickCount() / 16u;
    if (cache.entity != t.ent || cache.bucket != bucket) {
        cache.entity = t.ent;
        cache.bucket = bucket;
        cache.header = setupBones(t.ent, cache.bones);
    }
    Matrix3x4* bones = cache.bones;
    char* hdr = cache.header;

    if (!hdr) {
        out = t.aimPos;
        return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
    }

    const int boneCount = Field<int>(hdr, 156);
    const int boneIndex = Field<int>(hdr, 160);
    const int hdrLength = Field<int>(hdr, 76);
    const int numHitboxSets = Field<int>(hdr, 172);
    const int hitboxSetIndex = Field<int>(hdr, 176);
    if (boneCount < 1 || boneCount > kMaxBones || boneIndex < 0 ||
        hdrLength < 200 || hdrLength > (1 << 26) ||
        boneIndex + kBoneStride * boneCount > hdrLength ||
        numHitboxSets < 1 || numHitboxSets > 64 || hitboxSetIndex < 0 ||
        hitboxSetIndex + 12 > hdrLength)
        return false;

    char* set = hdr + hitboxSetIndex;
    const int hitboxCount = Field<int>(set, 4);
    const int hitboxIndex = Field<int>(set, 8);
    if (hitboxCount < 1 || hitboxCount > 256 || hitboxIndex < 0 ||
        hitboxSetIndex + hitboxIndex + kHitboxStride * hitboxCount > hdrLength)
        return false;
    char* boxes = set + hitboxIndex;

    if (which == 0) {
        if (findHitgroupPoint(bones, boneCount, boxes, hitboxCount, 1, false, out)) return true;
        return findGenericHitboxPoint(t, bones, boneCount, boxes, hitboxCount, which, out);
    }
    if (which == 1) {
        if (findHitgroupPoint(bones, boneCount, boxes, hitboxCount, 2, false, out)) return true;
        return findGenericHitboxPoint(t, bones, boneCount, boxes, hitboxCount, which, out);
    }
    if (which == 2) {
        if (findHitgroupPoint(bones, boneCount, boxes, hitboxCount, 3, false, out)) return true;
        return findGenericHitboxPoint(t, bones, boneCount, boxes, hitboxCount, which, out);
    }
    if (which == 3) {
        if (findHitgroupPoint(bones, boneCount, boxes, hitboxCount, 3, true, out)) return true;
        return findGenericHitboxPoint(t, bones, boneCount, boxes, hitboxCount, which, out);
    }

    Vec3 chest, stomach;
    const bool haveChest = findHitgroupPoint(bones, boneCount, boxes, hitboxCount, 2, false, chest);
    const bool haveStomach = findHitgroupPoint(bones, boneCount, boxes, hitboxCount, 3, false, stomach);
    if (haveChest && haveStomach) {
        out = (chest + stomach) * 0.5f;
        return true;
    }
    if (haveChest) { out = chest; return true; }
    if (haveStomach) { out = stomach; return true; }
    return findGenericHitboxPoint(t, bones, boneCount, boxes, hitboxCount, which, out);
}

Vec3 eyePos(void* e) {
    Vec3 o = Field<Vec3>(e, o_Origin);
    Vec3 v = Field<Vec3>(e, o_ViewOffset);
    return o + v;
}

QAngle calcAngle(const Vec3& from, const Vec3& to) {
    Vec3 d = to - from;
    float hyp = sqrtf(d.x * d.x + d.y * d.y);
    QAngle a;
    a.x = -atan2f(d.z, hyp) * 57.2957795f;
    a.y = atan2f(d.y, d.x) * 57.2957795f;
    a.z = 0.f;
    return a;
}

float normAngle(float a) {
    while (a > 180) a -= 360;
    while (a < -180) a += 360;
    return a;
}

bool tryAimPoint(const Target& t, int which, Vec3& out) {
    if (!t.ent || which < 0 || which >= kBoneCount) return false;
    struct AimPointCacheEntry {
        void* entity;
        DWORD bucket;
        bool attempted[kBoneCount];
        bool valid[kBoneCount];
        Vec3 points[kBoneCount];
    };
    static AimPointCacheEntry cache[256]{};

    const uintptr_t raw = reinterpret_cast<uintptr_t>(t.ent);
    const size_t slot = ((raw >> 4) ^ (raw >> 11)) & 255u;
    const DWORD bucket = GetTickCount() / 16u;
    AimPointCacheEntry& entry = cache[slot];
    if (entry.entity != t.ent || entry.bucket != bucket) {
        entry.entity = t.ent;
        entry.bucket = bucket;
        memset(entry.attempted, 0, sizeof(entry.attempted));
        memset(entry.valid, 0, sizeof(entry.valid));
    }
    if (!entry.attempted[which]) {
        entry.attempted[which] = true;
        entry.valid[which] = computeAimPoint(t, which, entry.points[which]);
    }
    if (!entry.valid[which]) return false;
    out = entry.points[which];
    return true;
}

bool visible(void* local, const Vec3& from, const Target& t) {
    if (!local || !t.ent || !g_Trace) return false;

    struct VisibilityCacheEntry {
        void* local = nullptr;
        void* entity = nullptr;
        DWORD bucket = 0xFFFFFFFFu;
        Vec3 from{};
        Vec3 to{};
        bool result = false;
        bool valid = false;
    };
    static VisibilityCacheEntry cache[128]{};
    const DWORD bucket = GetTickCount() / 16u;
    static DWORD traceBudgetBucket = 0xFFFFFFFFu;
    static int traceBudgetUsed = 0;
    if (traceBudgetBucket != bucket) {
        traceBudgetBucket = bucket;
        traceBudgetUsed = 0;
    }
    const uintptr_t localRaw = reinterpret_cast<uintptr_t>(local);
    const uintptr_t entRaw = reinterpret_cast<uintptr_t>(t.ent);
    const size_t slot = ((localRaw >> 4) ^ (entRaw >> 7) ^
        (static_cast<uintptr_t>(t.index) * 2654435761u)) & 127u;
    VisibilityCacheEntry& entry = cache[slot];
    if (entry.valid && entry.local == local && entry.entity == t.ent && entry.bucket == bucket) {
        const Vec3 fromDelta = entry.from - from;
        const Vec3 toDelta = entry.to - t.aimPos;
        const float fromDistance2 = fromDelta.x * fromDelta.x + fromDelta.y * fromDelta.y +
                                    fromDelta.z * fromDelta.z;
        const float toDistance2 = toDelta.x * toDelta.x + toDelta.y * toDelta.y +
                                  toDelta.z * toDelta.z;
        if (fromDistance2 < 1.f && toDistance2 < 1.f)
            return entry.result;
    }

    constexpr int kMaxVisibilityTracesPerWindow = 32;
    if (traceBudgetUsed >= kMaxVisibilityTracesPerWindow) {
        entry.valid = false;
        return false;
    }

    Ray_t ray;
    ray.init(from, t.aimPos);
    TraceFilter filter;
    filter.skip = local;
    Trace_t tr{};
    ++traceBudgetUsed;
    __try {
        VCall<void, const Ray_t&, unsigned, void*, Trace_t*>(g_Trace, 4, ray, MASK_SHOT, &filter, &tr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        entry.valid = false;
        return false;
    }
    const bool visible = tr.fraction > 0.97f || tr.ent == t.ent;
    entry.local = local;
    entry.entity = t.ent;
    entry.bucket = bucket;
    entry.from = from;
    entry.to = t.aimPos;
    entry.result = visible;
    entry.valid = true;
    return visible;
}

float traceDown(void* local, float len) {
    Vec3 o = Field<Vec3>(local, o_Origin);
    Ray_t ray;
    ray.init(o, o - Vec3(0, 0, len));
    TraceFilter filter;
    filter.skip = local;
    Trace_t tr{};
    VCall<void, const Ray_t&, unsigned, void*, Trace_t*>(g_Trace, 4, ray, 0x201400B, &filter, &tr);
    return tr.fraction * len;
}

float traceDownFrom(void* local, const Vec3& start, float len) {
    if (!local || !g_Trace || len <= 0.f) return len;
    Ray_t ray;
    ray.init(start, start - Vec3(0.f, 0.f, len));
    TraceFilter filter;
    filter.skip = local;
    Trace_t tr{};
    VCall<void, const Ray_t&, unsigned, void*, Trace_t*>(g_Trace, 4, ray, 0x201400B, &filter, &tr);
    if (!std::isfinite(tr.fraction)) return len;
    return tr.fraction * len;
}

float traceDownHull(void* local, float len) {
    if (!local || !g_Trace || o_Origin < 0 || o_Mins < 0 || o_Maxs < 0 || len <= 0.f) return len;
    const Vec3 origin = Field<Vec3>(local, o_Origin);
    const Vec3 mins = Field<Vec3>(local, o_Mins);
    const Vec3 maxs = Field<Vec3>(local, o_Maxs);
    const Vec3 center((mins.x + maxs.x) * 0.5f, (mins.y + maxs.y) * 0.5f, (mins.z + maxs.z) * 0.5f);
    const Vec3 extents((maxs.x - mins.x) * 0.5f, (maxs.y - mins.y) * 0.5f, (maxs.z - mins.z) * 0.5f);
    Ray_t ray;
    const Vec3 start = origin + center;
    ray.init(start, start - Vec3(0.f, 0.f, len));

    ray.startOffset = Vec3(-center.x, -center.y, -center.z);
    ray.extents = extents;

    const int flagOffset = g_RayVariant ? 4 : 0;
    ray.tail[flagOffset] = 0;
    TraceFilter filter;
    filter.skip = local;
    Trace_t tr{};
    VCall<void, const Ray_t&, unsigned, void*, Trace_t*>(g_Trace, 4, ray, 0x201400B, &filter, &tr);
    if (!std::isfinite(tr.fraction)) return len;
    return tr.fraction * len;
}

void pickRayVariant(void* local) {
    static bool done;
    if (done || !(Field<int>(local, o_Flags) & 1)) return;
    float d[2];
    for (int v = 0; v < 2; v++) {
        g_RayVariant = v;
        d[v] = traceDown(local, 64.f);
    }
    g_RayVariant = d[0] < 63.f ? 0 : d[1] < 63.f ? 1 : 0;
    done = d[0] < 63.f || d[1] < 63.f;
}

static bool copyNetTextAt(void* net, int slot, char* out, size_t capacity) {
    if (!net || !out || capacity < 2) return false;
    out[0] = '\0';
    __try {
        const char* text = VCall<const char*>(net, slot);
        if (!text) return false;
        size_t i = 0;
        for (; i < capacity; ++i) {
            const unsigned char ch = static_cast<unsigned char>(text[i]);
            if (ch == 0) break;
            if (i + 1 >= capacity || ch < 32 || ch > 126) { out[0] = '\0'; return false; }
            out[i] = static_cast<char>(ch);
        }
        if (i == 0 || i >= capacity) { out[0] = '\0'; return false; }
        out[i] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
        return false;
    }
}

static bool tryGetNetFloat(void* net, int slot, int flow, float* value) {
    if (!net || !value) return false;
    *value = -1.f;
    __try {
        const float result = VCall<float, int>(net, slot, flow);
        if (!std::isfinite(result)) return false;
        *value = result;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *value = -1.f;
        return false;
    }
}

static bool looksLikeAddress(const char* text) {
    if (!text || !*text) return false;

    if (_stricmp(text, "loopback") == 0 || _stricmp(text, "localhost") == 0 ||
        _stricmp(text, "local") == 0) return true;

    static const char* invalid[] = {
        "unknown", "unconnected", "connecting", "none", "null", "net", "netchan",
        "netchannel", "client", "serverchannel", "unnamed", "n/a"
    };
    for (const char* item : invalid) if (_stricmp(text, item) == 0) return false;
    bool hasAlphaNumeric = false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (std::isalnum(*p)) { hasAlphaNumeric = true; continue; }
        if (*p == '.' || *p == ':' || *p == '-' || *p == '_' || *p == '[' || *p == ']' || *p == '%')
            continue;
        return false;
    }
    if (!hasAlphaNumeric) return false;
    if (strchr(text, ':') || strchr(text, '.') || strchr(text, '-') || strchr(text, '_')) return true;
    return strlen(text) >= 3;  
}

static bool copyNetAddress(void* net, char* out, size_t capacity, float* avgLatency) {
    if (!net || !out || capacity < 2) return false;
    out[0] = '\0';

    char address[64]{};
    if (copyNetTextAt(net, 1, address, sizeof(address)) && looksLikeAddress(address)) {
        strncpy_s(out, capacity, address, _TRUNCATE);
    } else {

        char name[64]{};
        if (!copyNetTextAt(net, 0, name, sizeof(name)) || !looksLikeAddress(name)) return false;
        strncpy_s(out, capacity, name, _TRUNCATE);
    }

    if (avgLatency) {
        float latency = -1.f;
        if (tryGetNetFloat(net, 10, 0, &latency) && latency >= 0.f && latency <= 10.f)
            *avgLatency = latency;
        else
            *avgLatency = -1.f;
    }
    return out[0] != '\0';
}

static bool copyEngineLevelName(void* engine, int slot, char* out, size_t capacity) {
    if (!engine || !out || capacity < 2) return false;
    out[0] = '\0';
    __try {
        const char* level = VCall<const char*>(engine, slot);
        if (!level) return false;
        size_t i = 0;
        for (; i + 1 < capacity; ++i) {
            const unsigned char ch = static_cast<unsigned char>(level[i]);
            if (ch == 0) break;
            if (ch < 32 || ch > 126) { out[0] = '\0'; return false; }
            out[i] = static_cast<char>(ch);
        }
        if (i == 0 || i + 1 >= capacity) { out[0] = '\0'; return false; }
        out[i] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
        return false;
    }
}

static bool tryGetNetInfoAt(void* engine, int slot, void** netOut, char* address,
                            size_t addressCapacity, float* avgLatency) {
    if (!engine || !netOut || !address || addressCapacity < 2) return false;
    *netOut = nullptr;
    address[0] = '\0';
    void* candidate = nullptr;
    __try {
        candidate = VCall<void*>(engine, slot);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!candidate || !copyNetAddress(candidate, address, addressCapacity, avgLatency)) return false;
    *netOut = candidate;
    return true;
}

bool queryServerAddress(char* out, size_t outSize) {
    if (!out || outSize < 2) return false;
    out[0] = '\0';
    if (!g_Engine) return false;
    void* net = nullptr;
    float unusedLatency = -1.f;
    if (!tryGetNetInfoAt(g_Engine, 72, &net, out, outSize, &unusedLatency)) {
        out[0] = '\0';
        return false;
    }
    return out[0] != '\0';
}

void queryNetStats(NetStats& out) {
    out = {};
    if (!g_Engine) return;

    // Keep the full instance vtable; the engine calls entries by slot
    char mapName[128]{};
    if (copyEngineLevelName(g_Engine, 51, mapName, sizeof(mapName))) {
        if (strchr(mapName, '/') || strstr(mapName, "maps") || strstr(mapName, ".bsp") ||
            (mapName[0] && !strchr(mapName, ' '))) {
            const char* base = strrchr(mapName, '/');
            base = base ? base + 1 : mapName;
            snprintf(out.level, sizeof out.level, "%s", base);
            char* dot = strrchr(out.level, '.');
            if (dot) *dot = 0;
        }
    }

    void* net = nullptr;
    float avgLatency = -1.f;
    char address[64]{};
    if (!tryGetNetInfoAt(g_Engine, 72, &net, address, sizeof(address), &avgLatency)) {
        out.ok = out.level[0] != 0;
        return;
    }

    snprintf(out.address, sizeof out.address, "%s", address);
    out.ok = true;

    float latOut = -1.f, latIn = -1.f;
    if (!tryGetNetFloat(net, 9, 0, &latOut) || latOut < 0.f || latOut > 5.f) latOut = avgLatency;
    if (!tryGetNetFloat(net, 9, 1, &latIn) || latIn < 0.f || latIn > 5.f) latIn = latOut;
    out.pingMs = (latOut >= 0.f) ? latOut * 1000.f : 0.f;
    out.pingInMs = (latIn >= 0.f) ? latIn * 1000.f : out.pingMs;

    float value = 0.f;
    if (tryGetNetFloat(net, 11, 0, &value)) out.loss = value;  
    if (tryGetNetFloat(net, 12, 0, &value)) out.choke = value;  
    if (tryGetNetFloat(net, 13, 0, &value)) out.dataOut = value;  
    if (tryGetNetFloat(net, 13, 1, &value)) out.dataIn = value;
    if (tryGetNetFloat(net, 14, 0, &value)) out.pktOut = value;  
    if (tryGetNetFloat(net, 14, 1, &value)) out.pktIn = value;
}
