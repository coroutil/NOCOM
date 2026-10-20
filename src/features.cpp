#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "features.h"
#include "game.h"
#include "settings.h"
#include "render.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

void fixMove(CUserCmd* cmd, float oldYaw) {
    float d = (cmd->viewangles.y - oldYaw) * 0.0174533f;
    float f = cmd->forwardmove, sd = cmd->sidemove, c = cosf(d), sn = sinf(d);
    cmd->forwardmove = f * c - sd * sn;
    cmd->sidemove = f * sn + sd * c;
}

volatile LONG g_EdgebugDetected = 0;
volatile LONG g_EdgebugTicksToLanding = -1;

static float safeTickInterval(float ft) {
    if (!std::isfinite(ft) || ft < 0.001f || ft > 0.05f) return 0.015f;
    return ft;
}

static void doJumpbug(void* local, CUserCmd* cmd, float ft) {

    ft = safeTickInterval(ft);
    if (!g_S.jumpbug || o_Flags < 0 || o_Velocity < 0 || !g_Trace || g_MenuOpen ||
        (g_S.jbKey > 0 && !(GetAsyncKeyState(g_S.jbKey) & 0x8000)))
        return;

    const int flags = Field<int>(local, o_Flags);
    if ((flags & 1) != 0) return;

    Vec3 velocity{};
    __try { velocity = Field<Vec3>(local, o_Velocity); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (!std::isfinite(velocity.z) || velocity.z >= 0.f) return;

    const bool holdingDuck = (cmd->buttons & IN_DUCK) != 0 ||
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (!holdingDuck) return;

    const float fallStep = (-velocity.z) * ft;

    const float unduckExpand = 18.f;
    const float windowSlack = 2.5f;  
    const float traceDistance = unduckExpand + windowSlack + fallStep + 6.f;
    const float distance = traceDownHull(local, traceDistance);
    if (!std::isfinite(distance) || distance >= traceDistance) return;

    const float predicted = distance - fallStep;
    if (predicted < unduckExpand - 1.f || predicted > unduckExpand + windowSlack)
        return;

    cmd->buttons &= ~IN_DUCK;
    cmd->buttons |= IN_JUMP;
}

static float traceDownFromPlayerCenter(void* local, float len) {
    if (!local || o_Origin < 0 || o_Mins < 0 || o_Maxs < 0 || !g_Trace) return len;
    const Vec3 origin = Field<Vec3>(local, o_Origin);
    const Vec3 mins = Field<Vec3>(local, o_Mins);
    const Vec3 maxs = Field<Vec3>(local, o_Maxs);
    const Vec3 center((mins.x + maxs.x) * 0.5f, (mins.y + maxs.y) * 0.5f,
                      (mins.z + maxs.z) * 0.5f);

    return traceDownFrom(local, origin + center, len);
}

static float horizontalSpeed(const Vec3& velocity) {
    const float v = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
    return std::isfinite(v) ? v : 0.f;
}

static float playerHullHalfHeight(void* local) {
    if (!local || o_Mins < 0 || o_Maxs < 0) return 36.f;
    __try {
        const Vec3 mins = Field<Vec3>(local, o_Mins);
        const Vec3 maxs = Field<Vec3>(local, o_Maxs);
        const float halfHeight = (maxs.z - mins.z) * 0.5f;
        if (std::isfinite(halfHeight) && halfHeight >= 8.f && halfHeight <= 128.f)
            return halfHeight;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 36.f;
}

static bool isEdgeFootprint(void* local, float hullDistance, float pointDistance) {
    if (!std::isfinite(hullDistance) || !std::isfinite(pointDistance)) return false;
    if (pointDistance >= 511.f) return true;  

    return pointDistance - hullDistance > playerHullHalfHeight(local) + 2.0f;
}

static void doEdgejump(void* local, CUserCmd* cmd, float ft, bool grounded) {
    if (!g_S.edgejump || !grounded || o_Velocity < 0 || o_Origin < 0 || !g_Trace) return;
    Vec3 vel{};
    __try { vel = Field<Vec3>(local, o_Velocity); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    if (horizontalSpeed(vel) < g_S.edgejumpMinSpeed) return;  
    ft = safeTickInterval(ft);
    const Vec3 origin = Field<Vec3>(local, o_Origin);
    const float lookahead = g_S.edgejumpLookaheadTicks < 1.f ? 1.f : g_S.edgejumpLookaheadTicks;
    const Vec3 here(origin.x, origin.y, origin.z + 8.f);
    const Vec3 next(origin.x + vel.x * ft * lookahead,
                    origin.y + vel.y * ft * lookahead, origin.z + 8.f);
    const float currentFloor = traceDownFrom(local, here, 24.f);
    const float nextFloor = traceDownFrom(local, next, 24.f);

    if (currentFloor < 13.f && nextFloor > 20.f) cmd->buttons |= IN_JUMP;
}

static void doEdgebug(void* local, CUserCmd* cmd, float ft, bool grounded) {

    if (!g_S.edgebug || grounded || o_Velocity < 0 || !g_Trace) {
        InterlockedExchange(&g_EdgebugDetected, 0);
        InterlockedExchange(&g_EdgebugTicksToLanding, -1);
        return;
    }
    Vec3 vel{};
    __try { vel = Field<Vec3>(local, o_Velocity); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_EdgebugDetected, 0);
        InterlockedExchange(&g_EdgebugTicksToLanding, -1);
        return;
    }

    if (!std::isfinite(vel.z) || vel.z >= -1.f || horizontalSpeed(vel) < 30.f) {
        InterlockedExchange(&g_EdgebugDetected, 0);
        InterlockedExchange(&g_EdgebugTicksToLanding, -1);
        return;
    }
    ft = safeTickInterval(ft);
    const float hullDistance = traceDownHull(local, 512.f);
    const float pointDistance = traceDownFromPlayerCenter(local, 512.f);
    if (!std::isfinite(hullDistance) || !std::isfinite(pointDistance) ||
        hullDistance >= 512.f || hullDistance <= 0.f) {
        InterlockedExchange(&g_EdgebugDetected, 0);
        InterlockedExchange(&g_EdgebugTicksToLanding, -1);
        return;
    }
    const float perTick = fmaxf(1.f, -vel.z * ft);
    const int ticksToLanding = (int)ceilf(hullDistance / perTick);
    const int window = (int)(g_S.edgebugTicks + 0.5f);

    const bool edgeLike = isEdgeFootprint(local, hullDistance, pointDistance);

    const bool insideWindow = window == 0 ? ticksToLanding <= 1 : ticksToLanding <= window;
    const bool candidate = edgeLike && insideWindow;
    InterlockedExchange(&g_EdgebugDetected, candidate ? 1 : 0);
    InterlockedExchange(&g_EdgebugTicksToLanding, candidate ? ticksToLanding : -1);
    if (candidate && ticksToLanding <= 1) {

        cmd->buttons &= ~IN_JUMP;

        cmd->buttons |= IN_DUCK;
    }
}

static void applyAutoStrafe(void* local, CUserCmd* cmd) {
    if (!local || !cmd || !g_S.autoStrafe || !(GetAsyncKeyState(VK_SPACE) & 0x8000)) return;

    if (!g_S.autoStrafeBidirectional) {
        if (cmd->mousedx != 0)
            cmd->sidemove = cmd->mousedx > 0 ? 450.f : -450.f;
        return;
    }

    const int wishButtons = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT;
    if (!(cmd->buttons & wishButtons)) return;
    if (o_Velocity < 0) return;

    Vec3 velocity{};
    __try { velocity = Field<Vec3>(local, o_Velocity); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    const float speed2d = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);

    if (!std::isfinite(speed2d) || speed2d < 8.f) return;

    const float f = cmd->forwardmove;
    const float side = cmd->sidemove;
    if (fabsf(f) < 0.01f && fabsf(side) < 0.01f) return;

    const float yaw = cmd->viewangles.y * 0.017453292519943295f;
    const float fx = cosf(yaw), fy = sinf(yaw);
    const float rx = -sinf(yaw), ry = cosf(yaw);
    const float wishX = fx * f + rx * side;
    const float wishY = fy * f + ry * side;
    if (fabsf(wishX) < 0.001f && fabsf(wishY) < 0.001f) return;

    const float wishYaw = atan2f(wishY, wishX) * 57.29577951308232f;
    const float velocityYaw = atan2f(velocity.y, velocity.x) * 57.29577951308232f;
    const float dirDelta = normAngle(wishYaw - velocityYaw);
    if (!std::isfinite(dirDelta) || fabsf(dirDelta) > g_S.autoStrafeMaxDelta) return;

    float turnScale = 0.9f + 0.1f * g_S.autoStrafeTurnScale;
    float sideSign;
    if (fabsf(dirDelta) < 0.05f) {

        sideSign = (cmd->command_number & 1) ? 1.f : -1.f;
    } else {
        sideSign = dirDelta > 0.f ? -1.f : 1.f;
    }
    const float rotation = (sideSign * 90.f + dirDelta * turnScale) * 0.017453292519943295f;
    const float c = cosf(rotation), sn = sinf(rotation);
    cmd->forwardmove = f * c - side * sn;
    cmd->sidemove = f * sn + side * c;
}

namespace {
struct AimClassifyCacheEntry {
    void* entity = nullptr;
    int index = -1;
    DWORD bucket = 0xFFFFFFFFu;
    bool checked = false;
    bool result = false;
    Target target{};
};
static AimClassifyCacheEntry g_AimClassifyCache[512]{};

static bool classifyAimCached(void* entity, int index, Target& out) {
    if (!entity || index < 0) return false;
    const DWORD bucket = GetTickCount() / 48u;
    const uintptr_t raw = reinterpret_cast<uintptr_t>(entity);
    const size_t slot = ((raw >> 4) ^ (raw >> 12) ^
        (static_cast<uintptr_t>(index) * 2654435761u)) & 511u;
    AimClassifyCacheEntry& cache = g_AimClassifyCache[slot];
    if (cache.checked && cache.entity == entity && cache.index == index && cache.bucket == bucket) {
        if (!cache.result) return false;
        Target current = cache.target;
        __try {
            if (Dormant(entity)) return false;
            if (current.player && o_LifeState >= 0 && Field<uint8_t>(entity, o_LifeState) != 0)
                return false;
            if (o_Health >= 0) {
                const int hp = Field<int>(entity, o_Health);
                if (current.player && hp <= 0) return false;
                current.health = (!current.player && hp <= 0) ? -1 : hp;
            }
            if (o_Origin >= 0) current.origin = Field<Vec3>(entity, o_Origin);
            if (o_Mins >= 0) current.mins = Field<Vec3>(entity, o_Mins);
            if (o_Maxs >= 0) current.maxs = Field<Vec3>(entity, o_Maxs);
            current.aimPos = current.origin + (current.mins + current.maxs) * 0.5f;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        if (!std::isfinite(current.origin.x) || !std::isfinite(current.origin.y) ||
            !std::isfinite(current.origin.z) || !std::isfinite(current.aimPos.x) ||
            !std::isfinite(current.aimPos.y) || !std::isfinite(current.aimPos.z))
            return false;
        out = current;
        return true;
    }

    Target classified{};
    const bool result = classify(entity, index, classified);
    cache.entity = entity;
    cache.index = index;
    cache.bucket = bucket;
    cache.checked = true;
    cache.result = result;
    if (result) cache.target = classified;
    if (result) out = classified;
    return result;
}

struct AimRankedTarget {
    Target target{};
    float centerFov = 0.f;
    float angularPadding = 0.f;
    float rank = 0.f;
    bool boundsValid = false;
};

static constexpr int kAimShortlistCapacity = 8;
}

static bool readAimBroadphaseBounds(void* ent, Vec3& center, float& radius) {
    if (!ent || o_Origin < 0 || o_Mins < 0 || o_Maxs < 0) return false;

    __try {
        const Vec3 origin = Field<Vec3>(ent, o_Origin);
        const Vec3 mins = Field<Vec3>(ent, o_Mins);
        const Vec3 maxs = Field<Vec3>(ent, o_Maxs);
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
            !std::isfinite(mins.x) || !std::isfinite(mins.y) || !std::isfinite(mins.z) ||
            !std::isfinite(maxs.x) || !std::isfinite(maxs.y) || !std::isfinite(maxs.z))
            return false;
        const Vec3 localCenter = (mins + maxs) * 0.5f;
        const float rx = fmaxf(fabsf(mins.x - localCenter.x), fabsf(maxs.x - localCenter.x));
        const float ry = fmaxf(fabsf(mins.y - localCenter.y), fabsf(maxs.y - localCenter.y));
        const float rz = fmaxf(fabsf(mins.z - localCenter.z), fabsf(maxs.z - localCenter.z));
        const float r2 = rx * rx + ry * ry + rz * rz;
        if (!std::isfinite(r2) || r2 < 1.f || r2 > 4194304.f) return false;
        center = origin + localCenter;
        radius = sqrtf(r2);
        return std::isfinite(center.x) && std::isfinite(center.y) &&
               std::isfinite(center.z) && std::isfinite(radius);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool isAimEntityOutsideCone(void* ent, const Vec3& eye, const QAngle& angles,
                                   float coneFov, float& centerFov, float& angularPadding,
                                   bool& boundsValid) {
    boundsValid = false;
    centerFov = 0.f;
    angularPadding = 0.f;
    Vec3 center{};
    float radius = 0.f;
    if (!readAimBroadphaseBounds(ent, center, radius)) return false;
    boundsValid = true;

    const Vec3 delta = center - eye;
    const float distance2 = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    if (!std::isfinite(distance2) || distance2 < 1.f) return false;
    const float distance = sqrtf(distance2);

    angularPadding = (radius * 57.2957795f) / fmaxf(distance - radius, 1.f) + 2.f;
    if (!std::isfinite(angularPadding)) angularPadding = 45.f;
    if (angularPadding < 2.5f) angularPadding = 2.5f;
    if (angularPadding > 45.f) angularPadding = 45.f;

    const float limit = coneFov + angularPadding + 1.0f;
    if (limit < 88.f) {
        static float cachedPitch = 99999.f;
        static float cachedYaw = 99999.f;
        static Vec3 cachedForward{};
        if (cachedPitch != angles.x || cachedYaw != angles.y) {
            const float pitch = angles.x * 0.017453292519943295f;
            const float yaw = angles.y * 0.017453292519943295f;
            const float cp = cosf(pitch);
            cachedForward = Vec3(cp * cosf(yaw), cp * sinf(yaw), -sinf(pitch));
            cachedPitch = angles.x;
            cachedYaw = angles.y;
        }
        const float along = delta.x * cachedForward.x + delta.y * cachedForward.y +
                            delta.z * cachedForward.z;
        if (!std::isfinite(along) || along <= 0.f) return true;
        const float cosine = cosf(limit * 0.017453292519943295f);
        if (along * along < distance2 * cosine * cosine) return true;
    }

    const QAngle targetAngle = calcAngle(eye, center);
    const float dp = normAngle(targetAngle.x - angles.x);
    const float dy = normAngle(targetAngle.y - angles.y);
    centerFov = sqrtf(dp * dp + dy * dy);
    if (!std::isfinite(centerFov)) { boundsValid = false; return false; }
    return centerFov > coneFov + angularPadding;
}

namespace {
static constexpr int kAimEntityScanBudget = 192;  

struct AimIncrementalScan {
    bool configurationInitialized = false;
    bool building = false;
    bool activeValid = false;
    int activeBuffer = 0;
    void* configList = nullptr;
    void* configLocal = nullptr;
    int configFirst = -1;
    int configLast = -1;
    bool configAimActive = false;
    bool configTriggerActive = false;
    bool configWantPlayers = false;
    bool configWantNpcs = false;
    DWORD configExcludeMask = 0;
    float configAimFov = -1.f;
    float configTriggerFov = -1.f;

    void* scanList = nullptr;
    int scanFirst = 1;
    int scanLast = 0;
    int cursor = 1;
    bool scanAimActive = false;
    bool scanTriggerActive = false;
    bool scanWantPlayers = false;
    bool scanWantNpcs = false;
    DWORD scanExcludeMask = 0;
    float scanAimFov = 25.f;
    float scanTriggerFov = 3.f;
    Vec3 scanEye{};
    QAngle scanAngles{};
    ULONGLONG lastScanStartedAt = 0;

    AimRankedTarget aimCandidates[2][kAimShortlistCapacity]{};
    AimRankedTarget triggerCandidates[2][kAimShortlistCapacity]{};
    int aimCount[2]{};
    int triggerCount[2]{};
};
static AimIncrementalScan g_AimIncrementalScan{};

static void insertAimCandidate(AimRankedTarget* candidates, int& count,
                               const AimRankedTarget& candidate) {
    int insertAt = count;
    if (insertAt >= kAimShortlistCapacity) {
        if (candidate.rank >= candidates[count - 1].rank) return;
        insertAt = count - 1;
    } else {
        ++count;
    }
    while (insertAt > 0 && candidates[insertAt - 1].rank > candidate.rank) {
        candidates[insertAt] = candidates[insertAt - 1];
        --insertAt;
    }
    candidates[insertAt] = candidate;
}

static void updateAimScanIncremental(
    void* local, const Vec3& eye, const QAngle& angles,
    bool aimActive, bool triggerActive, bool wantPlayers, bool wantNpcs,
    float aimFov, float triggerFov,
    const AimRankedTarget*& aimCandidates, int& aimCount,
    const AimRankedTarget*& triggerCandidates, int& triggerCount) {
    AimIncrementalScan& scan = g_AimIncrementalScan;
    aimCandidates = nullptr;
    triggerCandidates = nullptr;
    aimCount = triggerCount = 0;

    const bool scanNeeded = (aimActive || triggerActive) && (wantPlayers || wantNpcs);
    if (!scanNeeded || !local) {
        scan.building = false;
        scan.activeValid = false;
        scan.lastScanStartedAt = 0;
        return;
    }

    void* list = g_EntList;
    const int maxClients = MaxClients();
    const int maxEntities = MaxEnt();
    int first = wantPlayers ? 1 : maxClients + 1;
    int last = wantNpcs ? maxEntities : maxClients;
    if (first < 1) first = 1;
    if (last > maxEntities) last = maxEntities;
    if (!wantPlayers && !wantNpcs) last = first - 1;
    if (last < first) last = first - 1;

    const bool configChanged = !scan.configurationInitialized ||
        scan.configList != list || scan.configLocal != local ||
        scan.configFirst != first || scan.configLast != last ||
        scan.configAimActive != aimActive || scan.configTriggerActive != triggerActive ||
        scan.configWantPlayers != wantPlayers || scan.configWantNpcs != wantNpcs ||
        scan.configExcludeMask != g_S.aimExcludeMask ||
        scan.configAimFov != aimFov || scan.configTriggerFov != triggerFov;
    if (configChanged) {
        scan.configurationInitialized = true;
        scan.configList = list;
        scan.configLocal = local;
        scan.configFirst = first;
        scan.configLast = last;
        scan.configAimActive = aimActive;
        scan.configTriggerActive = triggerActive;
        scan.configWantPlayers = wantPlayers;
        scan.configWantNpcs = wantNpcs;
        scan.configExcludeMask = g_S.aimExcludeMask;
        scan.configAimFov = aimFov;
        scan.configTriggerFov = triggerFov;
        scan.building = false;
        scan.activeValid = false;
        scan.lastScanStartedAt = 0;
    }

    if (!list || last < first) {
        scan.building = false;
        scan.activeValid = true;
        scan.aimCount[scan.activeBuffer] = 0;
        scan.triggerCount[scan.activeBuffer] = 0;
        scan.lastScanStartedAt = GetTickCount64();
        return;
    }

    const DWORD interval = (g_S.aimScanIntervalMs >= 16.f && g_S.aimScanIntervalMs <= 200.f)
        ? static_cast<DWORD>(g_S.aimScanIntervalMs) : 64u;
    const ULONGLONG now = GetTickCount64();
    if (!scan.building && (!scan.activeValid || configChanged || scan.lastScanStartedAt == 0 ||
        now < scan.lastScanStartedAt || now - scan.lastScanStartedAt >= interval)) {
        scan.building = true;
        scan.lastScanStartedAt = now;
        scan.scanList = list;
        scan.scanFirst = first;
        scan.scanLast = last;
        scan.cursor = first;
        scan.scanAimActive = aimActive;
        scan.scanTriggerActive = triggerActive;
        scan.scanWantPlayers = wantPlayers;
        scan.scanWantNpcs = wantNpcs;
        scan.scanExcludeMask = g_S.aimExcludeMask;
        scan.scanAimFov = aimFov;
        scan.scanTriggerFov = triggerFov;
        scan.scanEye = eye;
        scan.scanAngles = angles;
        const int buildBuffer = 1 - scan.activeBuffer;
        scan.aimCount[buildBuffer] = 0;
        scan.triggerCount[buildBuffer] = 0;
    }

    if (scan.building) {
        const int buildBuffer = 1 - scan.activeBuffer;
        const float broadCone = scan.scanAimActive && scan.scanTriggerActive
            ? (scan.scanAimFov > scan.scanTriggerFov ? scan.scanAimFov : scan.scanTriggerFov)
            : (scan.scanAimActive ? scan.scanAimFov : scan.scanTriggerFov);
        int processed = 0;
        while (scan.cursor <= scan.scanLast && processed < kAimEntityScanBudget) {
            const int index = scan.cursor++;
            ++processed;
            void* ent = Ent(index);
            if (!ent || ent == local) continue;

            float centerFov = 0.f, angularPadding = 0.f;
            bool boundsValid = false;
            if (isAimEntityOutsideCone(ent, scan.scanEye, scan.scanAngles,
                                      broadCone, centerFov, angularPadding, boundsValid))
                continue;

            Target target{};
            if (!classifyAimCached(ent, index, target)) continue;
            if (target.player ? !scan.scanWantPlayers : !scan.scanWantNpcs) continue;
            if (!target.player && isAimEntityExcluded(ent, scan.scanExcludeMask)) continue;

            const bool aimCandidate = scan.scanAimActive &&
                (!boundsValid || centerFov <= scan.scanAimFov + angularPadding);
            const bool triggerCandidate = scan.scanTriggerActive &&
                (!boundsValid || centerFov <= scan.scanTriggerFov + angularPadding ||
                 (scan.scanAimActive && centerFov <= scan.scanAimFov + angularPadding));

            if (aimCandidate) {
                AimRankedTarget candidate{};
                candidate.target = target;
                candidate.centerFov = centerFov;
                candidate.angularPadding = angularPadding;
                candidate.boundsValid = boundsValid;
                candidate.rank = boundsValid ? centerFov : (scan.scanAimFov + 45.f);
                insertAimCandidate(scan.aimCandidates[buildBuffer], scan.aimCount[buildBuffer], candidate);
            }
            if (triggerCandidate) {
                AimRankedTarget candidate{};
                candidate.target = target;
                candidate.centerFov = centerFov;
                candidate.angularPadding = angularPadding;
                candidate.boundsValid = boundsValid;
                candidate.rank = boundsValid ? centerFov : (scan.scanTriggerFov + 45.f);
                insertAimCandidate(scan.triggerCandidates[buildBuffer], scan.triggerCount[buildBuffer], candidate);
            }
        }

        if (scan.cursor > scan.scanLast) {
            scan.activeBuffer = buildBuffer;
            scan.activeValid = true;
            scan.building = false;
        }
    }

    if (scan.activeValid) {
        aimCandidates = scan.aimCandidates[scan.activeBuffer];
        aimCount = scan.aimCount[scan.activeBuffer];
        triggerCandidates = scan.triggerCandidates[scan.activeBuffer];
        triggerCount = scan.triggerCount[scan.activeBuffer];
    }
}
}

void runCreateMove(float ft, CUserCmd* cmd) {
    if (!cmd || !cmd->command_number || !InGame() || g_MenuOpen) return;
    void* local = Ent(LocalIndex());
    if (!local) return;

    if (o_Flags >= 0) pickRayVariant(local);

    bool space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (o_Flags >= 0) {
        const int playerFlags = Field<int>(local, o_Flags);
        bool ground = (playerFlags & 1) != 0;

        const bool onLadder = isLocalOnLadder();

        static bool wasGround = false;
        static bool assist = false;
        if (ground && !wasGround) {
            float c = g_S.bhopChance;
            if (c >= 99.9f) assist = true;
            else if (c <= 0.1f) assist = false;
            else assist = (rand() % 10000) < (int)(c * 100.f);
        }
        wasGround = ground;
        if (space && !onLadder) {
            if (g_S.bhop && assist) {
                if (ground) cmd->buttons |= IN_JUMP;
                else cmd->buttons &= ~IN_JUMP;
            }
            if (!ground) applyAutoStrafe(local, cmd);
        }
        doEdgebug(local, cmd, ft, ground);

        doJumpbug(local, cmd, ft);
        doEdgejump(local, cmd, ft, ground);
    }

    const bool aimHeld = g_S.aim &&
        (g_S.aimKey <= 0 || (GetAsyncKeyState(g_S.aimKey) & 0x8000));
    const bool triggerActive = g_S.autoTrigger && !g_MenuOpen;
    const bool haveAimTargetTypes = g_S.aimPlayers || g_S.aimNpcs;
    const float aimFovScan = std::isfinite(g_S.aimFov)
        ? (g_S.aimFov < 1.0f ? 1.0f : (g_S.aimFov > 180.0f ? 180.0f : g_S.aimFov))
        : 25.0f;
    const float triggerFovScan = std::isfinite(g_S.autoTriggerFov)
        ? (g_S.autoTriggerFov < 0.5f ? 0.5f : (g_S.autoTriggerFov > 15.f ? 15.f : g_S.autoTriggerFov))
        : 3.f;
    const Vec3 scanEye = eyePos(local);
    const QAngle scanAngles = cmd->viewangles;
    const AimRankedTarget* aimCandidates = nullptr;
    const AimRankedTarget* triggerCandidates = nullptr;
    int aimCandidateCount = 0;
    int triggerCandidateCount = 0;
    updateAimScanIncremental(local, scanEye, scanAngles, aimHeld, triggerActive,
        g_S.aimPlayers, g_S.aimNpcs, aimFovScan, triggerFovScan,
        aimCandidates, aimCandidateCount, triggerCandidates, triggerCandidateCount);

    if (aimHeld) {
        Vec3 eye = scanEye;
        QAngle cur = cmd->viewangles;

        const float aimFov = aimFovScan;
        const float aimSmooth = std::isfinite(g_S.aimSmooth)
            ? (g_S.aimSmooth < 1.0f ? 1.0f : (g_S.aimSmooth > 20.0f ? 20.0f : g_S.aimSmooth))
            : 4.0f;
        float bestFov = aimFov;
        Target best{};
        bool have = false;

        struct AimPointCandidate {
            float fovSquared;
            Vec3 point;
        };

        const int mask = g_S.aimBones > 0 ? g_S.aimBones : (1 << g_S.aimBone);
        for (int candidateIndex = 0; candidateIndex < aimCandidateCount; ++candidateIndex) {
            AimRankedTarget ranked = aimCandidates[candidateIndex];
            if (!ranked.target.ent || Ent(ranked.target.index) != ranked.target.ent) continue;
            Target liveTarget{};
            if (!classifyAimCached(ranked.target.ent, ranked.target.index, liveTarget)) continue;
            ranked.target = liveTarget;
            if (isAimEntityOutsideCone(ranked.target.ent, eye, cur, aimFov,
                                       ranked.centerFov, ranked.angularPadding, ranked.boundsValid))
                continue;
            if (ranked.boundsValid && ranked.centerFov > bestFov + ranked.angularPadding)
                continue;

            AimPointCandidate points[kBoneCount]{};
            int pointCount = 0;
            for (int b = 0; b < kBoneCount; ++b) {
                if (!(mask & (1 << b))) continue;
                Vec3 pos{};
                if (!tryAimPoint(ranked.target, b, pos)) continue;
                const QAngle a = calcAngle(eye, pos);
                const float dp = normAngle(a.x - cur.x);
                const float dy = normAngle(a.y - cur.y);
                const float fovSquared = dp * dp + dy * dy;
                if (!std::isfinite(fovSquared) || fovSquared >= bestFov * bestFov) continue;

                int insertAt = pointCount;
                while (insertAt > 0 && points[insertAt - 1].fovSquared > fovSquared) {
                    points[insertAt] = points[insertAt - 1];
                    --insertAt;
                }
                if (pointCount < kBoneCount) ++pointCount;
                if (insertAt < kBoneCount) points[insertAt] = AimPointCandidate{fovSquared, pos};
            }

            int visibilityPointsTested = 0;
            for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
                const AimPointCandidate& point = points[pointIndex];
                if (point.fovSquared >= bestFov * bestFov) break;

                if (g_S.aimVisible && visibilityPointsTested >= 3) break;
                Target cand = ranked.target;
                cand.aimPos = point.point;
                if (g_S.aimVisible) {
                    ++visibilityPointsTested;
                    if (!visible(local, eye, cand)) continue;
                }
                bestFov = sqrtf(point.fovSquared);
                best = cand;
                have = true;
                break;
            }
        }
        if (have) {
            QAngle want = calcAngle(eye, best.aimPos);
            const float s = aimSmooth;
            QAngle delta(normAngle(want.x - cur.x) / s, normAngle(want.y - cur.y) / s, 0);
            cmd->viewangles.x = cur.x + delta.x;
            cmd->viewangles.y = cur.y + delta.y;
            fixMove(cmd, cur.y);
        }
    }

    if (triggerActive) {
        Vec3 eye = eyePos(local);
        QAngle cur = cmd->viewangles;
        const float trigFov = triggerFovScan;
        bool shouldFire = false;
        for (int entityOffset = 0; entityOffset < triggerCandidateCount && !shouldFire; ++entityOffset) {
            const AimRankedTarget& ranked = triggerCandidates[entityOffset];
            Target t = ranked.target;
            void* e = t.ent;
            if (!e || e == local || Ent(t.index) != e) continue;
            Target liveTarget{};
            if (!classifyAimCached(e, t.index, liveTarget)) continue;
            t = liveTarget;
            if (t.player ? !g_S.aimPlayers : !g_S.aimNpcs) continue;
            if (!t.player && isAimEntityExcluded(e, g_S.aimExcludeMask)) continue;
            float centerFov = 0.f, angularPadding = 0.f;
            bool boundsValid = false;
            if (isAimEntityOutsideCone(e, eye, cur, trigFov,
                                       centerFov, angularPadding, boundsValid)) continue;
            struct TriggerPoint { float fovSquared; Vec3 point; };
            TriggerPoint points[kBoneCount]{};
            int pointCount = 0;
            const int mask = g_S.aimBones > 0 ? g_S.aimBones : (1 << g_S.aimBone);
            const float trigFovSquared = trigFov * trigFov;
            for (int b = 0; b < kBoneCount; ++b) {
                if (!(mask & (1 << b))) continue;
                Vec3 pos{};
                if (!tryAimPoint(t, b, pos)) continue;
                const QAngle a = calcAngle(eye, pos);
                const float dp = normAngle(a.x - cur.x);
                const float dy = normAngle(a.y - cur.y);
                const float fovSquared = dp * dp + dy * dy;
                if (!std::isfinite(fovSquared) || fovSquared > trigFovSquared) continue;
                int insertAt = pointCount;
                while (insertAt > 0 && points[insertAt - 1].fovSquared > fovSquared) {
                    points[insertAt] = points[insertAt - 1];
                    --insertAt;
                }
                if (pointCount < kBoneCount) ++pointCount;
                if (insertAt < kBoneCount) points[insertAt] = TriggerPoint{fovSquared, pos};
            }

            for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
                Target cand = t;
                cand.aimPos = points[pointIndex].point;
                if (g_S.aimVisible && !visible(local, eye, cand)) continue;
                shouldFire = true;
                break;
            }
        }
        if (shouldFire) cmd->buttons |= IN_ATTACK;
    }

    {
        static DWORD s_nextLadderPulse = 0;
        const int ladderKey = g_S.kLadderSpeed;
        const bool ladderHeld = ladderKey > 0 && ladderKey < 256 &&
            (GetAsyncKeyState(ladderKey) & 0x8000) != 0;
        if (!ladderHeld) {
            s_nextLadderPulse = 0;
        } else {
            const DWORD now = GetTickCount();
            const DWORD delay = static_cast<DWORD>(g_S.ladderSpeedDelayMs < 1.f ? 1.f : g_S.ladderSpeedDelayMs);
            if (s_nextLadderPulse == 0 || static_cast<LONG>(now - s_nextLadderPulse) >= 0) {
                cmd->buttons |= IN_JUMP | IN_DUCK;
                s_nextLadderPulse = now + delay;
            }
        }
    }

    updateCombatFeedback(cmd);
}
