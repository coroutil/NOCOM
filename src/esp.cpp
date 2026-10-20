#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "features.h"
#include "game.h"
#include "settings.h"
#include "render.h"
#include <cmath>
#include <algorithm>
#include <climits>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cstdlib>

static bool isReadableCString(const char* text, size_t maxLength = 512) {
    if (!text) return false;
    const uintptr_t address = reinterpret_cast<uintptr_t>(text);

    if (address < 0x10000u || address > 0x7FFEFFFFu) return false;
    if (maxLength == 0) return false;
    __try {
        for (size_t i = 0; i < maxLength; ++i) {
            if (text[i] == '\0') return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

static bool containsNoCase(const char* text, const char* needle) {
    if (!text || !needle || !*needle) return false;
    const size_t n = strlen(needle);
    for (const char* p = text; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i]) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
            if (a != b) break;
            ++i;
        }
        if (i == n) return true;
    }
    return false;
}

static const char* entityModelPath(void* ent) {

    return getEntityModelPath(ent);
}

static bool isSupplyCrateModel(const char* modelPath) {
    if (!modelPath || !*modelPath) return false;

    return containsNoCase(modelPath, "item_item_crate") ||
           containsNoCase(modelPath, "item_beacon_crate") ||
           containsNoCase(modelPath, "item_ammo_crate") ||
           containsNoCase(modelPath, "models/items/ammocrate") ||
           containsNoCase(modelPath, "/items/ammocrate");
}

static bool isSupplyCrateClass(const char* className) {
    if (!className || !*className) return false;
    return containsNoCase(className, "item_item_crate") ||
           containsNoCase(className, "item_dynamic_resupply") ||
           containsNoCase(className, "item_ammo_crate") ||
           containsNoCase(className, "Item_ItemCrate") ||
           containsNoCase(className, "CItem_ItemCrate") ||
           containsNoCase(className, "C_Item_ItemCrate") ||
           containsNoCase(className, "CItem_DynamicResupply") ||
           containsNoCase(className, "C_Item_DynamicResupply") ||
           containsNoCase(className, "CItem_AmmoCrate") ||
           containsNoCase(className, "C_Item_AmmoCrate");
}

static bool isWOrVGrenadeAlias(const char* className, const char* modelPath) {

    if (className && (_stricmp(className, "W_Grenade") == 0 ||
                      _stricmp(className, "V_Grenade") == 0))
        return true;
    if (!modelPath || !*modelPath) return false;
    const char* base = strrchr(modelPath, '/');
    const char* backslash = strrchr(modelPath, '\\');
    if (backslash && (!base || backslash > base)) base = backslash;
    base = base ? base + 1 : modelPath;
    return _stricmp(base, "w_grenade.mdl") == 0 ||
           _stricmp(base, "v_grenade.mdl") == 0 ||
           _stricmp(base, "w_grenade") == 0 ||
           _stricmp(base, "v_grenade") == 0;
}

static bool isWorldWeaponModel(const char* modelPath) {
    if (!modelPath || !*modelPath || isWOrVGrenadeAlias(nullptr, modelPath)) return false;

    static const char* const tokens[] = {

        "w_shotgun", "w_smg1", "w_smg", "w_pistol", "w_revolver", "w_357",
        "w_irifle", "w_ar2", "w_pulse", "w_rocket_launcher", "w_rocketlauncher",
        "w_rpg", "w_crossbow", "w_crowbar", "w_physics", "w_physcannon",
        "w_gravitygun", "w_gravgun", "w_stunbaton", "w_stunstick", "w_slam"
    };
    for (const char* token : tokens) {
        if (containsNoCase(modelPath, token)) return true;
    }
    return false;
}

static bool isGrenadeEspEntity(const char* className, const char* modelPath) {
    if (isWOrVGrenadeAlias(className, modelPath)) return false;
    if (className && *className &&
        (containsNoCase(className, "grenade") ||
         containsNoCase(className, "weapon_frag") ||
         containsNoCase(className, "weaponfrag")))
        return true;

    if (!modelPath || !*modelPath) return false;
    return containsNoCase(modelPath, "fraggrenade") ||
           containsNoCase(modelPath, "grenade_frag") ||
           containsNoCase(modelPath, "grenade_smg") ||
           containsNoCase(modelPath, "smg1_grenade") ||
           containsNoCase(modelPath, "grenade_ar2") ||
           containsNoCase(modelPath, "ar2_grenade") ||
           containsNoCase(modelPath, "grenade_bugbait") ||
           containsNoCase(modelPath, "grenade_helicopter") ||
           containsNoCase(modelPath, "grenade_homer") ||
           containsNoCase(modelPath, "grenade_spit") ||
           containsNoCase(modelPath, "grenade_satchel") ||
           containsNoCase(modelPath, "grenade_tripmine") ||
           containsNoCase(modelPath, "/grenades/");
}

struct EspSkipCacheEntry {
    void* entity = nullptr;
    ULONGLONG checkedAt = 0;
    bool checked = false;
    bool skip = false;
};

static EspSkipCacheEntry g_EspSkipCache[16385]{};

static bool isViewModelName(const char* value) {
    if (!value || !*value) return false;
    return containsNoCase(value, "viewmodel") ||
           containsNoCase(value, "view_model") ||
           containsNoCase(value, "predictedviewmodel") ||
           containsNoCase(value, "predicted_viewmodel");
}

static bool isExcludedEspModelPath(const char* modelPath) {
    if (!modelPath || !*modelPath) return false;
    if (isViewModelName(modelPath) ||
        containsNoCase(modelPath, "models/weapons/v_") ||
        containsNoCase(modelPath, "/viewmodels/")) return true;

    const char* basename = strrchr(modelPath, '/');
    basename = basename ? basename + 1 : modelPath;
    if ((basename[0] == 'w' || basename[0] == 'W') && basename[1] == '_')
        return true;
    const char* backslash = strrchr(basename, '\\');
    if (backslash) basename = backslash + 1;
    return (basename[0] == 'w' || basename[0] == 'W') && basename[1] == '_';
}

struct EspCullCacheEntry {
    void* entity = nullptr;
    unsigned long long generation = 0;
    int screenW = 0;
    int screenH = 0;
    bool checked = false;
    bool outside = false;
};
static EspCullCacheEntry g_EspCullCache[16385]{};
static unsigned long long g_EspCullGeneration = 1;
static unsigned long long g_EspClassifyGeneration = 1;
static ULONGLONG g_EspLastRefreshAt = 0;
static bool g_EspScanInProgress = false;
static constexpr int kEspEntityScanBudget = 192;  
static Vec3 g_EspCullLocalOrigin{};
static bool g_EspCullHasLocalOrigin = false;
static unsigned long long g_EspCullLocalOriginGeneration = 0;

static void beginEspCullPass() {
    ++g_EspCullGeneration;
    if (g_EspCullGeneration == 0) ++g_EspCullGeneration;

    g_EspCullLocalOriginGeneration = 0;
    g_EspCullHasLocalOrigin = false;
}

static void ensureEspCullLocalOrigin() {
    if (g_EspCullLocalOriginGeneration == g_EspCullGeneration) return;
    g_EspCullLocalOriginGeneration = g_EspCullGeneration;
    g_EspCullHasLocalOrigin = false;
    if (g_S.espMaxDistance <= 0.f || o_Origin < 0) return;
    const int localIndex = LocalIndex();
    void* local = localIndex > 0 ? Ent(localIndex) : nullptr;
    if (!local) return;
    __try {
        const char* addr = reinterpret_cast<const char*>(local) + o_Origin;
        if (IsReadableProcessRange(addr, sizeof(Vec3))) {
            const Vec3 pos = Field<Vec3>(local, o_Origin);
            if (std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z)) {
                g_EspCullLocalOrigin = pos;
                g_EspCullHasLocalOrigin = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_EspCullHasLocalOrigin = false; }
}

static void beginEspDataRefreshPass() {

    if (g_EspScanInProgress) return;
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG interval = static_cast<ULONGLONG>(
        (g_S.espUpdateIntervalMs >= 16.f && g_S.espUpdateIntervalMs <= 100.f &&
         std::isfinite(g_S.espUpdateIntervalMs)) ? g_S.espUpdateIntervalMs : 50.f);
    if (g_EspLastRefreshAt == 0 || now < g_EspLastRefreshAt ||
        now - g_EspLastRefreshAt >= interval) {
        ++g_EspClassifyGeneration;
        if (g_EspClassifyGeneration == 0) ++g_EspClassifyGeneration;
        g_EspLastRefreshAt = now;
        g_EspScanInProgress = true;
    }
}

static bool isOutsideEspCullBounds(void* ent, int index, int sw, int sh, bool applyDistance = false) {
    if (!ent || o_Origin < 0 || sw < 2 || sh < 2) return false;
    EspCullCacheEntry* cache = nullptr;
    if (index >= 0 && index < static_cast<int>(_countof(g_EspCullCache))) {
        cache = &g_EspCullCache[index];
        if (cache->checked && cache->entity == ent &&
            cache->generation == g_EspCullGeneration &&
            cache->screenW == sw && cache->screenH == sh)
            return cache->outside;
    }

    bool outside = false;
    Vec3 origin{};
    __try {
        if (!IsReadableProcessRange(reinterpret_cast<const char*>(ent) + o_Origin, sizeof(Vec3)))
            return false;
        origin = Field<Vec3>(ent, o_Origin);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) {
        outside = true;
    } else {

        if (applyDistance && g_S.espMaxDistance > 0.f) ensureEspCullLocalOrigin();
        if (applyDistance && g_S.espMaxDistance > 0.f && g_EspCullHasLocalOrigin) {
            const float dx = origin.x - g_EspCullLocalOrigin.x;
            const float dy = origin.y - g_EspCullLocalOrigin.y;
            const float dz = origin.z - g_EspCullLocalOrigin.z;
            const float maxDistSq = g_S.espMaxDistance * g_S.espMaxDistance;
            if (dx * dx + dy * dy + dz * dz > maxDistSq) outside = true;
        }
        if (!outside) {
            int x = 0, y = 0;
            if (!W2S(origin, sw, sh, x, y)) outside = true;
            else {

                const int marginX = (sw / 6 > 24) ? sw / 6 : 24;
                const int marginY = (sh / 6 > 24) ? sh / 6 : 24;
                outside = x < -marginX || x > sw + marginX ||
                          y < -marginY || y > sh + marginY;
            }
        }
    }

    if (cache) {
        cache->entity = ent;
        cache->generation = g_EspCullGeneration;
        cache->screenW = sw;
        cache->screenH = sh;
        cache->checked = true;
        cache->outside = outside;
    }
    return outside;
}

struct EspClassifyCacheEntry {
    void* entity = nullptr;
    unsigned long long generation = 0;
    bool checked = false;
    bool result = false;
    Target target{};
    EntityRelation relation = ER_Neutral;
};
static EspClassifyCacheEntry g_EspClassifyCache[16385]{};

static bool classifyEspCached(void* ent, int index, Target& out, EntityRelation* outRelation = nullptr) {
    if (!ent || index < 0 || index >= static_cast<int>(_countof(g_EspClassifyCache)))
        return classify(ent, index, out);
    EspClassifyCacheEntry& cache = g_EspClassifyCache[index];
    if (cache.checked && cache.entity == ent && cache.generation == g_EspClassifyGeneration) {
        if (cache.result) {
            out = cache.target;
            if (outRelation) *outRelation = cache.relation;
        }
        return cache.result;
    }
    Target candidate{};
    const bool result = classify(ent, index, candidate);
    cache.entity = ent;
    cache.generation = g_EspClassifyGeneration;
    cache.checked = true;
    cache.result = result;
    if (result) {
        cache.target = candidate;
        out = candidate;

        __try { cache.relation = classifyRelation(ent, index, &candidate); }
        __except (EXCEPTION_EXECUTE_HANDLER) { cache.relation = ER_Neutral; }
        if (outRelation) *outRelation = cache.relation;
    }
    return result;
}

struct EspRenderCandidate { int index; void* entity; bool classified; bool item; };
static EspRenderCandidate g_EspRenderCandidateBuffers[2][16385]{};
static EspRenderCandidate* g_EspRenderCandidates = g_EspRenderCandidateBuffers[0];
static EspRenderCandidate* g_EspRenderCandidateBuild = g_EspRenderCandidateBuffers[1];
static int g_EspRenderCandidateCount = 0;
static int g_EspRenderCandidateBuildCount = 0;
static int g_EspRenderCandidateCursor = 0;
static bool g_EspRenderCandidateBuilding = false;
static unsigned long long g_EspRenderCandidateGeneration = 0;
static void* g_EspRenderCandidateList = nullptr;
static void* g_EspRenderCandidateLocal = nullptr;
static DWORD g_EspRenderCandidateOptions = 0xFFFFFFFFu;
static int g_EspRenderCandidateScreenW = 0;
static int g_EspRenderCandidateScreenH = 0;
static int g_EspRenderCandidateFirstIndex = -1;
static int g_EspRenderCandidateLastIndex = -1;
static bool g_EspRenderCandidateBuilt = false;
static unsigned long long g_EspRenderCandidateBuildGeneration = 0;
static void* g_EspRenderCandidateBuildList = nullptr;
static void* g_EspRenderCandidateBuildLocal = nullptr;
static DWORD g_EspRenderCandidateBuildOptions = 0xFFFFFFFFu;
static int g_EspRenderCandidateBuildScreenW = 0;
static int g_EspRenderCandidateBuildScreenH = 0;
static int g_EspRenderCandidateBuildFirstIndex = -1;
static int g_EspRenderCandidateBuildLastIndex = -1;

static bool shouldSkipEspEntity(void* ent, int index) {
    if (!ent || index < 0 || index >= static_cast<int>(_countof(g_EspSkipCache)))
        return false;

    const ULONGLONG now = GetTickCount64();
    EspSkipCacheEntry& cache = g_EspSkipCache[index];
    if (cache.checked && cache.entity == ent && now >= cache.checkedAt &&
        now - cache.checkedAt < 750)
        return cache.skip;

    bool skip = false;
    __try {
        ClientClass* cc = ClassOf(ent);
        const char* className = (cc && IsReadableProcessRange(cc, sizeof(ClientClass)))
            ? cc->m_pNetworkName : nullptr;
        if (!isReadableCString(className)) className = nullptr;
        if (isWOrVGrenadeAlias(className, nullptr)) {
            skip = true;
        } else if (className) {
            skip = isViewModelName(className);
        }

        if (!skip) {
            const char* modelPath = entityModelPath(ent);
            if (!isReadableCString(modelPath)) modelPath = nullptr;

            const bool modelIsView = modelPath &&
                (isViewModelName(modelPath) ||
                 containsNoCase(modelPath, "models/weapons/v_") ||
                 containsNoCase(modelPath, "/viewmodels/"));
            if (isWOrVGrenadeAlias(nullptr, modelPath) || modelIsView) {
                skip = true;
            } else if (isGrenadeEspEntity(className, modelPath)) {

                skip = false;
            } else if (modelPath) {
                skip = isExcludedEspModelPath(modelPath);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {

        skip = false;
    }

    cache.entity = ent;
    cache.checkedAt = now;
    cache.checked = true;
    cache.skip = skip;
    return skip;
}

static bool isKnownPickupModel(const char* modelPath) {
    if (!modelPath || !*modelPath) return false;

    static const char* const tokens[] = {
        "models/items/healthkit", "models/items/healthvial", "models/items/battery",
        "models/items/item_item_crate", "models/items/item_beacon_crate",
        "models/items/item_ammo_crate", "models/items/ammocrate",
        "models/items/boxsrounds", "models/items/boxmrounds",
        "models/items/boxlrounds", "models/items/boxbuckshot",
        "models/items/357ammobox", "models/items/ar2_grenade",
        "models/items/ar2_grenade.mdl",
        "models/items/ammo_357", "models/items/ammo_pistol", "models/items/ammo_smg1",
        "models/items/ammo_ar2", "models/items/ammo_crossbow", "models/items/rpg_round"
    };
    for (const char* token : tokens) {
        if (containsNoCase(modelPath, token)) return true;
    }

    if (containsNoCase(modelPath, "/items/") &&
        (containsNoCase(modelPath, "healthkit") || containsNoCase(modelPath, "healthvial") ||
         containsNoCase(modelPath, "battery") || containsNoCase(modelPath, "ammobox") ||
         containsNoCase(modelPath, "ammo_") || containsNoCase(modelPath, "boxsrounds") ||
         containsNoCase(modelPath, "boxmrounds") || containsNoCase(modelPath, "boxlrounds") ||
         containsNoCase(modelPath, "boxbuckshot") || containsNoCase(modelPath, "rpg_round")))
        return true;
    return false;
}

static bool isWorldItem(void* ent, const char*& className, const char*& modelPath) {
    className = nullptr;
    modelPath = nullptr;

    if (!IsSaneClientEntity(ent)) return false;

    __try {
        const char* candidateModel = entityModelPath(ent);
        if (isReadableCString(candidateModel)) modelPath = candidateModel;

        ClientClass* cc = ClassOf(ent);
        if (cc && IsReadableProcessRange(cc, sizeof(ClientClass))) {
            const char* candidateClass = cc->m_pNetworkName;
            if (isReadableCString(candidateClass)) className = candidateClass;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        className = nullptr;
        modelPath = nullptr;
        return false;
    }
    if ((!className || !*className) && (!modelPath || !*modelPath)) return false;
    if (isWOrVGrenadeAlias(className, modelPath)) return false;

    if ((className && (containsNoCase(className, "healthvial") ||
                       containsNoCase(className, "health_vial"))) ||
        (modelPath && (containsNoCase(modelPath, "healthvial") ||
                       containsNoCase(modelPath, "health_vial"))))
        return true;

    if (isGrenadeEspEntity(className, modelPath))
        return true;

    if (modelPath) {
        if (containsNoCase(modelPath, "wood_crate001a") ||
            containsNoCase(modelPath, "wood_crate002a") ||
            containsNoCase(modelPath, "PlasticCrate01a") ||
            containsNoCase(modelPath, "plasticcrate") ||
            containsNoCase(modelPath, "wood_crate") ||
            containsNoCase(modelPath, "oildrum") ||
            containsNoCase(modelPath, "cardboard") ||
            containsNoCase(modelPath, "props_junk"))
            return false;
    }

    if (isSupplyCrateModel(modelPath) || isSupplyCrateClass(className))
        return true;

    if (isKnownPickupModel(modelPath))
        return true;

    if (isWorldWeaponModel(modelPath))
        return true;
    if (className) {
        if (containsNoCase(className, "CBaseCombatWeapon") ||
            containsNoCase(className, "CWeapon"))
            return true;

        if (containsNoCase(className, "item_health") ||
            containsNoCase(className, "item_battery") ||
            containsNoCase(className, "item_suit") ||
            containsNoCase(className, "item_ammo") ||
            containsNoCase(className, "item_box") ||
            containsNoCase(className, "item_rpg_round") ||
            containsNoCase(className, "item_ar2") ||
            containsNoCase(className, "weapon_"))
            return true;
    }

    return false;
}

struct WorldItemCacheEntry {
    void* entity = nullptr;
    DWORD bucket = 0;
    const char* className = nullptr;
    const char* modelPath = nullptr;
    bool result = false;
};
static WorldItemCacheEntry g_WorldItemCache[16385]{};

static bool isWorldItemCached(void* ent, int index, const char*& className,
                              const char*& modelPath) {
    className = nullptr;
    modelPath = nullptr;
    if (!ent || index < 0 || index >= static_cast<int>(_countof(g_WorldItemCache)))
        return false;

    const DWORD bucket = GetTickCount() / 250u;
    WorldItemCacheEntry& entry = g_WorldItemCache[index];
    if (entry.entity == ent && entry.bucket == bucket) {
        className = entry.className;
        modelPath = entry.modelPath;
        return entry.result;
    }
    const bool result = isWorldItem(ent, className, modelPath);
    entry.entity = ent;
    entry.bucket = bucket;
    entry.className = className;
    entry.modelPath = modelPath;
    entry.result = result;
    return result;
}

static DWORD espCandidateOptionsKey() {
    DWORD key = 0;
    if (g_S.espEntities) key |= 1u << 0;
    if (g_S.espItems) key |= 1u << 1;
    if (g_S.espWires) key |= 1u << 2;
    if (g_S.espWiresPlayers) key |= 1u << 3;
    if (g_S.espWiresNpcs) key |= 1u << 4;
    if (g_S.espWiresItems) key |= 1u << 5;
    return key;
}

static void refreshEspRenderCandidates(void* local, int sw, int sh) {
    const void* list = g_EntList;
    const DWORD options = espCandidateOptionsKey();
    const bool needPlayers = g_S.espEntities ||
        (g_S.espWires && g_S.espWiresPlayers);
    const bool needWorld = g_S.espEntities || g_S.espItems ||
        (g_S.espWires && (g_S.espWiresNpcs || g_S.espWiresItems));
    const int maxClients = MaxClients();
    const int maxEntities = MaxEnt();
    int first = 1;
    int last = 0;
    if (needPlayers && needWorld) { first = 1; last = maxEntities; }
    else if (needPlayers) { first = 1; last = maxClients; }
    else if (needWorld) { first = maxClients + 1; last = maxEntities; }
    if (last > maxEntities) last = maxEntities;
    if (last < first) last = first - 1;

    if (g_EspRenderCandidateBuilt && !g_EspRenderCandidateBuilding &&
        g_EspRenderCandidateGeneration == g_EspClassifyGeneration &&
        g_EspRenderCandidateList == list && g_EspRenderCandidateLocal == local &&
        g_EspRenderCandidateOptions == options && g_EspRenderCandidateScreenW == sw &&
        g_EspRenderCandidateScreenH == sh &&
        g_EspRenderCandidateFirstIndex == first && g_EspRenderCandidateLastIndex == last)
        return;

    const bool hardContextChange =
        (g_EspRenderCandidateBuilt &&
         (g_EspRenderCandidateList != list || g_EspRenderCandidateLocal != local ||
          g_EspRenderCandidateScreenW != sw || g_EspRenderCandidateScreenH != sh));
    if (hardContextChange) {

        g_EspRenderCandidateCount = 0;
        g_EspRenderCandidateBuilt = false;
        g_EspRenderCandidateBuilding = false;
    }

    const bool buildMatch = g_EspRenderCandidateBuilding &&
        g_EspRenderCandidateBuildGeneration == g_EspClassifyGeneration &&
        g_EspRenderCandidateBuildList == list &&
        g_EspRenderCandidateBuildLocal == local &&
        g_EspRenderCandidateBuildOptions == options &&
        g_EspRenderCandidateBuildScreenW == sw &&
        g_EspRenderCandidateBuildScreenH == sh &&
        g_EspRenderCandidateBuildFirstIndex == first &&
        g_EspRenderCandidateBuildLastIndex == last;
    if (!buildMatch) {
        g_EspRenderCandidateCursor = first;
        g_EspRenderCandidateBuildCount = 0;
        g_EspRenderCandidateBuildGeneration = g_EspClassifyGeneration;
        g_EspRenderCandidateBuildList = const_cast<void*>(list);
        g_EspRenderCandidateBuildLocal = local;
        g_EspRenderCandidateBuildOptions = options;
        g_EspRenderCandidateBuildScreenW = sw;
        g_EspRenderCandidateBuildScreenH = sh;
        g_EspRenderCandidateBuildFirstIndex = first;
        g_EspRenderCandidateBuildLastIndex = last;
        g_EspRenderCandidateBuilding = true;
        g_EspLastRefreshAt = GetTickCount64();  
        g_EspScanInProgress = true;
    }

    const bool wantClassified = g_S.espEntities ||
        (g_S.espWires && (g_S.espWiresPlayers || g_S.espWiresNpcs));
    const bool wantItems = g_S.espItems || (g_S.espWires && g_S.espWiresItems);
    int processed = 0;

    for (; g_EspRenderCandidateCursor <= g_EspRenderCandidateBuildLastIndex &&
           processed < kEspEntityScanBudget &&
           g_EspRenderCandidateBuildCount < 16385;
         ++g_EspRenderCandidateCursor, ++processed) {
        const int index = g_EspRenderCandidateCursor;
        void* ent = Ent(index);
        if (!ent || ent == local || Ent(index) != ent ||
            isOutsideEspCullBounds(ent, index, sw, sh, true)) continue;
        if (Dormant(ent)) continue;
        if (index > maxClients && shouldSkipEspEntity(ent, index)) {
            bool isHealthVial = false;
            __try {
                ClientClass* cc = ClassOf(ent);
                const char* name = (cc && IsReadableProcessRange(cc, sizeof(ClientClass)))
                    ? cc->m_pNetworkName : nullptr;
                isHealthVial = isReadableCString(name) &&
                    (containsNoCase(name, "healthvial") ||
                     containsNoCase(name, "health_vial"));
                if (!isHealthVial) {
                    const char* model = entityModelPath(ent);
                    isHealthVial = isReadableCString(model) &&
                        (containsNoCase(model, "healthvial") ||
                         containsNoCase(model, "health_vial"));
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                isHealthVial = false;
            }
            if (!isHealthVial) continue;
        }

        bool classified = false;
        bool item = false;
        Target target{};
        if (wantClassified) classified = classifyEspCached(ent, index, target);
        if (wantItems) {
            const char* className = nullptr;
            const char* modelPath = nullptr;
            item = isWorldItemCached(ent, index, className, modelPath);
        }

        bool relevantClassified = g_S.espEntities && classified;
        if (!relevantClassified && g_S.espWires && classified) {
            relevantClassified = (target.player && g_S.espWiresPlayers) ||
                                 (!target.player && g_S.espWiresNpcs);
        }
        const bool relevantItem = item && (g_S.espItems || (g_S.espWires && g_S.espWiresItems));
        if (!relevantClassified && !relevantItem) continue;

        EspRenderCandidate& candidate = g_EspRenderCandidateBuild[g_EspRenderCandidateBuildCount++];
        candidate.index = index;
        candidate.entity = ent;
        candidate.classified = classified;
        candidate.item = item;
    }

    if (g_EspRenderCandidateCursor > g_EspRenderCandidateBuildLastIndex ||
        g_EspRenderCandidateBuildCount >= 16385) {
        std::swap(g_EspRenderCandidates, g_EspRenderCandidateBuild);
        g_EspRenderCandidateCount = g_EspRenderCandidateBuildCount;
        g_EspRenderCandidateGeneration = g_EspRenderCandidateBuildGeneration;
        g_EspRenderCandidateList = g_EspRenderCandidateBuildList;
        g_EspRenderCandidateLocal = g_EspRenderCandidateBuildLocal;
        g_EspRenderCandidateOptions = g_EspRenderCandidateBuildOptions;
        g_EspRenderCandidateScreenW = g_EspRenderCandidateBuildScreenW;
        g_EspRenderCandidateScreenH = g_EspRenderCandidateBuildScreenH;
        g_EspRenderCandidateFirstIndex = g_EspRenderCandidateBuildFirstIndex;
        g_EspRenderCandidateLastIndex = g_EspRenderCandidateBuildLastIndex;
        g_EspRenderCandidateBuilt = true;
        g_EspRenderCandidateBuilding = false;

        g_EspScanInProgress = false;
    }
}

static void getWorldItemLabel(const char* className, const char* modelPath,
                              wchar_t* out, size_t outCount) {
    if (!out || outCount == 0) return;

    if ((className && (containsNoCase(className, "healthvial") ||
                       containsNoCase(className, "health_vial"))) ||
        (modelPath && (containsNoCase(modelPath, "healthvial") ||
                       containsNoCase(modelPath, "health_vial")))) {
        wcsncpy(out, L"Health Vial", outCount - 1);
        out[outCount - 1] = 0;
        return;
    }

    const char* hay = modelPath && *modelPath ? modelPath : (className ? className : "");
    struct Map { const char* token; const wchar_t* label; };
    static const Map maps[] = {

        {"item_item_crate", L"Supply Crate"},
        {"item_beacon_crate", L"Beacon Crate"},
        {"item_ammo_crate", L"Ammo Crate"},
        {"item_dynamic_resupply", L"Resupply"},
        {"ammocrate", L"Ammo Crate"},

        {"healthkit", L"Health Kit"},
        {"health_kit", L"Health Kit"},
        {"item_healthkit", L"Health Kit"},
        {"healthvial", L"Health Vial"},
        {"health_vial", L"Health Vial"},
        {"item_healthvial", L"Health Vial"},

        {"npc_grenade_frag", L"Frag Grenade"},
        {"grenade_frag", L"Frag Grenade"},
        {"fraggrenade", L"Frag Grenade"},
        {"grenade_smg", L"SMG Grenade"},
        {"smg1_grenade", L"SMG Grenade"},
        {"grenade_ar2", L"AR2 Orb"},
        {"grenade_bugbait", L"Bugbait"},
        {"grenade_helicopter", L"Hunter-Chopper Grenade"},
        {"grenade_homer", L"Homing Grenade"},
        {"grenade_spit", L"Antlion Spit"},
        {"grenade_satchel", L"Satchel Charge"},
        {"grenade_tripmine", L"Tripmine"},
        {"weapon_frag", L"Grenade"},
        {"battery", L"Battery"},
        {"item_battery", L"Battery"},
        {"suit_charge", L"Suit Battery"},
        {"suit_charge", L"Suit Battery"},
        {"item_suit", L"HEV Suit"},

        {"boxsrounds", L"Pistol Rounds"},
        {"box_srounds", L"Pistol Rounds"},
        {"item_box_srounds", L"Pistol Rounds"},
        {"boxmrounds", L"SMG Rounds"},
        {"box_mrounds", L"SMG Rounds"},
        {"item_box_mrounds", L"SMG Rounds"},
        {"boxlrounds", L"AR2 Rounds"},
        {"box_lrounds", L"AR2 Rounds"},
        {"item_box_lrounds", L"AR2 Rounds"},
        {"boxbuckshot", L"Buckshot"},
        {"box_buckshot", L"Buckshot"},
        {"item_box_buckshot", L"Buckshot"},
        {"357ammobox", L".357 Rounds"},
        {"item_ammo_357", L".357 Rounds"},
        {"ar2_grenade", L"AR2 Orb"},
        {"item_ammo_ar2_altfire", L"AR2 Orb"},
        {"item_ammo_ar2", L"AR2 Ammo"},
        {"item_ammo_pistol", L"Pistol Ammo"},
        {"item_ammo_smg1", L"SMG Ammo"},
        {"item_ammo_smg1_grenade", L"SMG Grenade"},
        {"item_rpg_round", L"RPG Round"},
        {"item_box_flare_rounds", L"Flare Rounds"},
        {"item_flare_round", L"Flare"},
        {"item_ammo_crossbow", L"Crossbow Bolts"},
        {"crossbow_bolt", L"Crossbow Bolts"},

        {"w_shotgun", L"Shotgun"}, {"weapon_shotgun", L"Shotgun"}, {"cweaponshotgun", L"Shotgun"},
        {"w_smg1", L"SMG"}, {"weapon_smg1", L"SMG"}, {"cweaponsmg1", L"SMG"},
        {"w_smg", L"SMG"}, {"weapon_smg", L"SMG"},
        {"w_pistol", L"Pistol"}, {"weapon_pistol", L"Pistol"}, {"cweaponpistol", L"Pistol"},
        {"w_revolver", L"Revolver"}, {"weapon_revolver", L"Revolver"}, {"cweaponrevolver", L"Revolver"},
        {"w_357", L".357 Magnum"}, {"weapon_357", L".357 Magnum"}, {"cweapon357", L".357 Magnum"},
        {"w_irifle", L"Pulse Rifle"}, {"weapon_irifle", L"Pulse Rifle"}, {"cweaponar2", L"Pulse Rifle"},
        {"weapon_ar2", L"Pulse Rifle"}, {"w_ar2", L"Pulse Rifle"}, {"w_pulse", L"Pulse Rifle"},
        {"w_rocket_launcher", L"RPG"}, {"w_rocketlauncher", L"RPG"},
        {"weapon_rpg", L"RPG"}, {"w_rpg", L"RPG"}, {"cweaponrpg", L"RPG"},
        {"weapon_frag", L"Grenade"}, {"cweaponfrag", L"Grenade"},
        {"w_crossbow", L"Crossbow"}, {"weapon_crossbow", L"Crossbow"}, {"cweaponcrossbow", L"Crossbow"},
        {"w_crowbar", L"Crowbar"}, {"weapon_crowbar", L"Crowbar"}, {"cweaponcrowbar", L"Crowbar"},
        {"w_physics", L"Gravity Gun"}, {"w_physcannon", L"Gravity Gun"},
        {"w_gravitygun", L"Gravity Gun"}, {"w_gravgun", L"Gravity Gun"},
        {"weapon_physcannon", L"Gravity Gun"}, {"weapon_gravitygun", L"Gravity Gun"}, {"cweaponphyscannon", L"Gravity Gun"},
        {"w_stunbaton", L"Stunstick"}, {"w_stunstick", L"Stunstick"},
        {"weapon_stunstick", L"Stunstick"}, {"weapon_stunbaton", L"Stunstick"}, {"cweaponstunstick", L"Stunstick"},
        {"w_slam", L"SLAM"}, {"weapon_slam", L"SLAM"}, {"cweaponslam", L"SLAM"},
        {"w_annabelle", L"Annabelle"}, {"weapon_annabelle", L"Annabelle"},
        {"w_bugbait", L"Bugbait"}, {"weapon_bugbait", L"Bugbait"},

        {"ammo_pistol", L"Pistol Ammo"}, {"ammo_smg1", L"SMG Ammo"},
        {"ammo_ar2", L"AR2 Ammo"}, {"ammo_357", L".357 Ammo"},
        {"ammo_buckshot", L"Buckshot"}, {"ammo_rpg_round", L"RPG Round"},
        {"ammo_crossbow", L"Crossbow Bolts"},
    };

    for (const Map& m : maps) {
        if (containsNoCase(hay, m.token) || (className && containsNoCase(className, m.token))) {
            wcsncpy(out, m.label, outCount - 1);
            out[outCount - 1] = 0;
            return;
        }
    }
    const char* source = (modelPath && *modelPath) ? modelPath : className;
    if (!source || !*source) {
        wcsncpy(out, L"item", outCount - 1);
        out[outCount - 1] = 0;
        return;
    }
    const char* base = strrchr(source, '/');
    base = base ? base + 1 : source;
    wchar_t tmp[96]{};
    size_t j = 0;
    for (size_t i = 0; base[i] && j + 1 < _countof(tmp); ++i) {
        char c = base[i];
        if (c == '.') break;
        tmp[j++] = static_cast<unsigned char>(c);
    }
    if (!j) { tmp[0] = L'i'; tmp[1] = L't'; tmp[2] = L'e'; tmp[3] = L'm'; j = 4; }
    tmp[j] = 0;
    wcsncpy(out, tmp, outCount - 1);
    out[outCount - 1] = 0;
}

static void drawItemEsp(void* ent, int index, int sw, int sh) {
    if (!ent || Dormant(ent) || o_Origin < 0 ||
        isOutsideEspCullBounds(ent, index, sw, sh, true) || shouldSkipEspEntity(ent, index)) return;
    const char* className = nullptr;
    const char* modelPath = nullptr;
    if (!isWorldItemCached(ent, index, className, modelPath)) return;

    const Vec3 origin = Field<Vec3>(ent, o_Origin);
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) return;
    Vec3 mins = (o_Mins >= 0) ? Field<Vec3>(ent, o_Mins) : Vec3(-12.f, -12.f, 0.f);
    Vec3 maxs = (o_Maxs >= 0) ? Field<Vec3>(ent, o_Maxs) : Vec3(12.f, 12.f, 24.f);
    if (!std::isfinite(mins.x) || !std::isfinite(maxs.x) ||
        (maxs.x - mins.x) < 1.f || (maxs.y - mins.y) < 1.f) {
        mins = Vec3(-12.f, -12.f, 0.f);
        maxs = Vec3(12.f, 12.f, 24.f);
    }

    int minx = INT_MAX, miny = INT_MAX, maxx = INT_MIN, maxy = INT_MIN;
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 point = origin + Vec3((corner & 1) ? maxs.x : mins.x,
                                         (corner & 2) ? maxs.y : mins.y,
                                         (corner & 4) ? maxs.z : mins.z);
        int px = 0, py = 0;
        if (!W2S(point, sw, sh, px, py)) return;
        minx = (px < minx) ? px : minx;
        miny = (py < miny) ? py : miny;
        maxx = (px > maxx) ? px : maxx;
        maxy = (py > maxy) ? py : maxy;
    }
    if (maxx < 0 || maxy < 0 || minx >= sw || miny >= sh) return;

    int cr, cg, cb;
    relationColor(ER_Item, cr, cg, cb);
    setColor(cr, cg, cb, 230);
    if (g_S.espBox) rect(minx, miny, maxx, maxy);
    if (g_S.espName) {
        wchar_t label[80]{};
        getWorldItemLabel(className, modelPath, label, _countof(label));
        text(minx, miny - 13, cr, cg, cb, label);
    }
}

static bool projectEntityBounds(const Target& t, int sw, int sh,
                                int& minx, int& miny, int& maxx, int& maxy) {
    minx = INT_MAX; miny = INT_MAX; maxx = INT_MIN; maxy = INT_MIN;
    bool allCornersProjected = true;
    for (int c = 0; c < 8; ++c) {
        Vec3 p = t.origin + Vec3((c & 1) ? t.maxs.x : t.mins.x,
                                (c & 2) ? t.maxs.y : t.mins.y,
                                (c & 4) ? t.maxs.z : t.mins.z);
        int x = 0, y = 0;
        if (!W2S(p, sw, sh, x, y)) { allCornersProjected = false; break; }
        if (x < minx) minx = x;
        if (y < miny) miny = y;
        if (x > maxx) maxx = x;
        if (y > maxy) maxy = y;
    }

    if (!allCornersProjected) {
        int bx = 0, by = 0, tx = 0, ty = 0;
        const Vec3 bottom = t.origin + Vec3(0.f, 0.f, t.mins.z);
        const Vec3 top = t.origin + Vec3(0.f, 0.f, t.maxs.z);
        if (!W2S(bottom, sw, sh, bx, by) || !W2S(top, sw, sh, tx, ty)) return false;
        int h = by > ty ? by - ty : ty - by;
        if (h < 6) return false;
        int halfW = h / 4;
        if (halfW < 3) halfW = 3;
        int cx = (bx + tx) / 2;
        minx = cx - halfW; maxx = cx + halfW;
        miny = by < ty ? by : ty; maxy = by > ty ? by : ty;
    }

    if (maxx < 0 || maxy < 0 || minx >= sw || miny >= sh) return false;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= sw) maxx = sw - 1;
    if (maxy >= sh) maxy = sh - 1;
    return maxx > minx && maxy > miny;
}

struct EspBoneSegment { Vec3 a; Vec3 b; };
struct EspBoneCacheEntry {
    void* entity = nullptr;
    int index = -1;
    ULONGLONG builtAt = 0;
    bool checked = false;
    bool valid = false;
    int segmentCount = 0;
    EspBoneSegment segments[kMaxBones]{};
};
static EspBoneCacheEntry g_EspBoneCache[512]{};
static constexpr ULONGLONG kEspBoneCacheLifetimeMs = 33;

static const EspBoneCacheEntry* getEspBoneSegments(const Target* target) {
    if (!target || !target->ent || target->index < 0) return nullptr;
    const ULONGLONG now = GetTickCount64();
    const size_t slot = (reinterpret_cast<uintptr_t>(target->ent) >> 4) % _countof(g_EspBoneCache);
    EspBoneCacheEntry& cache = g_EspBoneCache[slot];
    if (cache.checked && cache.entity == target->ent && cache.index == target->index &&
        now >= cache.builtAt && now - cache.builtAt < kEspBoneCacheLifetimeMs)
        return cache.valid ? &cache : nullptr;

    cache.entity = target->ent;
    cache.index = target->index;
    cache.builtAt = now;
    cache.checked = true;
    cache.valid = false;
    cache.segmentCount = 0;

    Matrix3x4 bones[kMaxBones];
    char* hdr = setupBones(target->ent, bones);
    if (!hdr || !IsReadableProcessRange(hdr, 180)) return nullptr;
    const int nb = Field<int>(hdr, 156);
    const int bi = Field<int>(hdr, 160);
    const int len = Field<int>(hdr, 76);
    if (nb < 1 || nb > kMaxBones || len < 200 || len > (1 << 26) || bi < 0 ||
        static_cast<long long>(bi) + static_cast<long long>(kBoneStride) * nb > len ||
        !IsReadableProcessRange(hdr + bi, static_cast<size_t>(kBoneStride) * nb))
        return nullptr;

    for (int b = 0; b < nb && cache.segmentCount < kMaxBones; ++b) {
        char* bone = hdr + bi + kBoneStride * b;
        if (!(Field<int>(bone, 160) & 0x100)) continue;
        int parent = Field<int>(bone, 4);
        while (parent >= 0 && parent < nb &&
               !(Field<int>(hdr + bi + kBoneStride * parent, 160) & 0x100))
            parent = Field<int>(hdr + bi + kBoneStride * parent, 4);
        if (parent < 0 || parent >= nb) continue;
        EspBoneSegment& segment = cache.segments[cache.segmentCount++];
        segment.a = BonePos(bones[b]);
        segment.b = BonePos(bones[parent]);
    }
    cache.valid = cache.segmentCount > 0;
    return cache.valid ? &cache : nullptr;
}

static void drawBonesUnsafe(const Target* target, int sw, int sh, EntityRelation rel) {
    if (!target || !g_S.espBones) return;
    const EspBoneCacheEntry* cached = getEspBoneSegments(target);
    if (!cached) return;
    int cr, cg, cb;
    relationColor(rel, cr, cg, cb);
    setColor(cr, cg, cb, 255);
    for (int i = 0; i < cached->segmentCount; ++i) {
        const EspBoneSegment& segment = cached->segments[i];
        int x0, y0, x1, y1;
        if (W2S(segment.a, sw, sh, x0, y0) && W2S(segment.b, sw, sh, x1, y1))
            line(x0, y0, x1, y1);
    }
}

static void drawBonesSafe(const Target* target, int sw, int sh, EntityRelation rel) {
    __try { drawBonesUnsafe(target, sw, sh, rel); }
    __except (EXCEPTION_EXECUTE_HANDLER) {   }
}

static void drawClassifiedEntityUnsafe(void* ent, int idx, int sw, int sh) {
    Target t{};
    EntityRelation rel = ER_Neutral;
    if (!classifyEspCached(ent, idx, t, &rel)) return;
    drawBonesSafe(&t, sw, sh, rel);

    int minx, miny, maxx, maxy;
    if (!projectEntityBounds(t, sw, sh, minx, miny, maxx, maxy)) return;

    int cr, cg, cb;
    relationColor(rel, cr, cg, cb);

    if (g_S.espBox) {
        setColor(cr, cg, cb, 255);
        rect(minx, miny, maxx, maxy);
    }

    if (t.player && g_S.espHealth && t.health >= 0) {
        int hp = t.health > 100 ? 100 : t.health;
        if (hp < 0) hp = 0;
        int bh = (maxy - miny) * hp / 100;
        setColor(0, 0, 0, 200);
        VCall<void, int, int, int, int>(g_Surface, 12, minx - 6, miny - 1, minx - 1, maxy + 1);
        setColor(255 - hp * 255 / 100, hp * 255 / 100, 0, 255);
        VCall<void, int, int, int, int>(g_Surface, 12, minx - 5, maxy - bh, minx - 2, maxy);
    }

    if (g_S.espName) {
        wchar_t buf[96]{};
        if (t.player && t.health >= 0) swprintf(buf, 96, L"%hs [%d]", t.name, t.health);
        else swprintf(buf, 96, L"%hs", t.name[0] ? t.name : "NPC");
        text(minx, miny - 14, cr, cg, cb, buf);
    }
}

static void drawClassifiedEntitySafe(void* ent, int idx, int sw, int sh) {
    __try { drawClassifiedEntityUnsafe(ent, idx, sw, sh); }
    __except (EXCEPTION_EXECUTE_HANDLER) {   }
}

static void drawClassifiedIndexUnsafe(int idx, int maxClients, void* local, int sw, int sh) {
    void* ent = Ent(idx);
    if (!ent || ent == local || isOutsideEspCullBounds(ent, idx, sw, sh, true)) return;

    if (idx > maxClients && shouldSkipEspEntity(ent, idx)) return;
    if (g_S.espEntities) drawClassifiedEntitySafe(ent, idx, sw, sh);
}

static void drawClassifiedIndexSafe(int idx, int maxClients, void* local, int sw, int sh) {
    __try { drawClassifiedIndexUnsafe(idx, maxClients, local, sw, sh); }
    __except (EXCEPTION_EXECUTE_HANDLER) {   }
}

void drawEdgebugHitbox() {
    if (!InGame() || !g_S.edgebug || !g_S.edgebugIndicator ||
        o_Origin < 0 || o_Mins < 0 || o_Maxs < 0) return;
    void* local = Ent(LocalIndex());
    if (!local) return;
    Vec3 origin{}, mins{}, maxs{};
    __try {
        origin = Field<Vec3>(local, o_Origin);
        mins = Field<Vec3>(local, o_Mins);
        maxs = Field<Vec3>(local, o_Maxs);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        !std::isfinite(mins.z) || !std::isfinite(maxs.z)) return;

    static void* debugOverlay = nullptr;
    if (!debugOverlay) debugOverlay = getIface("engine.dll", "VDebugOverlay004");
    if (!debugOverlay) return;
    const bool detected = InterlockedCompareExchange(&g_EdgebugDetected, 0, 0) != 0;
    const int r = detected ? 35 : 255;
    const int g = detected ? 235 : 255;
    const int b = detected ? 75 : 255;
    const QAngle angles(0.f, 0.f, 0.f);
    __try {
        VCall<void, const Vec3&, const Vec3&, const Vec3&, const QAngle&,
              int, int, int, int, float>(debugOverlay, 1, origin, mins, maxs,
                                         angles, r, g, b, 230, 0.04f);
    } __except (EXCEPTION_EXECUTE_HANDLER) { debugOverlay = nullptr; }
}

static void drawThickLine(int x0, int y0, int x1, int y1, int width, int r, int g, int b, int a) {
    if (width < 1) width = 1;
    setColor(r, g, b, a);
    line(x0, y0, x1, y1);
    if (width <= 1) return;
    const float dx = static_cast<float>(x1 - x0);
    const float dy = static_cast<float>(y1 - y0);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.f) return;
    const float nx = -dy / len;
    const float ny = dx / len;
    const int half = width / 2;
    for (int o = -half; o <= half; ++o) {
        if (o == 0) continue;
        const int ox = static_cast<int>(nx * o + (o > 0 ? 0.5f : -0.5f));
        const int oy = static_cast<int>(ny * o + (o > 0 ? 0.5f : -0.5f));
        line(x0 + ox, y0 + oy, x1 + ox, y1 + oy);
    }
}

static void drawEspWires(void* local, int sw, int sh) {
    if (!g_S.espWires || !local || o_Origin < 0) return;
    const bool wantPlayers = g_S.espWiresPlayers;
    const bool wantNpcs = g_S.espWiresNpcs;
    const bool wantItems = g_S.espWiresItems;
    if (!wantPlayers && !wantNpcs && !wantItems) return;

    const int cx = sw / 2;
    const int cy = sh / 2;
    const int ww = static_cast<int>(g_S.espWireWidth + 0.5f);
    const int maxClients = MaxClients();

    for (int snapshotIndex = 0; snapshotIndex < g_EspRenderCandidateCount; ++snapshotIndex) {
        const int i = g_EspRenderCandidates[snapshotIndex].index;

        const bool playerSlot = i <= maxClients;
        if (playerSlot ? !wantPlayers : (!wantNpcs && !wantItems)) continue;

        void* e = g_EspRenderCandidates[snapshotIndex].entity;
        if (!e || e == local || Ent(i) != e || Dormant(e) ||
            isOutsideEspCullBounds(e, i, sw, sh, true)) continue;

        enum { WT_NONE, WT_HOSTILE, WT_FRIENDLY, WT_VEHICLE, WT_ITEM };
        int kind = WT_NONE;
        Vec3 pos{};
        Target t{};

        if (playerSlot) {
            if (wantPlayers && g_EspRenderCandidates[snapshotIndex].classified &&
                classifyEspCached(e, i, t) && t.player) {
                kind = WT_HOSTILE;
                pos = t.origin;
            }
        } else {
            if (wantNpcs && g_EspRenderCandidates[snapshotIndex].classified &&
                classifyEspCached(e, i, t) && !t.player) {

                if (containsNoCase(t.name, "vehicle") || containsNoCase(t.name, "jeep") ||
                    containsNoCase(t.name, "airboat") || containsNoCase(t.name, "buggy")) {
                    kind = WT_VEHICLE;
                } else {
                    kind = isFriendlyNpc(t) ? WT_FRIENDLY : WT_HOSTILE;
                }
                pos = t.origin;
                if (o_ViewOffset >= 0) {
                    __try {
                        const Vec3 off = Field<Vec3>(e, o_ViewOffset);
                        if (std::isfinite(off.z) && off.z > 1.f) pos.z += off.z * 0.5f;
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
            }
            if (kind == WT_NONE && wantItems && g_EspRenderCandidates[snapshotIndex].item) {
                const char* cn = nullptr;
                const char* mp = nullptr;
                if (isWorldItemCached(e, i, cn, mp)) {
                    __try { pos = Field<Vec3>(e, o_Origin); kind = WT_ITEM; }
                    __except (EXCEPTION_EXECUTE_HANDLER) { kind = WT_NONE; }
                }
            }
        }

        if (kind == WT_NONE) continue;
        int sx = 0, sy = 0;
        if (!W2S(pos, sw, sh, sx, sy)) continue;

        EntityRelation rel = ER_Neutral;
        if (kind == WT_ITEM) rel = ER_Item;
        else if (kind == WT_HOSTILE) rel = ER_Hostile;
        else if (kind == WT_FRIENDLY) rel = ER_Friendly;
        else if (kind == WT_VEHICLE) rel = ER_Vehicle;
        int r, g, b;
        relationColor(rel, r, g, b);
        drawThickLine(cx, cy, sx, sy, ww, r, g, b, 200);
    }
}

static bool isWaterBombEntityClass(const char* className) {
    if (!className || !*className) return false;
    return containsNoCase(className, "water_bomb") ||
           containsNoCase(className, "waterbomb") ||
           containsNoCase(className, "weapon_grenade") ||
           containsNoCase(className, "cweapongrenade");
}

static bool isBoostGrenadeEntityClass(const char* className) {
    if (!className || !*className) return false;
    return isWaterBombEntityClass(className) ||
           containsNoCase(className, "grenade") ||
           containsNoCase(className, "smg1_grenade") ||
           containsNoCase(className, "smg_grenade") ||
           containsNoCase(className, "npc_satchel") ||
           containsNoCase(className, "grenade_satchel") ||
           containsNoCase(className, "slam");
}

static bool isSatchelEntityClass(const char* className) {
    if (!className || !*className) return false;
    return containsNoCase(className, "npc_satchel") ||
           containsNoCase(className, "grenade_satchel") ||
           containsNoCase(className, "slam_projectile") ||
           containsNoCase(className, "projectile_slam") ||
           (containsNoCase(className, "slam") && !containsNoCase(className, "tripmine"));
}

static bool isImpactDetonatingSmgGrenadeClass(const char* className) {
    if (!className || !*className) return false;
    return containsNoCase(className, "smg1_grenade") ||
           containsNoCase(className, "smg_grenade") ||
           containsNoCase(className, "grenade_smg1") ||
           containsNoCase(className, "grenade_smg") ||
           containsNoCase(className, "smg1grenade") ||
           containsNoCase(className, "smggrenade");
}

static bool isTrajectoryEntityClass(const char* className) {
    if (!className || !*className) return false;
    if (isWaterBombEntityClass(className)) return true;

    return containsNoCase(className, "projectile") ||
           containsNoCase(className, "grenade") ||
           containsNoCase(className, "smg1_grenade") ||
           containsNoCase(className, "rocket") ||
           containsNoCase(className, "missile") ||
           containsNoCase(className, "crossbow_bolt") ||
           containsNoCase(className, "crossbowbolt") ||
           containsNoCase(className, "flechette") ||
           containsNoCase(className, "combine_ball") ||
           containsNoCase(className, "combineball") ||
           containsNoCase(className, "ar2_orb") ||
           containsNoCase(className, "energy_ball") ||
           containsNoCase(className, "energyball") ||
           containsNoCase(className, "orb") ||
           containsNoCase(className, "hornet") ||
           containsNoCase(className, "arrow") ||
           containsNoCase(className, "bolt") ||
           containsNoCase(className, "flechette") ||
           containsNoCase(className, "flare") ||
           containsNoCase(className, "dart") ||
           containsNoCase(className, "spore") ||
           containsNoCase(className, "spit") ||
           containsNoCase(className, "plasma") ||
           containsNoCase(className, "fireball") ||

           containsNoCase(className, "npc_tripmine") ||
           containsNoCase(className, "grenade_tripmine") ||
           containsNoCase(className, "npc_satchel") ||
           containsNoCase(className, "grenade_satchel") ||
           containsNoCase(className, "slam_projectile") ||
           containsNoCase(className, "projectile_slam");
}

static bool readTrajectoryClassName(void* ent, const char*& className) {
    className = nullptr;
    if (!ent || !IsSaneClientEntity(ent)) return false;
    __try {
        ClientClass* cc = ClassOf(ent);
        if (!cc || !IsReadableProcessRange(cc, sizeof(ClientClass))) return false;
        const char* candidate = cc->m_pNetworkName;
        if (!isReadableCString(candidate)) return false;
        className = candidate;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        className = nullptr;
        return false;
    }
}

static int getVisibleSkillLevel() {
    static ULONGLONG lastRead = 0;
    static int cachedSkill = 2;
    const ULONGLONG now = GetTickCount64();
    if (lastRead != 0 && now >= lastRead && now - lastRead < 1000) return cachedSkill;
    lastRead = now;
    char value[32]{};
    if (getConVarStringDirect("skill", value, sizeof(value))) {
        char* end = nullptr;
        const long parsed = strtol(value, &end, 10);
        if (end != value && end && *end == '\0' && parsed >= 1 && parsed <= 3)
            cachedSkill = static_cast<int>(parsed);
    }
    return cachedSkill;
}

enum class BlastKind { WaterBomb, SmgGrenade, Satchel };

static float readConVarFloatCached(const char* name, float fallback,
                                   ULONGLONG& lastRead, float& cached) {
    const ULONGLONG now = GetTickCount64();
    if (lastRead != 0 && now >= lastRead && now - lastRead < 1000)
        return cached;
    lastRead = now;
    char value[64]{};
    if (getConVarStringDirect(name, value, sizeof(value))) {
        char* end = nullptr;
        const float parsed = strtof(value, &end);
        if (end != value && end && *end == '\0' && std::isfinite(parsed) &&
            parsed > 0.f && parsed <= 10000.f) {
            cached = parsed;
            return cached;
        }
    }
    cached = fallback;
    return cached;
}

static float getPredictedBlastDamage(BlastKind kind) {
    static ULONGLONG lastWater = 0, lastSmg = 0, lastSatchel = 0;
    static float cacheWater = 150.f, cacheSmg = 100.f, cacheSatchel = 150.f;
    if (kind == BlastKind::SmgGrenade) {
        static ULONGLONG lr = 0; static float c = 100.f;
        float v = readConVarFloatCached("sk_plr_dmg_smg1_grenade", 0.f, lr, c);
        if (v > 0.f) return v;
        v = readConVarFloatCached("sk_plr_dmg_grenade", 0.f, lastSmg, cacheSmg);
        if (v > 0.f) return v;
        switch (getVisibleSkillLevel()) {
        case 1: return 75.f; case 3: return 125.f; default: return 100.f;
        }
    }
    if (kind == BlastKind::Satchel) {
        float v = readConVarFloatCached("sk_plr_dmg_satchel", 0.f, lastSatchel, cacheSatchel);
        if (v > 0.f) return v;
        switch (getVisibleSkillLevel()) {
        case 1: return 100.f; case 3: return 200.f; default: return 150.f;
        }
    }

    float v = readConVarFloatCached("sk_plr_dmg_waterbomb", 0.f, lastWater, cacheWater);
    if (v <= 0.f) v = readConVarFloatCached("sk_plr_dmg_water_bomb", 0.f, lastWater, cacheWater);
    if (v <= 0.f) v = readConVarFloatCached("sk_plr_dmg_grenade", 0.f, lastWater, cacheWater);
    if (v > 0.f) return v;
    switch (getVisibleSkillLevel()) {
    case 1: return 100.f; case 3: return 200.f; default: return 150.f;
    }
}

static float getPredictedBlastRadius(BlastKind kind) {
    static ULONGLONG lastSmg = 0, lastSatchel = 0, lastGrenade = 0;
    static float cacheSmg = 250.f, cacheSatchel = 200.f, cacheGrenade = 250.f;
    if (kind == BlastKind::SmgGrenade) {

        float v = readConVarFloatCached("sk_smg1_grenade_radius", 0.f, lastSmg, cacheSmg);
        return v > 0.f ? v : 250.f;
    }
    if (kind == BlastKind::Satchel) {
        float v = readConVarFloatCached("sk_satchel_radius", 0.f, lastSatchel, cacheSatchel);
        return v > 0.f ? v : 200.f;
    }
    float v = readConVarFloatCached("sk_grenade_radius", 0.f, lastGrenade, cacheGrenade);
    if (v <= 0.f) v = readConVarFloatCached("sk_plr_grenade_radius", 0.f, lastGrenade, cacheGrenade);
    return v > 0.f ? v : 250.f;
}

static void blastOutcomeColor(void* local, const Vec3& nadeOrigin, BlastKind kind,
                              int& r, int& g, int& b) {
    r = 70; g = 235; b = 100;  
    if (!local || o_Health < 0 || o_Origin < 0) return;
    int health = 0, armor = 0;
    bool armorKnown = false;
    Vec3 playerOrigin{};
    __try {
        health = Field<int>(local, o_Health);
        playerOrigin = Field<Vec3>(local, o_Origin);
        if (o_Armor >= 0) {
            const int a = Field<int>(local, o_Armor);
            if (a >= 0 && a <= 2000) { armor = a; armorKnown = true; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (health <= 0 || health > 1000) return;
    const float blastRadius = getPredictedBlastRadius(kind);
    const float maxDamage = getPredictedBlastDamage(kind);
    if (blastRadius < 1.f || maxDamage <= 0.f) return;
    const float armorPool = armorKnown ? static_cast<float>(armor) : 0.f;
    Vec3 playerPoint = playerOrigin;
    playerPoint.z += 36.f;
    const float distance = (playerPoint - nadeOrigin).Length();
    float rawDamage = 0.f;
    if (std::isfinite(distance) && distance < blastRadius)
        rawDamage = maxDamage * (1.f - distance / blastRadius);
    const float absorbed = (std::min)(rawDamage * 0.5f, armorPool * 0.5f);
    const float healthDamage = (std::max)(0.f, rawDamage - absorbed);
    if (healthDamage >= static_cast<float>(health)) { r = 255; g = 55; b = 45; }
    else if (healthDamage >= 0.5f) { r = 255; g = 205; b = 45; }
}

static void drawNadeBoostRadius(void* local, const Vec3& nadeOrigin, int sw, int sh,
                                BlastKind kind = BlastKind::WaterBomb) {
    if (!g_S.nadeBoostIndicator || !local || o_Health < 0 || o_Origin < 0) return;

    int health = 0;
    int armor = 0;
    bool armorKnown = false;
    Vec3 playerOrigin{};
    __try {
        const char* healthAddress = reinterpret_cast<const char*>(local) + o_Health;
        const char* originAddress = reinterpret_cast<const char*>(local) + o_Origin;
        if (!IsReadableProcessRange(healthAddress, sizeof(int)) ||
            !IsReadableProcessRange(originAddress, sizeof(Vec3))) return;
        health = Field<int>(local, o_Health);
        playerOrigin = Field<Vec3>(local, o_Origin);
        if (o_Armor >= 0) {
            const char* armorAddress = reinterpret_cast<const char*>(local) + o_Armor;
            if (IsReadableProcessRange(armorAddress, sizeof(int))) {
                const int candidateArmor = Field<int>(local, o_Armor);
                if (candidateArmor >= 0 && candidateArmor <= 2000) {
                    armor = candidateArmor;
                    armorKnown = true;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }

    if (health <= 0 || health > 1000 ||
        !std::isfinite(playerOrigin.x) || !std::isfinite(playerOrigin.y) ||
        !std::isfinite(playerOrigin.z) ||
        !std::isfinite(nadeOrigin.x) || !std::isfinite(nadeOrigin.y) ||
        !std::isfinite(nadeOrigin.z)) return;

    const float blastRadius = getPredictedBlastRadius(kind);
    const float maxDamage = getPredictedBlastDamage(kind);
    if (!std::isfinite(blastRadius) || blastRadius < 8.f ||
        !std::isfinite(maxDamage) || maxDamage <= 0.f) return;

    const float armorPool = armorKnown ? static_cast<float>(armor) : 0.f;
    Vec3 playerPoint = playerOrigin;
    playerPoint.z += 36.f;
    Vec3 delta = playerPoint - nadeOrigin;
    const float distance = delta.Length();
    float rawDamage = 0.f;
    if (std::isfinite(distance) && distance < blastRadius)
        rawDamage = maxDamage * (1.f - distance / blastRadius);
    const float absorbedDamage = (std::min)(rawDamage * 0.5f, armorPool * 0.5f);
    const float healthDamage = (std::max)(0.f, rawDamage - absorbedDamage);

    int r = 70, g = 235, b = 100;
    if (healthDamage >= static_cast<float>(health)) {
        r = 255; g = 55; b = 45;
    } else if (healthDamage >= 0.5f) {
        r = 255; g = 205; b = 45;
    }

    Vec3 center = nadeOrigin;
    center.z += 2.f;
    constexpr int segments = 32;
    static float unitX[segments + 1]{};
    static float unitY[segments + 1]{};
    static bool unitCircleReady = false;
    if (!unitCircleReady) {
        for (int i = 0; i <= segments; ++i) {
            const float angle = static_cast<float>(i) * 6.28318530718f / static_cast<float>(segments);
            unitX[i] = cosf(angle);
            unitY[i] = sinf(angle);
        }
        unitCircleReady = true;
    }
    int previousX = 0, previousY = 0;
    bool previousProjected = false;
    for (int i = 0; i <= segments; ++i) {
        const Vec3 point = center + Vec3(unitX[i] * blastRadius, unitY[i] * blastRadius, 0.f);
        int x = 0, y = 0;
        const bool projected = W2S(point, sw, sh, x, y);
        if (previousProjected && projected)
            drawThickLine(previousX, previousY, x, y, 3, r, g, b, 235);
        previousX = x;
        previousY = y;
        previousProjected = projected;
    }
    int cx = 0, cy = 0;
    if (W2S(center, sw, sh, cx, cy)) {
        drawThickLine(cx - 5, cy, cx + 5, cy, 2, r, g, b, 240);
        drawThickLine(cx, cy - 5, cx, cy + 5, 2, r, g, b, 240);
    }
}

namespace {
    constexpr int kMaxTrajectoryCandidates = 128;
    constexpr int kMaxTrajectoryPathPoints = 41;  
    constexpr ULONGLONG kCandidateRefreshMs = 400;
    constexpr ULONGLONG kPredictionRefreshMs = 100;

    struct ProjectileMotionSample {
        void* entity = nullptr;
        Vec3 origin{};
        ULONGLONG tick = 0;
        bool valid = false;
    };

    struct ProjectileTrajectoryCache {
        bool active = false;
        bool boostGrenade = false;
        bool impactDetonate = false;
        bool satchel = false;
        bool crossbow = false;
        int entityIndex = -1;
        void* entity = nullptr;
        unsigned seenGeneration = 0;
        ULONGLONG calculatedAt = 0;
        int pointCount = 0;
        Vec3 points[kMaxTrajectoryPathPoints]{};
        Vec3 endpoint{};
    };

    static ProjectileTrajectoryCache g_ProjectilePaths[kMaxTrajectoryCandidates]{};
    static ProjectileMotionSample g_ProjectileMotion[16385]{};

    static void refreshTrajectoryCandidates(ULONGLONG now, int localIndex) {
        static ULONGLONG lastScan = 0;
        static unsigned generation = 0;
        static bool building = false;
        static void* scanList = nullptr;
        static int scanCursor = 1;
        static int scanMaxEntities = 0;
        constexpr int kTrajectoryEntityScanBudget = 192;

        const void* currentList = g_EntList;
        if (scanList != currentList) {

            for (int i = 0; i < kMaxTrajectoryCandidates; ++i)
                g_ProjectilePaths[i] = ProjectileTrajectoryCache{};
            for (int i = 0; i < static_cast<int>(_countof(g_ProjectileMotion)); ++i) {
                g_ProjectileMotion[i].entity = nullptr;
                g_ProjectileMotion[i].valid = false;
            }
            scanList = const_cast<void*>(currentList);
            building = false;
            lastScan = 0;
        }

        if (!building) {
            if (lastScan != 0 && now >= lastScan && now - lastScan < kCandidateRefreshMs)
                return;
            building = true;
            ++generation;
            if (generation == 0) ++generation;
            scanCursor = 1;
            scanMaxEntities = MaxEnt();
        }

        int processed = 0;
        while (scanCursor <= scanMaxEntities && processed < kTrajectoryEntityScanBudget) {
            const int i = scanCursor++;
            ++processed;
            if (i == localIndex) continue;
            void* ent = Ent(i);
            if (!ent) continue;

            const char* className = nullptr;
            if (!readTrajectoryClassName(ent, className) || !isTrajectoryEntityClass(className))
                continue;
            const bool boostGrenade = isBoostGrenadeEntityClass(className);
            const bool impactDetonate = isImpactDetonatingSmgGrenadeClass(className);

            int slot = -1;
            for (int j = 0; j < kMaxTrajectoryCandidates; ++j) {
                ProjectileTrajectoryCache& path = g_ProjectilePaths[j];
                if (path.active && path.entityIndex == i && path.entity == ent) {
                    slot = j;
                    break;
                }
            }
            if (slot < 0) {

                for (int j = 0; j < kMaxTrajectoryCandidates; ++j) {
                    if (!g_ProjectilePaths[j].active) {
                        slot = j;
                        break;
                    }
                }
            }
            if (slot < 0) continue;

            ProjectileTrajectoryCache& path = g_ProjectilePaths[slot];
            if (!path.active || path.entity != ent || path.entityIndex != i) {
                path = ProjectileTrajectoryCache{};
                path.active = true;
                path.entityIndex = i;
                path.entity = ent;
            }
            path.boostGrenade = boostGrenade;
            path.impactDetonate = impactDetonate;
            path.satchel = isSatchelEntityClass(className);
            path.crossbow = containsNoCase(className, "crossbow_bolt") ||
                containsNoCase(className, "crossbowbolt");
            path.seenGeneration = generation;
        }

        if (scanCursor > scanMaxEntities) {

            for (int i = 0; i < kMaxTrajectoryCandidates; ++i) {
                ProjectileTrajectoryCache& path = g_ProjectilePaths[i];
                if (path.active && path.seenGeneration != generation)
                    path = ProjectileTrajectoryCache{};
            }
            building = false;
            lastScan = now;
        }
    }

    static bool updateProjectilePath(ProjectileTrajectoryCache& entry, void* ent,
                                    void* local, ULONGLONG now) {
        entry.calculatedAt = now;
        entry.pointCount = 0;
        if (!ent || o_Origin < 0 || !g_Trace) return false;

        Vec3 pos{}, velocity{};
        __try {
            if (!IsReadableProcessRange(reinterpret_cast<const char*>(ent) + o_Origin, sizeof(Vec3)))
                return false;
            pos = Field<Vec3>(ent, o_Origin);
            if (o_Velocity >= 0 &&
                IsReadableProcessRange(reinterpret_cast<const char*>(ent) + o_Velocity, sizeof(Vec3)))
                velocity = Field<Vec3>(ent, o_Velocity);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) ||
            !std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z))
            return false;

        Vec3 measuredVelocity{};
        bool haveMeasuredVelocity = false;
        const int idx = entry.entityIndex;
        if (idx >= 0 && idx < static_cast<int>(sizeof(g_ProjectileMotion) / sizeof(g_ProjectileMotion[0]))) {
            ProjectileMotionSample& sample = g_ProjectileMotion[idx];
            if (sample.valid && sample.entity == ent && now > sample.tick && now - sample.tick <= 300) {
                const float dt = static_cast<float>(now - sample.tick) * 0.001f;
                if (dt >= 0.001f && dt <= 0.30f) {
                    measuredVelocity = (pos - sample.origin) * (1.f / dt);
                    const float measuredSpeed = measuredVelocity.Length();
                    haveMeasuredVelocity = std::isfinite(measuredSpeed) &&
                                           measuredSpeed >= 1.f && measuredSpeed <= 3500.f;
                }
            }
            sample.entity = ent;
            sample.origin = pos;
            sample.tick = now;
            sample.valid = true;
        }

        float initialSpeed = velocity.Length();
        const bool reportUsable = std::isfinite(initialSpeed) &&
                                  initialSpeed >= 1.f && initialSpeed <= 3500.f;
        bool preferMeasured = !reportUsable;
        if (reportUsable && haveMeasuredVelocity) {
            const float measuredSpeed = measuredVelocity.Length();
            const float ratio = initialSpeed > measuredSpeed
                ? initialSpeed / measuredSpeed : measuredSpeed / initialSpeed;
            if (std::isfinite(ratio) && ratio > 3.f)
                preferMeasured = true;
        }
        if (preferMeasured) {
            if (!haveMeasuredVelocity) return false;  
            velocity = measuredVelocity;
            initialSpeed = velocity.Length();
        }
        if (!std::isfinite(initialSpeed) || initialSpeed < 1.f || initialSpeed > 3500.f)
            return false;

        entry.points[entry.pointCount++] = pos;
        constexpr float step = 0.05f;
        float gravity = 600.f;
        {
            char gravText[32]{};
            if (getConVarStringDirect("sv_gravity", gravText, sizeof(gravText))) {
                char* end = nullptr;
                const float parsed = strtof(gravText, &end);
                if (end != gravText && std::isfinite(parsed) && parsed > 1.f && parsed < 5000.f)
                    gravity = parsed;
            }
        }
        if (entry.satchel)
            gravity = gravity * (560.f / 800.f);
        if (entry.crossbow)
            gravity = gravity * 0.05f;
        const float kRestitution = entry.satchel ? 0.85f : 0.2f;
        constexpr int maxSteps = 40;  
        Vec3 current = pos;
        Vec3 v = velocity;
        for (int i = 0; i < maxSteps && entry.pointCount < kMaxTrajectoryPathPoints; ++i) {
            const Vec3 acceleration(0.f, 0.f, -gravity);

            Vec3 nextVelocity = v + acceleration * step;
            Vec3 next = current + nextVelocity * step;

            Trace_t tr{};
            Ray_t ray;
            ray.init(current, next);
            struct ProjectileTraceFilter : TraceFilter {
                void* skipSecond = nullptr;
                bool ShouldHitEntity(void* candidate, int) override {
                    return candidate != skip && candidate != skipSecond;
                }
            } filter;
            filter.skip = ent;
            filter.skipSecond = local;
            VCall<void, const Ray_t&, unsigned, void*, Trace_t*>(g_Trace, 4, ray, MASK_SHOT, &filter, &tr);

            bool hit = false;
            Vec3 normal{};
            if (std::isfinite(tr.fraction) && tr.fraction >= 0.f && tr.fraction < 0.999f &&
                std::isfinite(tr.endpos.x) && std::isfinite(tr.endpos.y) && std::isfinite(tr.endpos.z)) {
                next = tr.endpos;
                hit = true;
                normal = tr.plane.normal;
            }
            entry.points[entry.pointCount++] = next;

            if (hit && entry.impactDetonate) break;

            if (hit) {
                const float normalLength = normal.Length();
                if (std::isfinite(normalLength) && normalLength > 0.5f && normalLength < 1.5f) {
                    normal = normal * (1.f / normalLength);
                    if (entry.crossbow) {
                        Vec3 dir = v;
                        float speed = dir.Length();
                        if (speed > 1.f) dir = dir * (1.f / speed);
                        const float hitDot = normal.x * (-dir.x) + normal.y * (-dir.y) +
                                             normal.z * (-dir.z);
                        if (hitDot < 0.5f && speed > 100.f) {
                            Vec3 reflect(
                                2.f * normal.x * hitDot + dir.x,
                                2.f * normal.y * hitDot + dir.y,
                                2.f * normal.z * hitDot + dir.z);
                            nextVelocity = reflect * (speed * 0.75f);
                            gravity = 600.f;
                            {
                                char gravText[32]{};
                                if (getConVarStringDirect("sv_gravity", gravText, sizeof(gravText))) {
                                    char* end = nullptr;
                                    const float parsed = strtof(gravText, &end);
                                    if (end != gravText && std::isfinite(parsed) && parsed > 1.f && parsed < 5000.f)
                                        gravity = parsed;
                                }
                            }
                            current = next + normal * 1.f;
                            v = nextVelocity;
                            continue;
                        }
                        break;
                    }
                    const float normalSpeed = nextVelocity.x * normal.x +
                                              nextVelocity.y * normal.y +
                                              nextVelocity.z * normal.z;
                    if (normalSpeed < 0.f) {
                        nextVelocity = nextVelocity - normal * (2.f * normalSpeed);
                        nextVelocity = nextVelocity * kRestitution;
                        current = next + normal * 1.f;
                        v = nextVelocity;
                        if (v.Length() < 40.f) break;
                        continue;
                    }
                }
                break;
            }
            current = next;
            v = nextVelocity;
        }

        if (entry.pointCount < 2) return false;
        entry.endpoint = entry.points[entry.pointCount - 1];
        return true;
    }

    static void drawCachedProjectilePath(const ProjectileTrajectoryCache& entry,
                                         const Vec3& liveOffset, int sw, int sh,
                                         void* local) {
        if (!entry.active || entry.pointCount < 2) return;
        int originX = 0, originY = 0;
        if (!W2S(entry.points[0] + liveOffset, sw, sh, originX, originY) ||
            originX < -sw || originX > sw * 2 || originY < -sh || originY > sh * 2)
            return;
        int previousX = 0, previousY = 0;
        bool previousProjected = W2S(entry.points[0] + liveOffset, sw, sh, previousX, previousY);
        int endX = previousX, endY = previousY;
        bool endpointProjected = previousProjected;

        for (int i = 1; i < entry.pointCount; ++i) {
            int x = 0, y = 0;
            const bool projected = W2S(entry.points[i] + liveOffset, sw, sh, x, y);
            if (previousProjected && projected)
                drawThickLine(previousX, previousY, x, y, 3, 80, 205, 255, 235);
            previousX = x;
            previousY = y;
            previousProjected = projected;
            if (i == entry.pointCount - 1) {
                endX = x;
                endY = y;
                endpointProjected = projected;
            }
        }

        if (endpointProjected) {
            int lr = 255, lg = 225, lb = 95;
            if (entry.boostGrenade && local) {
                BlastKind bk = BlastKind::WaterBomb;
                if (entry.impactDetonate) bk = BlastKind::SmgGrenade;
                else if (entry.satchel) bk = BlastKind::Satchel;
                const Vec3 land = entry.points[entry.pointCount - 1] + liveOffset;
                blastOutcomeColor(local, land, bk, lr, lg, lb);
            }
            drawThickLine(endX - 7, endY, endX + 7, endY, 3, lr, lg, lb, 245);
            drawThickLine(endX, endY - 7, endX, endY + 7, 3, lr, lg, lb, 245);
        }
    }
}

void drawTrajectories(int sw, int sh) {
    beginEspCullPass();
    if (sw < 2 || sh < 2 || o_Origin < 0 || !g_EntList || !g_Surface)
        return;
    if (!g_S.waterBombTrajectories && !g_S.nadeBoostIndicator) return;

    const int localIndex = LocalIndex();
    void* local = localIndex > 0 ? Ent(localIndex) : nullptr;
    if (!local) return;

    if (o_LifeState >= 0) {
        __try {
            if (Field<uint8_t>(local, o_LifeState) != 0) {
                for (int i = 0; i < kMaxTrajectoryCandidates; ++i)
                    g_ProjectilePaths[i] = ProjectileTrajectoryCache{};
                return;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    const ULONGLONG now = GetTickCount64();
    refreshTrajectoryCandidates(now, localIndex);
    static int updateCursor = 0;
    int updatesRemaining = static_cast<int>(g_S.trajectoryBudget + 0.5f);
        if (updatesRemaining < 1) updatesRemaining = 1;
        if (updatesRemaining > 4) updatesRemaining = 4;

    for (int offset = 0; offset < kMaxTrajectoryCandidates; ++offset) {
        const int i = (updateCursor + offset) % kMaxTrajectoryCandidates;
        ProjectileTrajectoryCache& entry = g_ProjectilePaths[i];
        if (!entry.active) continue;

        void* currentEntity = Ent(entry.entityIndex);
        if (!currentEntity || currentEntity != entry.entity) {
            entry = ProjectileTrajectoryCache{};
            continue;
        }

        Vec3 liveOrigin{};
        bool haveLiveOrigin = false;
        __try {
            const char* originAddress = reinterpret_cast<const char*>(currentEntity) + o_Origin;
            if (IsReadableProcessRange(originAddress, sizeof(Vec3))) {
                liveOrigin = Field<Vec3>(currentEntity, o_Origin);
                haveLiveOrigin = std::isfinite(liveOrigin.x) && std::isfinite(liveOrigin.y) &&
                                 std::isfinite(liveOrigin.z);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { haveLiveOrigin = false; }
        if (!haveLiveOrigin || isOutsideEspCullBounds(currentEntity, entry.entityIndex, sw, sh)) continue;

        if (entry.boostGrenade && g_S.nadeBoostIndicator)
            {
                BlastKind bk = BlastKind::WaterBomb;
                if (entry.impactDetonate) bk = BlastKind::SmgGrenade;
                else if (entry.satchel) bk = BlastKind::Satchel;
                drawNadeBoostRadius(local, liveOrigin, sw, sh, bk);
            }

        if (!g_S.waterBombTrajectories) continue;

        const bool pathExpired = entry.calculatedAt == 0 || now < entry.calculatedAt ||
            now - entry.calculatedAt >= kPredictionRefreshMs;
        if (pathExpired && updatesRemaining > 0 && g_Trace) {
            updateProjectilePath(entry, currentEntity, local, now);
            --updatesRemaining;
        }
        if (entry.pointCount < 2) continue;

        const Vec3 liveOffset = liveOrigin - entry.points[0];
        drawCachedProjectilePath(entry, liveOffset, sw, sh, local);
    }

    updateCursor = (updateCursor + (static_cast<int>(g_S.trajectoryBudget + 0.5f) < 1 ? 1 : (static_cast<int>(g_S.trajectoryBudget + 0.5f) > 4 ? 4 : static_cast<int>(g_S.trajectoryBudget + 0.5f)))) % kMaxTrajectoryCandidates;
}

namespace {
    constexpr int kPreLaunchPredictionPoints = 41;  
    constexpr ULONGLONG kPreLaunchRefreshMs = 33;   

    struct PreLaunchPredictionCache {
        bool valid = false;
        bool smgLauncher = false;
        bool combineBall = false;
        bool slam = false;
        bool crossbow = false;
        int weaponIndex = -1;
        void* weaponEntity = nullptr;
        ULONGLONG calculatedAt = 0;
        Vec3 origin{};
        QAngle angles{};
        int pointCount = 0;
        Vec3 points[kPreLaunchPredictionPoints]{};
    };

    static PreLaunchPredictionCache g_PreLaunchPrediction;
    static QAngle g_PredictionViewAngles{};
    static ULONGLONG g_PredictionViewAnglesAt = 0;

    enum class PreLaunchKind { None, HandThrown, SmgLauncher, CombineBall, Slam, Crossbow };

    static PreLaunchKind classifyPreLaunchWeapon(const char* className) {
        if (!className || !*className) return PreLaunchKind::None;
        if (containsNoCase(className, "weapon_smg1") ||
            containsNoCase(className, "weaponsmg1") ||
            containsNoCase(className, "weapon_smg"))
            return PreLaunchKind::SmgLauncher;

        if (containsNoCase(className, "weapon_ar2") ||
            containsNoCase(className, "weapon_irifle") ||
            containsNoCase(className, "weapon_pulse") ||
            containsNoCase(className, "cweaponar2"))
            return PreLaunchKind::CombineBall;
        if (containsNoCase(className, "weapon_slam") ||
            containsNoCase(className, "cweaponslam") ||
            containsNoCase(className, "weapon_satchel") ||
            containsNoCase(className, "weapon_slamsatchel") ||
            containsNoCase(className, "slam_projectile"))
            return PreLaunchKind::Slam;
        if (containsNoCase(className, "weapon_crossbow") ||
            containsNoCase(className, "cweaponcrossbow") ||
            containsNoCase(className, "weapon_xbow"))
            return PreLaunchKind::Crossbow;
        if (containsNoCase(className, "weapon_frag") ||
            containsNoCase(className, "cweaponfrag") ||
            containsNoCase(className, "weapon_grenade") ||
            containsNoCase(className, "cweapongrenade") ||
            containsNoCase(className, "water_bomb") ||
            containsNoCase(className, "waterbomb"))
            return PreLaunchKind::HandThrown;
        return PreLaunchKind::None;
    }

    static bool getActivePreLaunchWeapon(void* local, int& weaponIndex, void*& weapon,
                                         PreLaunchKind& kind) {
        weaponIndex = -1;
        weapon = nullptr;
        kind = PreLaunchKind::None;
        if (!local || o_ActiveWeapon < 0 ||
            !IsReadableProcessRange(reinterpret_cast<const char*>(local) + o_ActiveWeapon, sizeof(int)))
            return false;

        int handle = -1;
        __try { handle = Field<int>(local, o_ActiveWeapon); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        if (handle == -1 || handle == 0) return false;

        const int masks[] = { 0xFFF, 0x7FF };
        const int maxEntities = MaxEnt();
        for (int mask : masks) {
            const int index = handle & mask;
            if (index <= 0 || index > maxEntities) continue;
            void* candidate = Ent(index);
            const char* className = nullptr;
            if (!candidate || !readTrajectoryClassName(candidate, className)) continue;
            const PreLaunchKind candidateKind = classifyPreLaunchWeapon(className);
            if (candidateKind == PreLaunchKind::None) continue;
            weaponIndex = index;
            weapon = candidate;
            kind = candidateKind;
            return true;
        }
        return false;
    }

    static bool getLocalEyePosition(void* local, Vec3& eye) {
        if (!local || o_Origin < 0 ||
            !IsReadableProcessRange(reinterpret_cast<const char*>(local) + o_Origin, sizeof(Vec3)))
            return false;
        __try {
            eye = Field<Vec3>(local, o_Origin);
            if (o_ViewOffset >= 0 && IsReadableProcessRange(
                    reinterpret_cast<const char*>(local) + o_ViewOffset, sizeof(Vec3))) {
                const Vec3 viewOffset = Field<Vec3>(local, o_ViewOffset);
                if (std::isfinite(viewOffset.x) && std::isfinite(viewOffset.y) &&
                    std::isfinite(viewOffset.z) && viewOffset.z > 8.f && viewOffset.z < 120.f)
                    eye = eye + viewOffset;
                else
                    eye.z += 64.f;
            } else {
                eye.z += 64.f;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return std::isfinite(eye.x) && std::isfinite(eye.y) && std::isfinite(eye.z);
    }

    static Vec3 forwardFromAngles(const QAngle& angles) {
        const float pitch = angles.x * 0.017453292519943295f;
        const float yaw = angles.y * 0.017453292519943295f;
        const float cp = cosf(pitch);
        return Vec3(cp * cosf(yaw), cp * sinf(yaw), -sinf(pitch));
    }

    static bool simulatePreLaunchPath(PreLaunchPredictionCache& cache, void* local,
                                      const Vec3& eye, const QAngle& angles,
                                      PreLaunchKind kind, ULONGLONG now) {
        cache.pointCount = 0;
        cache.calculatedAt = now;
        cache.origin = eye;
        cache.angles = angles;
        cache.smgLauncher = kind == PreLaunchKind::SmgLauncher;
        cache.combineBall = kind == PreLaunchKind::CombineBall;
        cache.slam = kind == PreLaunchKind::Slam;
        cache.crossbow = kind == PreLaunchKind::Crossbow;
        if (!local || !g_Trace || !std::isfinite(angles.x) || !std::isfinite(angles.y))
            return false;

        const Vec3 forward = forwardFromAngles(angles);
        const float forwardLength = forward.Length();
        if (!std::isfinite(forwardLength) || forwardLength < 0.9f || forwardLength > 1.1f)
            return false;

        Vec3 right{};
        {

            const float sy = sinf(angles.y * 0.01745329252f);
            const float cy = cosf(angles.y * 0.01745329252f);
            right = Vec3(sy, -cy, 0.f);  

            const Vec3 worldUp(0.f, 0.f, 1.f);
            right = Vec3(forward.y * worldUp.z - forward.z * worldUp.y,
                         forward.z * worldUp.x - forward.x * worldUp.z,
                         forward.x * worldUp.y - forward.y * worldUp.x);
            const float rl = right.Length();
            if (std::isfinite(rl) && rl > 0.001f) right = right * (1.f / rl);
            else right = Vec3(sy, -cy, 0.f);
        }

        Vec3 start;
        Vec3 velocity;

        const int skill = getVisibleSkillLevel();
        float skillScale = 1.f;
        if (skill == 1) skillScale = 0.92f;
        else if (skill == 3) skillScale = 1.08f;

        if (cache.smgLauncher || cache.combineBall) {

            start = eye + forward * 12.f;
            velocity = forward * (1000.f * skillScale);
        } else if (cache.slam) {

            Vec3 center = eye;
            if (o_ViewOffset >= 0 && IsReadableProcessRange(
                    reinterpret_cast<const char*>(local) + o_ViewOffset, sizeof(Vec3))) {
                __try {
                    const Vec3 vo = Field<Vec3>(local, o_ViewOffset);
                    if (std::isfinite(vo.z) && vo.z > 8.f)
                        center.z -= vo.z * 0.5f;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            start = center + forward * 18.f;
            start.z += 24.f;
            velocity = forward * (500.f * skillScale);
            if (o_Velocity >= 0 && IsReadableProcessRange(
                    reinterpret_cast<const char*>(local) + o_Velocity, sizeof(Vec3))) {
                __try {
                    const Vec3 playerVelocity = Field<Vec3>(local, o_Velocity);
                    if (std::isfinite(playerVelocity.x) && std::isfinite(playerVelocity.y) &&
                        std::isfinite(playerVelocity.z) && playerVelocity.Length() < 1500.f)
                        velocity = velocity + playerVelocity;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        } else if (cache.crossbow) {

            start = eye + forward * 16.f;
            velocity = forward * (3500.f * skillScale);
        } else {

            Vec3 throwDir = forward;
            throwDir.z += 0.1f;
            const float td = throwDir.Length();
            if (std::isfinite(td) && td > 0.001f) throwDir = throwDir * (1.f / td);
            start = eye + throwDir * 18.f + right * 8.f;
            velocity = throwDir * (1200.f * skillScale);
            if (o_Velocity >= 0 && IsReadableProcessRange(
                    reinterpret_cast<const char*>(local) + o_Velocity, sizeof(Vec3))) {
                __try {
                    const Vec3 playerVelocity = Field<Vec3>(local, o_Velocity);
                    if (std::isfinite(playerVelocity.x) && std::isfinite(playerVelocity.y) &&
                        std::isfinite(playerVelocity.z) && playerVelocity.Length() < 1500.f)
                        velocity = velocity + playerVelocity;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }

        cache.points[cache.pointCount++] = start;
        constexpr float step = 0.05f;
        float gravity = cache.combineBall ? 0.f : 600.f;
        if (!cache.combineBall) {
            char gravText[32]{};
            if (getConVarStringDirect("sv_gravity", gravText, sizeof(gravText))) {
                char* end = nullptr;
                const float parsed = strtof(gravText, &end);
                if (end != gravText && std::isfinite(parsed) && parsed > 1.f && parsed < 5000.f)
                    gravity = parsed;
            }
            if (cache.slam)
                gravity = gravity * (560.f / 800.f);

            if (cache.crossbow)
                gravity = gravity * 0.05f;
        }
        const float kRestitution = cache.slam ? 0.85f : 0.2f;
        constexpr int maxSteps = 40;
        Vec3 current = start;
        Vec3 v = velocity;
        struct PreviewTraceFilter : TraceFilter {
            bool ShouldHitEntity(void* candidate, int) override { return candidate != skip; }
        } filter;
        filter.skip = local;

        for (int i = 0; i < maxSteps && cache.pointCount < kPreLaunchPredictionPoints; ++i) {
            const Vec3 acceleration(0.f, 0.f, -gravity);
            Vec3 nextVelocity = v + acceleration * step;
            Vec3 next = current + nextVelocity * step;
            Trace_t trace{};
            Ray_t ray;
            ray.init(current, next);
            __try {
                VCall<void, const Ray_t&, unsigned, TraceFilter*, Trace_t*>(
                    g_Trace, 4, ray, MASK_SHOT, &filter, &trace);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }

            bool hit = false;
            Vec3 normal{};
            if (std::isfinite(trace.fraction) && trace.fraction >= 0.f && trace.fraction < 0.999f &&
                std::isfinite(trace.endpos.x) && std::isfinite(trace.endpos.y) &&
                std::isfinite(trace.endpos.z)) {
                next = trace.endpos;
                normal = trace.plane.normal;
                hit = true;
            }
            cache.points[cache.pointCount++] = next;

            if (hit && cache.smgLauncher) break;
            if (hit) {
                const float nl = normal.Length();
                if (!std::isfinite(nl) || nl < 0.5f || nl > 1.5f) break;
                normal = normal * (1.f / nl);
                if (cache.crossbow) {

                    Vec3 dir = v;
                    float speed = dir.Length();
                    if (speed > 1.f) dir = dir * (1.f / speed);
                    const float hitDot = normal.x * (-dir.x) + normal.y * (-dir.y) + normal.z * (-dir.z);
                    if (hitDot < 0.5f && speed > 100.f) {

                        Vec3 reflect(
                            2.f * normal.x * hitDot + dir.x,
                            2.f * normal.y * hitDot + dir.y,
                            2.f * normal.z * hitDot + dir.z);
                        nextVelocity = reflect * (speed * 0.75f);

                        gravity = 600.f;
                        {
                            char gravText[32]{};
                            if (getConVarStringDirect("sv_gravity", gravText, sizeof(gravText))) {
                                char* end = nullptr;
                                const float parsed = strtof(gravText, &end);
                                if (end != gravText && std::isfinite(parsed) && parsed > 1.f && parsed < 5000.f)
                                    gravity = parsed;
                            }
                        }
                        current = next + normal * 1.f;
                        v = nextVelocity;
                        continue;
                    }
                    break;  
                }
                const float normalSpeed = nextVelocity.x * normal.x +
                                          nextVelocity.y * normal.y +
                                          nextVelocity.z * normal.z;
                if (normalSpeed >= 0.f) break;
                nextVelocity = nextVelocity - normal * (2.f * normalSpeed);
                if (cache.combineBall)
                    nextVelocity = nextVelocity * 0.9f;
                else
                    nextVelocity = nextVelocity * kRestitution;
                current = next + normal * 1.f;
                v = nextVelocity;
                if (v.Length() < 40.f) break;
                continue;
            }
            current = next;
            v = nextVelocity;
        }
        return cache.pointCount >= 2;
    }

    static void drawPreLaunchCache(const PreLaunchPredictionCache& cache,
                                   const Vec3& liveEye, int sw, int sh, void* local) {
        if (!cache.valid || cache.pointCount < 2) return;
        const Vec3 offset = liveEye - cache.origin;
        int pr = 220, pg = 180, pb = 255;
        if (cache.combineBall) { pr = 130; pg = 180; pb = 255; }
        else if (cache.smgLauncher) { pr = 100; pg = 225; pb = 255; }
        else if (cache.slam) { pr = 255; pg = 160; pb = 70; }
        else if (cache.crossbow) { pr = 180; pg = 255; pb = 120; }
        int px = 0, py = 0;
        bool pp = W2S(cache.points[0] + offset, sw, sh, px, py);
        int endX = px, endY = py;
        bool endProjected = pp;
        for (int i = 1; i < cache.pointCount; ++i) {
            int x = 0, y = 0;
            const bool projected = W2S(cache.points[i] + offset, sw, sh, x, y);
            if (pp && projected)
                drawThickLine(px, py, x, y, 3, pr, pg, pb, 235);
            px = x; py = y; pp = projected;
            if (projected) { endX = x; endY = y; endProjected = true; }
        }
        if (endProjected) {
            int lr = 255, lg = 225, lb = 95;
            if (local && !cache.combineBall) {
                BlastKind bk = BlastKind::WaterBomb;
                if (cache.smgLauncher) bk = BlastKind::SmgGrenade;
                else if (cache.slam) bk = BlastKind::Satchel;
                const Vec3 land = cache.points[cache.pointCount - 1] + (liveEye - cache.origin);
                blastOutcomeColor(local, land, bk, lr, lg, lb);
            }
            drawThickLine(endX - 7, endY, endX + 7, endY, 3, lr, lg, lb, 245);
            drawThickLine(endX, endY - 7, endX, endY + 7, 3, lr, lg, lb, 245);
        }
    }
}

void updatePredictionAngles(const QAngle& angles) {
    if (!std::isfinite(angles.x) || !std::isfinite(angles.y) || !std::isfinite(angles.z)) return;
    g_PredictionViewAngles = angles;
    g_PredictionViewAnglesAt = GetTickCount64();
}

void drawPrediction(int sw, int sh) {
    const bool showPrediction = g_S.prediction;
    const bool showBoostRadius = g_S.nadeBoostIndicator;
    if (!showPrediction && !showBoostRadius) return;
    if (sw < 2 || sh < 2 || !g_Engine || !g_EntList || !g_Trace || !g_Surface ||
        o_Origin < 0 || o_ActiveWeapon < 0) return;
    {
        void* localEarly = Ent(LocalIndex());
        if (localEarly && o_LifeState >= 0) {
            __try {
                if (Field<uint8_t>(localEarly, o_LifeState) != 0) {
                    g_PreLaunchPrediction = PreLaunchPredictionCache{};
                    return;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    const ULONGLONG now = GetTickCount64();
    if (g_PredictionViewAnglesAt == 0 || now < g_PredictionViewAnglesAt ||
        now - g_PredictionViewAnglesAt > 250) return;

    const int localIndex = LocalIndex();
    void* local = localIndex > 0 ? Ent(localIndex) : nullptr;
    if (!local) { g_PreLaunchPrediction = PreLaunchPredictionCache{}; return; }

    int weaponIndex = -1;
    void* weapon = nullptr;
    PreLaunchKind kind = PreLaunchKind::None;
    if (!getActivePreLaunchWeapon(local, weaponIndex, weapon, kind)) {
        g_PreLaunchPrediction = PreLaunchPredictionCache{};
        return;
    }

    Vec3 eye{};
    if (!getLocalEyePosition(local, eye)) { g_PreLaunchPrediction = PreLaunchPredictionCache{}; return; }
    const QAngle angles = g_PredictionViewAngles;
    const float pitchDelta = fabsf(angles.x - g_PreLaunchPrediction.angles.x);
    float yawDelta = fabsf(angles.y - g_PreLaunchPrediction.angles.y);
    if (yawDelta > 180.f) yawDelta = 360.f - yawDelta;
    const Vec3 eyeDelta = eye - g_PreLaunchPrediction.origin;
    const float eyeMovement = eyeDelta.Length();
    const bool identityChanged = g_PreLaunchPrediction.weaponIndex != weaponIndex ||
        g_PreLaunchPrediction.weaponEntity != weapon ||
        g_PreLaunchPrediction.smgLauncher != (kind == PreLaunchKind::SmgLauncher) ||
        g_PreLaunchPrediction.combineBall != (kind == PreLaunchKind::CombineBall) ||
        g_PreLaunchPrediction.slam != (kind == PreLaunchKind::Slam) ||
        g_PreLaunchPrediction.crossbow != (kind == PreLaunchKind::Crossbow);
    const bool timeReversed = now < g_PreLaunchPrediction.calculatedAt;
    const ULONGLONG predictionAge = timeReversed ? 0 : now - g_PreLaunchPrediction.calculatedAt;
    const bool refresh = identityChanged || timeReversed ||
        (g_PreLaunchPrediction.valid
            ? (predictionAge >= kPreLaunchRefreshMs || pitchDelta >= 4.f ||
               yawDelta >= 4.f || eyeMovement >= 16.f)
            : (g_PreLaunchPrediction.calculatedAt == 0 || predictionAge >= 250));
    if (refresh) {
        g_PreLaunchPrediction.weaponIndex = weaponIndex;
        g_PreLaunchPrediction.weaponEntity = weapon;
        g_PreLaunchPrediction.valid = simulatePreLaunchPath(
            g_PreLaunchPrediction, local, eye, angles, kind, now);
    }

    if (showBoostRadius && kind != PreLaunchKind::CombineBall &&
        g_PreLaunchPrediction.valid && g_PreLaunchPrediction.pointCount >= 2) {
        const Vec3 predictedLanding = g_PreLaunchPrediction.points[
            g_PreLaunchPrediction.pointCount - 1] +
            (eye - g_PreLaunchPrediction.origin);
        {
            BlastKind bk = BlastKind::WaterBomb;
            if (kind == PreLaunchKind::SmgLauncher) bk = BlastKind::SmgGrenade;
            else if (kind == PreLaunchKind::Slam) bk = BlastKind::Satchel;
            drawNadeBoostRadius(local, predictedLanding, sw, sh, bk);
        }
    }
    if (showPrediction)
        drawPreLaunchCache(g_PreLaunchPrediction, eye, sw, sh, local);
}

void drawEsp() {

    if (!g_S.esp) {
        g_EspLastRefreshAt = 0;
        g_EspScanInProgress = false;
        g_EspRenderCandidateBuilding = false;
        g_EspRenderCandidateBuilt = false;
        g_EspRenderCandidateCount = 0;
        return;
    }

    if (!g_S.espEntities && !g_S.espItems && !g_S.espWires) {
        g_EspLastRefreshAt = 0;
        g_EspScanInProgress = false;
        g_EspRenderCandidateBuilding = false;
        g_EspRenderCandidateBuilt = false;
        g_EspRenderCandidateCount = 0;
        return;
    }
    beginEspCullPass();
    beginEspDataRefreshPass();
    void* local = Ent(LocalIndex());
    if (!local) return;
    int sw = 0, sh = 0;
    VCall<void, int*, int*>(g_Engine, 5, &sw, &sh);
    if (sw < 2 || sh < 2) return;
    refreshEspRenderCandidates(local, sw, sh);

    if (g_S.espWires) {
        __try { drawEspWires(local, sw, sh); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    if (g_S.espEntities) {
        for (int n = 0; n < g_EspRenderCandidateCount; ++n) {
            if (!g_EspRenderCandidates[n].classified) continue;
            const int i = g_EspRenderCandidates[n].index;
            void* ent = g_EspRenderCandidates[n].entity;
            if (!ent || ent == local || Ent(i) != ent ||
                isOutsideEspCullBounds(ent, i, sw, sh, true)) continue;
            drawClassifiedEntitySafe(ent, i, sw, sh);
        }
    }

    if (g_S.espItems) {
        for (int n = 0; n < g_EspRenderCandidateCount; ++n) {
            if (!g_EspRenderCandidates[n].item) continue;
            const int i = g_EspRenderCandidates[n].index;
            void* ent = g_EspRenderCandidates[n].entity;
            if (!ent || ent == local || Ent(i) != ent) continue;
            drawItemEsp(ent, i, sw, sh);
        }
    }
}
