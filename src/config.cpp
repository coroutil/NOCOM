#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "config.h"
#include "settings.h"
#include "menu.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <windows.h>

std::vector<std::string> g_Cfgs;

std::string sanitizeName(const std::string& in) {
    std::string o;
    for (char ch : in) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')
            o.push_back(ch);
        else if ((ch == ' ' || ch == '.') && !o.empty())
            o.push_back('_');
        if (o.size() >= 32) break;
    }
    return o.empty() ? "default" : o;
}

std::string cfgDir() {
    char env[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", env, MAX_PATH);
    std::string dir = (n > 0 && n < MAX_PATH) ? std::string(env) : std::string(".");
    dir += "\\NOCOM";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

static std::string cfgFile(const std::string& name) {
    return cfgDir() + "\\" + sanitizeName(name) + ".cfg";
}

#define NOCOM_CFG_FIELDS(X) \
    X(esp) X(espBones) X(espBox) X(espName) X(espHealth) X(espEntities) X(espItems) X(waterBombTrajectories) X(nadeBoostIndicator) X(prediction) X(espMaxDistance) X(espUpdateIntervalMs) X(espPlayers) X(espNpcs) \
    X(aim) X(aimVisible) X(aimPlayers) X(aimNpcs) X(aimBone) X(aimBones) X(aimFov) X(aimSmooth) X(aimExcludeMask) X(aimKey) X(viewmodelSway) X(viewmodelSwayAmount) X(autoTrigger) X(autoTriggerFov) X(espWires) X(espWiresPlayers) X(espWiresNpcs) X(espWiresItems) X(espWireColorByType) X(espWireWidth) X(espWireR) X(espWireG) X(espWireB) X(espWireHostileR) X(espWireHostileG) X(espWireHostileB) X(espWireFriendlyR) X(espWireFriendlyG) X(espWireFriendlyB) X(espWireItemR) X(espWireItemG) X(espWireItemB) \
    X(fovOn) X(fov) X(backCam) X(backCamPitch) X(kBackCam) X(projCam) X(projCamDist) X(viewmodelOn) X(viewmodelFovOn) X(viewmodelFov) X(viewmodelX) X(viewmodelY) X(viewmodelZ) X(viewmodelRotX) X(viewmodelRotY) X(viewmodelRotZ) X(fullBright) X(kFullBright) X(svCheatsOverride) X(svPropJumpOverride) X(drawClipBrushes) X(kDrawClipBrushes) X(customFog) X(fogStart) X(fogEnd) X(fogDensity) X(fogR) X(fogG) X(fogB) X(hudColorMod) X(hudColorR) X(hudColorG) X(hudColorB) X(bhop) X(bhopChance) X(autoStrafe) X(autoStrafeBidirectional) X(autoStrafeTurnScale) X(autoStrafeMaxDelta) \
    X(watermark) X(wmBgOn) X(wmX) X(wmY) X(wmPad) X(wmScale) X(wmAlpha) \
    X(wmTextR) X(wmTextG) X(wmTextB) X(wmBgR) X(wmBgG) X(wmBgB) \
    X(wmImg) X(wmImgDvd) X(wmImgX) X(wmImgY) X(wmImgScale) X(wmImgAlpha) X(wmImgDvdSpeed) \
    X(jumpbug) X(jbKey) X(edgebug) X(kEdgebug) X(edgebugTicks) X(edgebugIndicator) X(edgebugX) X(edgebugY) X(edgejump) X(kEdgejump) X(edgejumpMinSpeed) X(edgejumpLookaheadTicks) X(ladderSpeed) X(kLadderSpeed) X(ladderSpeedDelayMs) X(menuKey) X(netStats) X(discordRpc) X(discordShowServer) X(speedometer) X(speedometerShowBest) X(speedometerBackground) X(speedometerFontSize) X(speedometerX) X(speedometerY) X(netX) X(netY) X(uiAlpha) X(uiBlur) X(uiBevel) X(debugMode) X(processHighPriority) X(processAboveNormal) X(processAffinityFastCores) X(processDisablePowerThrottle) X(processHighResTimer) X(processMainThreadBoost) X(processIdleWhenUnfocused) X(engineThreadedBones) X(engineThreadedParticles) X(engineMatQueueMode) X(engineMultiCore) X(networkSettingsEnabled) X(networkPreset) X(networkRate) X(networkCmdRate) X(networkUpdateRate) X(networkInterp) X(networkInterpRatio) X(networkTimeout) X(networkPredict) X(networkPredictWeapons) X(networkLagCompensation) X(networkInterpolate) X(networkSmooth) X(networkSmoothTime) X(networkInterpNpcs) X(networkPredOptimize) X(networkQosMode) X(networkCompressPackets) X(networkCompressMinSize) X(networkQueuedPacketThread) X(networkSplitRate) X(networkSplitPacketMaxRate) X(networkMaxRoutable) X(networkMaxFragments) X(networkMaxClearTime) X(networkMaxPacketDrop) X(networkNetGraph) X(networkNetGraphPos) X(networkNetGraphProportionalFont) X(aimScanIntervalMs) X(trajectoryBudget) \
    X(uiBgR) X(uiBgG) X(uiBgB) X(uiSideR) X(uiSideG) X(uiSideB) \
    X(uiTextR) X(uiTextG) X(uiTextB) X(uiSelR) X(uiSelG) X(uiSelB) \
    X(uiAccR) X(uiAccG) X(uiAccB) X(uiHoverR) X(uiHoverG) X(uiHoverB) \
    X(uiBtnR) X(uiBtnG) X(uiBtnB) X(uiFieldR) X(uiFieldG) X(uiFieldB) \
    X(uiCheckR) X(uiCheckG) X(uiCheckB) X(uiCloseR) X(uiCloseG) X(uiCloseB) \
    X(kEsp) \
    X(kAim) X(kAimVisible) X(kAimPlayers) X(kAimNpcs) X(kAutoStrafe) \
    X(kJumpbug) X(kNetStats) X(kEdgejump) X(kMicSpam) X(voiceLoopback) X(deathSound) X(deathSoundVolume)

template <class T> static void cfgWrite(FILE* f, const char* k, T v) {
    fprintf(f, "%s=%g\n", k, (double)v);
}

void refreshCfgs() {
    g_Cfgs.clear();
    std::string pat = cfgDir() + "\\*.cfg";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string n = fd.cFileName;
            size_t dot = n.rfind('.');
            if (dot != std::string::npos) n.resize(dot);
            if (!n.empty()) g_Cfgs.push_back(n);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(g_Cfgs.begin(), g_Cfgs.end());
}

static void rememberActive() {
    std::string p = cfgDir() + "\\active.txt";
    FILE* f = fopen(p.c_str(), "w");
    if (!f) return;
    fputs(sanitizeName(g_CfgName).c_str(), f);
    fclose(f);
}

bool saveConfig(const std::string& name) {
    std::string clean = sanitizeName(name);
    FILE* f = fopen(cfgFile(clean).c_str(), "w");
    if (!f) return false;
    fprintf(f, "# NOCOM\n");
#define X(n) cfgWrite(f, #n, g_S.n);
    NOCOM_CFG_FIELDS(X)
#undef X
    fprintf(f, "wmText=%s\n", g_WmText.c_str());
    fprintf(f, "uiFont=%s\n", g_UiFont.c_str());
    fprintf(f, "speedometerFont=%s\n", g_S.speedometerFont.c_str());
    fprintf(f, "wmImgPath=%s\n", g_WmImgPath.c_str());
    fprintf(f, "deathSoundPath=%s\n", g_S.deathSoundPath.c_str());
    fclose(f);
    g_CfgName = clean;
    g_NameEdit = clean;
    rememberActive();
    refreshCfgs();
    return true;
}

bool loadConfig(const std::string& name) {
    std::string clean = sanitizeName(name);
    FILE* f = fopen(cfgFile(clean).c_str(), "r");
    if (!f) return false;

    char line[2304];
    bool sawViewmodelFovOn = false;
    bool sawWmImgDvdSpeed = false;
    bool sawAimNpcExclusion = false;
    bool sawKFullBright = false;
    bool sawDeathSoundVolume = false;
    bool sawWaterBombTrajectories = false;
    bool sawNadeBoostIndicator = false;
    bool sawPrediction = false;
    bool sawEspMaxDistance = false;
    bool sawEspUpdateIntervalMs = false;
    bool sawKLadderSpeed = false;
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;
        size_t vl = strlen(val);
        while (vl && (val[vl - 1] == '\n' || val[vl - 1] == '\r')) val[--vl] = 0;
        if (!strcmp(line, "uiFont")) {
            g_UiFont = val;
            continue;
        }
        if (!strcmp(line, "speedometerFont")) {
            g_S.speedometerFont.assign(val);
            if (g_S.speedometerFont.size() > 63) g_S.speedometerFont.resize(63);
            continue;
        }
        if (!strcmp(line, "wmText")) {
            g_WmText.assign(val);
            if (g_WmText.size() > 32) g_WmText.resize(32);
            continue;
        }
        if (!strcmp(line, "wmImgPath")) {
            g_WmImgPath.assign(val);
            if (g_WmImgPath.size() > 2048) g_WmImgPath.resize(2048);
            continue;
        }
        if (!strcmp(line, "deathSoundPath")) {
            g_S.deathSoundPath.assign(val);

            if (g_S.deathSoundPath == "common/bugreporter_failed")
                g_S.deathSoundPath = "buttons/button14";
            if (g_S.deathSoundPath.size() > 2048) g_S.deathSoundPath.resize(2048);
            continue;
        }
        if (!strcmp(line, "wmImgDvdSpeed")) {
            g_S.wmImgDvdSpeed = static_cast<float>(atof(val));
            sawWmImgDvdSpeed = true;
            continue;
        }
        if (!strcmp(line, "kFullBright")) {
            g_S.kFullBright = atoi(val);
            sawKFullBright = true;
            continue;
        }
        if (!strcmp(line, "kLadderSpeed")) {
            const int parsedBind = atoi(val);
            g_S.kLadderSpeed = parsedBind >= 0 && parsedBind < 256 ? parsedBind : 0;
            sawKLadderSpeed = true;
            continue;
        }
        if (!strcmp(line, "deathSoundVolume")) {
            g_S.deathSoundVolume = static_cast<float>(atof(val));
            sawDeathSoundVolume = true;
            continue;
        }
        if (!strcmp(line, "waterBombTrajectories")) {
            g_S.waterBombTrajectories = atof(val) != 0.0;
            sawWaterBombTrajectories = true;
            continue;
        }
        if (!strcmp(line, "nadeBoostIndicator")) {
            g_S.nadeBoostIndicator = atof(val) != 0.0;
            sawNadeBoostIndicator = true;
            continue;
        }
        if (!strcmp(line, "prediction")) {
            g_S.prediction = atof(val) != 0.0;
            sawPrediction = true;
            continue;
        }
        if (!strcmp(line, "espMaxDistance")) {
            g_S.espMaxDistance = static_cast<float>(atof(val));
            sawEspMaxDistance = true;
            continue;
        }
        if (!strcmp(line, "espUpdateIntervalMs")) {
            g_S.espUpdateIntervalMs = static_cast<float>(atof(val));
            sawEspUpdateIntervalMs = true;
            continue;
        }
        if (!strcmp(line, "aimNpcExclusion")) {

            const int mode = atoi(val);
            sawAimNpcExclusion = true;
            if (mode <= 0) g_S.aimExcludeMask = 0;
            else if (mode == 1) g_S.aimExcludeMask = (1u << 10) | (1u << 11); /* RPG + combine ball */
            else if (mode == 2) g_S.aimExcludeMask = (1u << 10) | (1u << 11) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 15);
            else if (mode == 3) g_S.aimExcludeMask = 0x3FFu; /* friendlies */
            else g_S.aimExcludeMask = 0x3FFu | (1u << 10) | (1u << 11) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 15);
            continue;
        }
        double v = atof(val);

        if (!strcmp(line, "ladderSpeedDelayTicks")) { g_S.ladderSpeedDelayMs = (float)v * 15.f; continue; }
        if (!strcmp(line, "fullbright")) { g_S.fullBright = (v != 0.0); continue; }

        if (!strcmp(line, "viewmodelFovOn")) { g_S.viewmodelFovOn = (v != 0.0); sawViewmodelFovOn = true; continue; }
#define X(nm) if (!strcmp(line, #nm)) { g_S.nm = (decltype(g_S.nm))v; continue; }
        NOCOM_CFG_FIELDS(X)
#undef X
    }
    fclose(f);

    const bool oldLowLatencyDefaults =
        g_S.networkPreset == 1 &&
        g_S.networkRate == 786432.f && g_S.networkCmdRate == 66.f &&
        g_S.networkUpdateRate == 66.f && g_S.networkInterp == 0.f &&
        g_S.networkInterpRatio == 1.f && g_S.networkTimeout == 60.f &&
        !g_S.networkSmooth && g_S.networkSmoothTime == 0.01f &&
        g_S.networkInterpNpcs == 0.f && g_S.networkPredOptimize == 2.f &&
        g_S.networkQosMode == 1;
    const bool oldBalancedDefaults =
        g_S.networkPreset == 2 &&
        g_S.networkRate == 196608.f && g_S.networkCmdRate == 66.f &&
        g_S.networkUpdateRate == 66.f && g_S.networkInterp == 0.f &&
        g_S.networkInterpRatio == 1.f && g_S.networkTimeout == 60.f &&
        g_S.networkSmooth && g_S.networkSmoothTime == 0.1f &&
        g_S.networkInterpNpcs == 0.f && g_S.networkPredOptimize == 2.f &&
        g_S.networkQosMode == 2;
    if (oldLowLatencyDefaults || oldBalancedDefaults) {
        g_S.networkCmdRate = 100.f;
        g_S.networkUpdateRate = 100.f;
    }
    if (!sawViewmodelFovOn && g_S.viewmodelOn)
        g_S.viewmodelFovOn = true;
    if (!sawWmImgDvdSpeed)
        g_S.wmImgDvdSpeed = 100.f;

    if (!sawKFullBright) g_S.kFullBright = 0;
    if (!sawKLadderSpeed) g_S.kLadderSpeed = VK_TAB;

    if (!sawDeathSoundVolume) g_S.deathSoundVolume = 70.f;
    if (!sawWaterBombTrajectories) g_S.waterBombTrajectories = true;
    if (!sawNadeBoostIndicator) g_S.nadeBoostIndicator = true;
    if (!sawPrediction) g_S.prediction = true;
    if (!sawEspMaxDistance) g_S.espMaxDistance = 4000.f;
    if (!sawEspUpdateIntervalMs) g_S.espUpdateIntervalMs = 50.f;
    if (!sawAimNpcExclusion && g_S.aimExcludeMask == 0)
        g_S.aimExcludeMask = 0x0001FFFFu;  
    clampSettings();
    applyGameProcessScheduling();
    applyNetworkSettings();
    g_CfgName = clean;
    g_NameEdit = clean;
    rememberActive();
    return true;
}

bool deleteConfig(const std::string& name) {
    std::string clean = sanitizeName(name);
    if (!DeleteFileA(cfgFile(clean).c_str())) return false;
    refreshCfgs();
    if (g_CfgName == clean) {
        g_CfgName = g_Cfgs.empty() ? "default" : g_Cfgs[0];
        g_NameEdit = g_CfgName;
    }
    return true;
}

void loadActiveName() {
    std::string p = cfgDir() + "\\active.txt";
    FILE* f = fopen(p.c_str(), "r");
    if (!f) return;
    char buf[64] = {};
    if (fgets(buf, sizeof buf, f)) {
        for (char* c = buf; *c; ++c)
            if (*c == '\r' || *c == '\n' || *c == ' ') *c = 0;
        if (buf[0]) g_CfgName = sanitizeName(buf);
    }
    fclose(f);
    g_NameEdit = g_CfgName;
}
