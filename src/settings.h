#pragma once
#include <string>
#include <windows.h>

struct Settings {
    // ESP
    bool esp = false, espBones = false, espBox = false, espName = false, espHealth = false;
    bool espEntities = false;
    bool espItems = false;

    bool waterBombTrajectories = true;
    bool nadeBoostIndicator = true;
    bool prediction = true;

    float espMaxDistance = 4000.f;  
    float espUpdateIntervalMs = 66.f;  
    bool espWires = false;
    bool espWiresPlayers = true;
    bool espWiresNpcs = true;
    bool espWiresItems = true;
    bool espWireColorByType = true;
    float espWireWidth = 1.f;
    float espWireR = 120.f, espWireG = 200.f, espWireB = 255.f;

    float espWireHostileR = 255.f, espWireHostileG = 60.f, espWireHostileB = 60.f;
    float espWireFriendlyR = 60.f, espWireFriendlyG = 220.f, espWireFriendlyB = 90.f;
    float espWireItemR = 120.f, espWireItemG = 200.f, espWireItemB = 255.f;

    bool espPlayers = false, espNpcs = false;
    // Aim
    bool aim = false, aimVisible = false, aimPlayers = false, aimNpcs = false;
    int aimBone = 0; /* legacy */
    int aimBones = 1; /* Bits: head, chest, stomach, pelvis, center. */
    float aimFov = 25.f;
    float aimSmooth = 4.f;

    unsigned aimExcludeMask = 0x0001FFFFu;  

    int aimNpcExclusion = -1;
    int aimKey = VK_XBUTTON1;

    bool viewmodelSway = false;
    float viewmodelSwayAmount = 1.f;  
    bool autoTrigger = false;
    float autoTriggerFov = 3.f;

    bool fovOn = false;
    float fov = 110.f;

    bool backCam = false;  
    float backCamPitch = 0.f;
    int kBackCam = 0;
    bool projCam = false;
    float projCamDist = 120.f;

    bool viewmodelOn = false;
    bool viewmodelFovOn = false;
    float viewmodelFov = 54.f;
    float viewmodelX = 0.f, viewmodelY = 0.f, viewmodelZ = 0.f;
    float viewmodelRotX = 0.f, viewmodelRotY = 0.f, viewmodelRotZ = 0.f;  

    bool bhop = false, autoStrafe = false;

    bool autoStrafeBidirectional = true;
    float autoStrafeTurnScale = 0.5f;
    float autoStrafeMaxDelta = 180.f;
    float bhopChance = 100.f;
    bool jumpbug = false;
    int jbKey = 0;
    bool edgebug = false;
    int kEdgebug = 0;
    float edgebugTicks = 0.f;
    bool edgebugIndicator = false;
    float edgebugX = 50.f, edgebugY = 36.f;
    bool edgejump = false;
    int kEdgejump = 0;
    float edgejumpMinSpeed = 80.f;
    float edgejumpLookaheadTicks = 4.f;

    bool ladderSpeed = false;  
    int kLadderSpeed = VK_TAB;
    float ladderSpeedDelayMs = 10.f;

    bool fullBright = false;
    int kFullBright = 0;

    bool svCheatsOverride = false;

    bool svPropJumpOverride = false;

    bool drawClipBrushes = false;
    int kDrawClipBrushes = 0;  

    bool customFog = false;
    float fogStart = 0.f, fogEnd = 3000.f, fogDensity = 1.f;
    float fogR = 128.f, fogG = 128.f, fogB = 128.f;

    bool hudColorMod = true;
    float hudColorR = 72.f, hudColorG = 145.f, hudColorB = 90.f;

    bool watermark = false;
    bool wmBgOn = false;
    float wmX = 98.f, wmY = 1.5f, wmPad = 8.f, wmScale = 28.f, wmAlpha = 170.f;
    float wmTextR = 232.f, wmTextG = 232.f, wmTextB = 232.f;
    float wmBgR = 12.f, wmBgG = 12.f, wmBgB = 12.f;

    bool wmImg = false;
    bool wmImgDvd = false;
    float wmImgX = 98.f, wmImgY = 8.f, wmImgScale = 100.f, wmImgAlpha = 255.f;
    float wmImgDvdSpeed = 100.f;  

    float uiBgR = 14.f, uiBgG = 14.f, uiBgB = 14.f;
    float uiSideR = 20.f, uiSideG = 20.f, uiSideB = 20.f;
    float uiTextR = 230.f, uiTextG = 230.f, uiTextB = 230.f;
    float uiSelR = 46.f, uiSelG = 46.f, uiSelB = 46.f;
    float uiAccR = 230.f, uiAccG = 230.f, uiAccB = 230.f;
    float uiHoverR = 36.f, uiHoverG = 36.f, uiHoverB = 36.f;
    float uiBtnR = 36.f, uiBtnG = 36.f, uiBtnB = 36.f;
    float uiFieldR = 26.f, uiFieldG = 26.f, uiFieldB = 26.f;
    float uiCheckR = 58.f, uiCheckG = 58.f, uiCheckB = 58.f;
    float uiCloseR = 200.f, uiCloseG = 70.f, uiCloseB = 70.f;
    float uiAlpha = 235.f;
    bool uiBlur = true;
    bool uiBevel = false;
    bool debugMode = false;

    bool processHighPriority = false;
    bool processAboveNormal = false;
    bool processAffinityFastCores = false;
    bool processDisablePowerThrottle = false;
    bool processHighResTimer = false;       
    bool processMainThreadBoost = false;    
    bool processIdleWhenUnfocused = false;  
    bool engineThreadedBones = false;       
    bool engineThreadedParticles = false;   
    bool engineMatQueueMode = false;        
    bool engineMultiCore = false;           

    bool networkSettingsEnabled = false;
    int networkPreset = 2;  
    float networkRate = 196608.f;
    float networkCmdRate = 100.f;
    float networkUpdateRate = 100.f;
    float networkInterp = 0.f;
    float networkInterpRatio = 1.f;
    float networkTimeout = 60.f;
    bool networkPredict = true;
    bool networkPredictWeapons = true;
    bool networkLagCompensation = true;
    bool networkInterpolate = true;
    bool networkSmooth = true;
    float networkSmoothTime = 0.1f;
    float networkInterpNpcs = 0.f;
    float networkPredOptimize = 2.f;

    int networkQosMode = 2;  
    bool networkCompressPackets = true;
    float networkCompressMinSize = 1024.f;
    bool networkQueuedPacketThread = true;
    float networkSplitRate = 2.f;
    float networkSplitPacketMaxRate = 80000.f;
    float networkMaxRoutable = 1200.f;
    float networkMaxFragments = 1200.f;
    float networkMaxClearTime = 0.5f;
    float networkMaxPacketDrop = 100.f;
    bool networkNetGraph = false;
    int networkNetGraphPos = 1;
    bool networkNetGraphProportionalFont = true;
    float aimScanIntervalMs = 64.f;         
    float trajectoryBudget = 2.f;           

    int menuKey = VK_INSERT;

    bool netStats = false;

    bool micSpam = false;
    int kMicSpam = 0;

    bool voiceLoopback = false;

    bool deathSound = false;
    float deathSoundVolume = 70.f;
    std::string deathSoundPath = "buttons/button14";

    bool discordRpc = false;
    bool discordShowServer = false;
    bool speedometer = false;
    bool speedometerShowBest = false;
    bool speedometerBackground = false;
    int speedometerFontSize = 18;
    float speedometerX = 50.f, speedometerY = 92.f;  
    std::string speedometerFont = "Tahoma";
    float netX = 1.f, netY = 92.f;  

    int kEsp = 0;
    int kAim = 0, kAimVisible = 0, kAimPlayers = 0, kAimNpcs = 0;
    int kAutoStrafe = 0, kJumpbug = 0, kNetStats = 0;
};

extern Settings g_S;
extern std::string g_WmText;
extern std::string g_WmImgPath;
extern std::string g_CfgName;
extern std::string g_NameEdit;
extern std::string g_UiFont;

void clampSettings();
void resetGuiSettings();
void resetAllSettings();
