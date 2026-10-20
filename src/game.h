#pragma once
#include "sdk.h"
#include "netvar.h"
#include <cstdio>
#include <cstring>

extern void* g_Client;
extern void* g_Engine;
extern void* g_EntList;
extern void* g_Trace;
extern void* g_Surface;
extern void* g_Panel;
extern void* g_VGuiInput;
extern void* g_ModelInfo;
extern void* g_ClientMode;
extern void* g_Cvar;

extern int o_ModelIndex;
extern int o_Flags, o_NpcLife, o_Velocity;
extern int o_Health, o_Armor, o_Origin, o_TeamNum, o_Mins, o_Maxs, o_ViewOffset, o_LifeState;
extern int o_ActiveWeapon;
extern int o_PunchAngle, o_PunchAngleVel;

enum AimExcludeBit : unsigned {
    // Friendlies
    AX_Alyx            = 1u << 0,
    AX_Barney          = 1u << 1,
    AX_Kleiner         = 1u << 2,
    AX_Eli             = 1u << 3,
    AX_Mossman         = 1u << 4,
    AX_Monk            = 1u << 5,
    AX_Dog             = 1u << 6,
    AX_Vortigaunt      = 1u << 7,
    AX_Citizen         = 1u << 8,
    AX_Gman            = 1u << 9,
    // Projectiles
    AX_Rpg             = 1u << 10,
    AX_CombineBall     = 1u << 11,
    AX_CrossbowBolt    = 1u << 12,
    AX_Grenade         = 1u << 13,
    AX_HunterFlechette = 1u << 14,
    AX_AR2Orb          = 1u << 15,
    AX_Ragdoll         = 1u << 16,
    // Hostiles (optional exclude)
    AX_Zombie          = 1u << 17,
    AX_Headcrab        = 1u << 18,
    AX_Antlion        = 1u << 19,
    AX_Combine         = 1u << 20,
    AX_Metrocop        = 1u << 21,
    AX_Manhack         = 1u << 22,
    AX_Hunter          = 1u << 23,
    AX_Barnacle        = 1u << 24,
    AX_Scanner         = 1u << 25,
    AX_Turret          = 1u << 26,
};

constexpr unsigned kAimExcludeDefault =
    0x3FFu | /* friendlies 0-9 */
    (1u << 10) | (1u << 11) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 15) | /* projectiles */
    (1u << 16); /* ragdoll */

struct AimExcludeEntry {
    unsigned bit;
    const char* token;    
    const wchar_t* label;
};
extern const AimExcludeEntry kAimExcludeEntries[];
extern const int kAimExcludeEntryCount;

extern bool g_MenuOpen;
extern bool g_CmdReset;
extern bool g_EjectRequested;
extern HINSTANCE g_Self;

void requestEject();

constexpr uintptr_t kClientModeOff = 0x73262C;

constexpr const char* kDiscordAppId = "1560934986012434483";

using CreateInterfaceFn = void* (*)(const char*, int*);
void* getIface(const char* module, const char* name);

bool setClientFullBrightDirect(int value);
bool setClientConVarIntDirect(const char* name, int value);
bool setClientConVarStringDirect(const char* name, const char* value, bool allowLocalCheatFlag);
bool getConVarStringDirect(const char* name, char* out, size_t outSize);
long getClientFullBrightFlagBypassCount();

// Validate the networkable vtable before calling it

inline bool IsReadableProcessRange(const void* address, size_t bytes) {
    if (!address || bytes == 0) return false;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
    constexpr uintptr_t kMinUserAddress = 0x10000u;
    constexpr uintptr_t kMaxUserAddress = 0x7FFEFFFFu;
    if (begin < kMinUserAddress || begin > kMaxUserAddress || bytes > kMaxUserAddress - begin)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    const DWORD protection = mbi.Protect & 0xFF;
    const bool readable = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    if (!readable) return false;

    const uintptr_t regionBegin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const uintptr_t regionEnd = regionBegin + mbi.RegionSize;
    return begin >= regionBegin && begin <= regionEnd && bytes <= regionEnd - begin;
}

inline bool IsExecutableProcessAddress(const void* address) {
    if (!address) return false;
    const uintptr_t p = reinterpret_cast<uintptr_t>(address);
    if (p < 0x10000u || p > 0x7FFEFFFFu) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const DWORD protection = mbi.Protect & 0xFF;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

inline bool IsSaneClientEntity(void* entity) {
    if (!IsReadableProcessRange(entity, 12)) return false;

    // Validate the vtable range before reading slots

    void** networkableVtable = nullptr;
    memcpy(&networkableVtable, reinterpret_cast<const char*>(entity) + 8, sizeof(networkableVtable));
    if (!networkableVtable || reinterpret_cast<uintptr_t>(networkableVtable) < 0x10000u)
        return false;
    return IsReadableProcessRange(networkableVtable, 9 * sizeof(void*));
}

inline void* Ent(int i) {
    if (!g_EntList || i < 0 || i > 16384) return nullptr;
    struct Entry { void* list; void* entity; DWORD bucket; };
    static thread_local Entry cache[16385]{};
    const DWORD bucket = GetTickCount64() / 16u;
    Entry& entry = cache[i];
    void* list = g_EntList;
    if (entry.list == list && entry.bucket == bucket) return entry.entity;

    void* entity = nullptr;
    __try {
        entity = VCall<void*>(list, 3, i);
        if (entity && !IsSaneClientEntity(entity)) entity = nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        entity = nullptr;
    }
    entry.list = list;
    entry.entity = entity;
    entry.bucket = bucket;
    return entity;
}
inline int MaxEnt() {
    if (!g_EntList) return 0;
    struct Entry { void* list; DWORD bucket; int count; };
    static thread_local Entry cache{};
    const DWORD bucket = GetTickCount64() / 8u;
    void* list = g_EntList;
    if (cache.list == list && cache.bucket == bucket) return cache.count;
    int count = 0;
    __try {
        count = VCall<int>(list, 6);
        if (count < 0 || count > 16384) count = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { count = 0; }
    cache.list = list; cache.bucket = bucket; cache.count = count;
    return count;
}
inline int LocalIndex() { return g_Engine != nullptr ? VCall<int>(g_Engine, 12) : 0; }
inline int MaxClients() {
    if (!g_Engine) return 0;
    struct Entry { void* engine; DWORD bucket; int count; };
    static thread_local Entry cache{};
    const DWORD bucket = GetTickCount64() / 250u;
    void* engine = g_Engine;
    if (cache.engine == engine && cache.bucket == bucket) return cache.count;
    int count = 0;
    __try {
        count = VCall<int>(engine, 21);
        if (count < 0 || count > 256) count = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { count = 0; }
    cache.engine = engine; cache.bucket = bucket; cache.count = count;
    return count;
}
inline bool InGame() { return g_Engine != nullptr && VCall<bool>(g_Engine, 26); }
inline void* Networkable(void* e) { return reinterpret_cast<char*>(e) + 8; }
inline ClientClass* ClassOf(void* e) {
    if (!e) return nullptr;
    // Keep the full instance vtable; the engine calls entries by slot

    struct Entry { void* entity; ClientClass* clientClass; DWORD bucket; };
    static thread_local Entry cache[32768]{};
    const DWORD bucket = GetTickCount64() / 8u;
    const size_t slot = (reinterpret_cast<uintptr_t>(e) >> 4) & (_countof(cache) - 1);
    Entry& entry = cache[slot];
    if (entry.entity == e && entry.bucket == bucket) return entry.clientClass;

    ClientClass* result = nullptr;
    __try {
        if (IsSaneClientEntity(e)) {
            void* networkable = Networkable(e);
            void** vtable = *reinterpret_cast<void***>(networkable);
            if (vtable && IsReadableProcessRange(vtable, 3 * sizeof(void*))) {
                void* fn = vtable[2];
                if (IsExecutableProcessAddress(fn)) {
                    using GetClientClassFn = ClientClass*(__thiscall*)(void*);
                    ClientClass* candidate = reinterpret_cast<GetClientClassFn>(fn)(networkable);
                    if (IsReadableProcessRange(candidate, sizeof(ClientClass))) result = candidate;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { result = nullptr; }
    entry.entity = e;
    entry.clientClass = result;
    entry.bucket = bucket;
    return result;
}

const char* getEntityModelPath(void* e);
inline bool Dormant(void* e) {
    if (!e) return true;

    struct Entry { void* entity; DWORD bucket; bool dormant; };
    static thread_local Entry cache[32768]{};
    const DWORD bucket = GetTickCount64() / 8u;
    const size_t slot = (reinterpret_cast<uintptr_t>(e) >> 4) & (_countof(cache) - 1);
    Entry& entry = cache[slot];
    if (entry.entity == e && entry.bucket == bucket) return entry.dormant;

    bool result = true;
    __try {
        if (IsSaneClientEntity(e)) {
            void* networkable = Networkable(e);
            void** vtable = *reinterpret_cast<void***>(networkable);
            if (vtable && IsReadableProcessRange(vtable, 9 * sizeof(void*))) {
                void* fn = vtable[8];
                if (IsExecutableProcessAddress(fn)) {
                    using IsDormantFn = bool(__thiscall*)(void*);
                    result = reinterpret_cast<IsDormantFn>(fn)(networkable);
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { result = true; }
    entry.entity = e;
    entry.bucket = bucket;
    entry.dormant = result;
    return result;
}
template <typename T> inline T& Field(void* e, int off) {
    return *reinterpret_cast<T*>(reinterpret_cast<char*>(e) + off);
}

struct Target {
    void* ent;
    int index;
    bool player;
    Vec3 aimPos;
    Vec3 mins, maxs, origin;
    int health;
    char name[64];
};

enum EntityRelation {
    ER_Neutral = 0,
    ER_Friendly,
    ER_Hostile,
    ER_Item,
    ER_Vehicle
};

EntityRelation classifyRelation(void* e, int idx, const Target* optTarget = nullptr);
void relationColor(EntityRelation r, int& outR, int& outG, int& outB);

struct PlayerInfo {
    char name[128];
    int userID;
    char guid[33];
    uint32_t friendsID;
    char friendsName[128];
    bool fakeplayer;
    bool ishltv;
    uint32_t customFiles[4];
    unsigned char filesDownloaded;
    char pad[256];
};

struct alignas(16) Matrix3x4 { float m[3][4]; };
constexpr int kMaxBones = 128;
constexpr int kBoneStride = 216;
constexpr int kHitboxStride = 68;
constexpr int kBoneCount = 5;
extern const wchar_t* const kBoneNames[kBoneCount];

inline Vec3 BonePos(const Matrix3x4& b) { return Vec3(b.m[0][3], b.m[1][3], b.m[2][3]); }

void friendlyName(void* e, ClientClass* cc, bool player, int idx, char* out, size_t n);
bool classify(void* e, int idx, Target& t);
bool isFriendlyNpc(const Target& t);
bool isAimEntityExcluded(void* e, unsigned excludeMask);
char* setupBones(void* e, Matrix3x4* bones);
bool tryAimPoint(const Target& t, int which, Vec3& out);
Vec3 eyePos(void* e);
QAngle calcAngle(const Vec3& from, const Vec3& to);
float normAngle(float a);
bool visible(void* local, const Vec3& from, const Target& t);
float traceDown(void* local, float len);
float traceDownFrom(void* local, const Vec3& start, float len);
float traceDownHull(void* local, float len);
void pickRayVariant(void* local);

bool installLocalMoveTypeHook();
void removeLocalMoveTypeHook();
bool isLocalOnLadder();
int getLocalMoveType();

struct NetStats {
    bool ok = false;
    float pingMs = 0.f;
    float pingInMs = 0.f;
    float loss = 0.f;
    float choke = 0.f;
    float pktOut = 0.f;
    float pktIn = 0.f;
    float dataOut = 0.f;
    float dataIn = 0.f;
    char address[64] = {};
    char level[64] = {};
};
void queryNetStats(NetStats& out);
bool queryServerAddress(char* out, size_t outSize);
