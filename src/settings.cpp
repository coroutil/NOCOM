#include "settings.h"
#include "render.h"
#include <algorithm>
#include <cmath>

Settings g_S;
std::string g_WmText = "NOCOM";
std::string g_WmImgPath;
std::string g_CfgName = "default";
std::string g_NameEdit = "default";
std::string g_UiFont = "Tahoma";

static float clampF(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void clampSettings() {
    if (g_S.aimBone < 0 || g_S.aimBone >= 5) g_S.aimBone = 0;
    if (g_S.aimBones <= 0 || g_S.aimBones > 31) g_S.aimBones = 1;
    g_S.viewmodelSwayAmount = clampF(g_S.viewmodelSwayAmount, 0.f, 25.f);
    g_S.espWireWidth = clampF(g_S.espWireWidth, 1.f, 8.f);
    g_S.espMaxDistance = std::isfinite(g_S.espMaxDistance) ? clampF(g_S.espMaxDistance, 0.f, 10000.f) : 4000.f;
    g_S.espUpdateIntervalMs = std::isfinite(g_S.espUpdateIntervalMs) ? clampF(g_S.espUpdateIntervalMs, 16.f, 150.f) : 66.f;
    g_S.aimScanIntervalMs = std::isfinite(g_S.aimScanIntervalMs) ? clampF(g_S.aimScanIntervalMs, 16.f, 200.f) : 64.f;
    g_S.trajectoryBudget = std::isfinite(g_S.trajectoryBudget) ? clampF(g_S.trajectoryBudget, 1.f, 4.f) : 2.f;
    if (g_S.networkPreset < 0 || g_S.networkPreset > 4) g_S.networkPreset = 2;
    g_S.networkRate = std::isfinite(g_S.networkRate) ? clampF(g_S.networkRate, 20480.f, 786432.f) : 196608.f;
    g_S.networkCmdRate = std::isfinite(g_S.networkCmdRate) ? clampF(g_S.networkCmdRate, 10.f, 128.f) : 100.f;
    g_S.networkUpdateRate = std::isfinite(g_S.networkUpdateRate) ? clampF(g_S.networkUpdateRate, 10.f, 128.f) : 100.f;
    g_S.networkInterp = std::isfinite(g_S.networkInterp) ? clampF(g_S.networkInterp, 0.f, 0.5f) : 0.f;
    g_S.networkInterpRatio = std::isfinite(g_S.networkInterpRatio) ? clampF(g_S.networkInterpRatio, 1.f, 5.f) : 1.f;
    g_S.networkTimeout = std::isfinite(g_S.networkTimeout) ? clampF(g_S.networkTimeout, 15.f, 180.f) : 60.f;
    g_S.networkSmoothTime = std::isfinite(g_S.networkSmoothTime) ? clampF(g_S.networkSmoothTime, 0.f, 2.f) : 0.1f;
    g_S.networkInterpNpcs = std::isfinite(g_S.networkInterpNpcs) ? clampF(g_S.networkInterpNpcs, 0.f, 0.5f) : 0.f;
    g_S.networkPredOptimize = std::isfinite(g_S.networkPredOptimize) ? clampF(g_S.networkPredOptimize, 0.f, 2.f) : 2.f;
    if (g_S.networkQosMode < 0 || g_S.networkQosMode > 3) g_S.networkQosMode = 2;
    g_S.networkCompressMinSize = std::isfinite(g_S.networkCompressMinSize) ? clampF(g_S.networkCompressMinSize, 64.f, 2048.f) : 1024.f;
    g_S.networkSplitRate = std::isfinite(g_S.networkSplitRate) ? clampF(g_S.networkSplitRate, 1.f, 8.f) : 2.f;
    g_S.networkSplitPacketMaxRate = std::isfinite(g_S.networkSplitPacketMaxRate) ? clampF(g_S.networkSplitPacketMaxRate, 10000.f, 786432.f) : 80000.f;
    g_S.networkMaxRoutable = std::isfinite(g_S.networkMaxRoutable) ? clampF(g_S.networkMaxRoutable, 576.f, 1500.f) : 1200.f;
    g_S.networkMaxFragments = std::isfinite(g_S.networkMaxFragments) ? clampF(g_S.networkMaxFragments, 576.f, 4096.f) : 1200.f;
    g_S.networkMaxClearTime = std::isfinite(g_S.networkMaxClearTime) ? clampF(g_S.networkMaxClearTime, 0.f, 1.f) : 0.5f;
    g_S.networkMaxPacketDrop = std::isfinite(g_S.networkMaxPacketDrop) ? clampF(g_S.networkMaxPacketDrop, 0.f, 4000.f) : 100.f;
    if (g_S.networkNetGraphPos < 0 || g_S.networkNetGraphPos > 2) g_S.networkNetGraphPos = 1;
    g_S.espWireR = clampF(g_S.espWireR, 0.f, 255.f);
    g_S.espWireG = clampF(g_S.espWireG, 0.f, 255.f);
    g_S.espWireB = clampF(g_S.espWireB, 0.f, 255.f);
    g_S.espWireHostileR = clampF(g_S.espWireHostileR, 0.f, 255.f);
    g_S.espWireHostileG = clampF(g_S.espWireHostileG, 0.f, 255.f);
    g_S.espWireHostileB = clampF(g_S.espWireHostileB, 0.f, 255.f);
    g_S.espWireFriendlyR = clampF(g_S.espWireFriendlyR, 0.f, 255.f);
    g_S.espWireFriendlyG = clampF(g_S.espWireFriendlyG, 0.f, 255.f);
    g_S.espWireFriendlyB = clampF(g_S.espWireFriendlyB, 0.f, 255.f);
    g_S.espWireItemR = clampF(g_S.espWireItemR, 0.f, 255.f);
    g_S.espWireItemG = clampF(g_S.espWireItemG, 0.f, 255.f);
    g_S.espWireItemB = clampF(g_S.espWireItemB, 0.f, 255.f);
    g_S.autoTriggerFov = clampF(g_S.autoTriggerFov, 0.5f, 15.f);
    g_S.deathSoundVolume = std::isfinite(g_S.deathSoundVolume) ? clampF(g_S.deathSoundVolume, 0.f, 100.f) : 70.f;
    if (g_S.deathSoundPath.size() > 2048) g_S.deathSoundPath.resize(2048);

    g_S.aimFov = clampF(g_S.aimFov, 1.f, 180.f);
    g_S.aimSmooth = clampF(g_S.aimSmooth, 1.f, 20.f);
    if (!std::isfinite(g_S.aimFov)) g_S.aimFov = 25.f;
    if (!std::isfinite(g_S.aimSmooth)) g_S.aimSmooth = 4.f;
    g_S.bhopChance = clampF(g_S.bhopChance, 0.f, 100.f);
    g_S.autoStrafeTurnScale = clampF(g_S.autoStrafeTurnScale, 0.f, 1.f);
    g_S.autoStrafeMaxDelta = clampF(g_S.autoStrafeMaxDelta, 1.f, 180.f);
    g_S.ladderSpeedDelayMs = clampF(g_S.ladderSpeedDelayMs, 1.f, 100.f);
    g_S.edgebugTicks = clampF(g_S.edgebugTicks, 0.f, 128.f);
    g_S.edgebugX = clampF(g_S.edgebugX, 0.f, 100.f);
    g_S.edgebugY = clampF(g_S.edgebugY, 0.f, 100.f);
    g_S.edgejumpMinSpeed = clampF(g_S.edgejumpMinSpeed, 0.f, 250.f);
    g_S.edgejumpLookaheadTicks = clampF(g_S.edgejumpLookaheadTicks, 1.f, 12.f);
    if (g_S.aimKey < 0 || g_S.aimKey > 255) g_S.aimKey = VK_XBUTTON1;
    g_S.fov = clampF(g_S.fov, 10.f, 170.f);
    g_S.wmScale = clampF(g_S.wmScale, 8.f, 72.f);
    g_S.wmX = clampF(g_S.wmX, 0.f, 100.f);
    g_S.wmY = clampF(g_S.wmY, 0.f, 100.f);
    g_S.wmPad = clampF(g_S.wmPad, 0.f, 24.f);
    g_S.wmAlpha = clampF(g_S.wmAlpha, 0.f, 255.f);
    g_S.wmImgX = clampF(g_S.wmImgX, 0.f, 100.f);
    g_S.wmImgY = clampF(g_S.wmImgY, 0.f, 100.f);
    g_S.wmImgScale = clampF(g_S.wmImgScale, 10.f, 400.f);
    g_S.wmImgAlpha = clampF(g_S.wmImgAlpha, 0.f, 255.f);
    g_S.wmImgDvdSpeed = clampF(g_S.wmImgDvdSpeed, 10.f, 500.f);
    if (g_WmText.size() > 32) g_WmText.resize(32);
    if (g_WmImgPath.size() > 2048) g_WmImgPath.resize(2048);
    if (g_S.jbKey < 0 || g_S.jbKey > 255) g_S.jbKey = 0;
    if (g_S.kEdgebug < 0 || g_S.kEdgebug > 255) g_S.kEdgebug = 0;
    if (g_S.kEdgejump < 0 || g_S.kEdgejump > 255) g_S.kEdgejump = 0;
    if (g_S.menuKey <= 0 || g_S.menuKey > 255) g_S.menuKey = VK_INSERT;
    if (g_S.kMicSpam < 0 || g_S.kMicSpam > 255) g_S.kMicSpam = 0;
    if (g_S.kLadderSpeed < 0 || g_S.kLadderSpeed > 255) g_S.kLadderSpeed = 0;
    if (g_S.kFullBright < 0 || g_S.kFullBright > 255) g_S.kFullBright = 0;
    if (g_S.kDrawClipBrushes < 0 || g_S.kDrawClipBrushes > 255) g_S.kDrawClipBrushes = 0;
    if (g_S.netX < 0.f) g_S.netX = 0.f;
    if (g_S.netX > 100.f) g_S.netX = 100.f;
    if (g_S.netY < 0.f) g_S.netY = 0.f;
    if (g_S.netY > 100.f) g_S.netY = 100.f;
    if (g_S.uiAlpha < 40.f) g_S.uiAlpha = 40.f;
    if (g_S.uiAlpha > 255.f) g_S.uiAlpha = 255.f;
    if (g_S.fov < 10.f) g_S.fov = 10.f;
    if (g_S.fov > 170.f) g_S.fov = 170.f;
    if (g_S.backCamPitch < -89.f) g_S.backCamPitch = -89.f;
    if (g_S.backCamPitch > 89.f) g_S.backCamPitch = 89.f;
    if (g_S.projCamDist < 16.f) g_S.projCamDist = 16.f;
    if (g_S.projCamDist > 500.f) g_S.projCamDist = 500.f;
    g_S.viewmodelFov = clampF(g_S.viewmodelFov, 40.f, 120.f);
    g_S.viewmodelX = clampF(g_S.viewmodelX, -40.f, 40.f);
    g_S.viewmodelY = clampF(g_S.viewmodelY, -40.f, 40.f);
    g_S.viewmodelZ = clampF(g_S.viewmodelZ, -40.f, 40.f);
    g_S.viewmodelRotX = clampF(g_S.viewmodelRotX, -180.f, 180.f);
    g_S.viewmodelRotY = clampF(g_S.viewmodelRotY, -180.f, 180.f);
    g_S.viewmodelRotZ = clampF(g_S.viewmodelRotZ, -180.f, 180.f);
    g_S.fogStart = std::isfinite(g_S.fogStart) ? clampF(g_S.fogStart, 0.f, 10000.f) : 0.f;
    g_S.fogEnd = std::isfinite(g_S.fogEnd) ? clampF(g_S.fogEnd, 1.f, 20000.f) : 3000.f;
    if (g_S.fogEnd <= g_S.fogStart + 1.f) g_S.fogEnd = (std::min)(20000.f, g_S.fogStart + 1.f);
    g_S.fogDensity = std::isfinite(g_S.fogDensity) ? clampF(g_S.fogDensity, 0.f, 1.f) : 1.f;
    g_S.fogR = std::isfinite(g_S.fogR) ? clampF(g_S.fogR, 0.f, 255.f) : 128.f;
    g_S.fogG = std::isfinite(g_S.fogG) ? clampF(g_S.fogG, 0.f, 255.f) : 128.f;
    g_S.fogB = std::isfinite(g_S.fogB) ? clampF(g_S.fogB, 0.f, 255.f) : 128.f;
    if (g_S.speedometerFont.empty()) g_S.speedometerFont = "Tahoma";
    if (g_S.speedometerFont.size() > 63) g_S.speedometerFont.resize(63);
    if (g_S.speedometerFontSize < 10) g_S.speedometerFontSize = 10;
    if (g_S.speedometerFontSize > 36) g_S.speedometerFontSize = 36;
    g_S.speedometerX = clampF(g_S.speedometerX, 0.f, 100.f);
    g_S.speedometerY = clampF(g_S.speedometerY, 0.f, 100.f);
    auto clamp3 = [](float& a, float& b, float& c) {
        a = clampF(a, 0.f, 255.f); b = clampF(b, 0.f, 255.f); c = clampF(c, 0.f, 255.f);
    };
    clamp3(g_S.uiBgR, g_S.uiBgG, g_S.uiBgB);
    clamp3(g_S.uiSideR, g_S.uiSideG, g_S.uiSideB);
    clamp3(g_S.uiTextR, g_S.uiTextG, g_S.uiTextB);
    clamp3(g_S.uiSelR, g_S.uiSelG, g_S.uiSelB);
    clamp3(g_S.uiAccR, g_S.uiAccG, g_S.uiAccB);
    clamp3(g_S.uiHoverR, g_S.uiHoverG, g_S.uiHoverB);
    clamp3(g_S.uiBtnR, g_S.uiBtnG, g_S.uiBtnB);
    clamp3(g_S.uiFieldR, g_S.uiFieldG, g_S.uiFieldB);
    clamp3(g_S.uiCheckR, g_S.uiCheckG, g_S.uiCheckB);
    clamp3(g_S.uiCloseR, g_S.uiCloseG, g_S.uiCloseB);
    clamp3(g_S.hudColorR, g_S.hudColorG, g_S.hudColorB);
}

void resetGuiSettings() {
    g_S.uiBgR = 18; g_S.uiBgG = 18; g_S.uiBgB = 18;
    g_S.uiSideR = 14; g_S.uiSideG = 14; g_S.uiSideB = 14;
    g_S.uiTextR = 230; g_S.uiTextG = 230; g_S.uiTextB = 230;
    g_S.uiSelR = 40; g_S.uiSelG = 40; g_S.uiSelB = 48;
    g_S.uiAccR = 100; g_S.uiAccG = 160; g_S.uiAccB = 255;
    g_S.uiHoverR = 32; g_S.uiHoverG = 32; g_S.uiHoverB = 38;
    g_S.uiBtnR = 36; g_S.uiBtnG = 36; g_S.uiBtnB = 36;
    g_S.uiFieldR = 26; g_S.uiFieldG = 26; g_S.uiFieldB = 26;
    g_S.uiCheckR = 58; g_S.uiCheckG = 58; g_S.uiCheckB = 58;
    g_S.uiCloseR = 200; g_S.uiCloseG = 70; g_S.uiCloseB = 70;
    g_S.uiAlpha = 235.f;
    g_S.uiBlur = true;
    g_S.uiBevel = false;
    g_UiFont = "Tahoma";
    g_Font = 0;
    clampSettings();
}

void resetAllSettings() {
    g_S = Settings{};
    g_WmText = "NOCOM";
    g_WmImgPath.clear();
    g_UiFont = "Tahoma";
    g_Font = 0;
    clampSettings();
}
