#pragma once

void drawMenu(int sw, int sh);
bool isDebugTabActive();
void updateMenuInput(int sw, int sh, bool inGame);
void pollMenuHotkey(bool allowEngineCommands = true);  
void pollBinds();  
void notifyBindingForBindGuard(const char* binding);
void updateMicSpamLifecycle(bool inGame, bool focused);
void resetGameplayBindEdges();
bool shouldBlockGameplayFeatureInput();
void cancelBindCapture();
void forceStopMicSpam();
void setMicSpamEnabled(bool enabled);
void applyGameProcessScheduling();
void applyNetworkSettings(bool force = false);
void restartMicSpamForMapLoad();
void cleanupMenuResources();
int getHookDebugLines(wchar_t lines[][128], int maxLines);

extern int* g_BindPtr;
extern int g_Focus;
extern int g_Mx, g_My;
extern int g_ActiveSlider;

enum {
    FOCUS_NONE = 0,
    FOCUS_CFG = 1,
    FOCUS_WM = 2,
    FOCUS_IMG = 3,
    FOCUS_FONT = 4,
    FOCUS_DEATH_SND = 5
};
