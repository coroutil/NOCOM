#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "menu.h"
#include "game.h"
#include "features.h"
#include "settings.h"
#include "config.h"
#include "render.h"
#include "discord.h"
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>
#include <cmath>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winmm.lib")
#include <mmsystem.h>
#ifndef FR_PRIVATE
#define FR_PRIVATE 0x10
#endif

void applyGameProcessScheduling() {
    HANDLE proc = GetCurrentProcess();
    HWND fg = GetForegroundWindow();
    DWORD fgPid = 0;
    if (fg) GetWindowThreadProcessId(fg, &fgPid);
    const bool focused = (fgPid == GetCurrentProcessId());

    if (g_S.processIdleWhenUnfocused && !focused) {
        SetPriorityClass(proc, BELOW_NORMAL_PRIORITY_CLASS);
    } else if (g_S.processHighPriority) {
        SetPriorityClass(proc, HIGH_PRIORITY_CLASS);
    } else if (g_S.processAboveNormal) {
        SetPriorityClass(proc, ABOVE_NORMAL_PRIORITY_CLASS);
    } else {
        SetPriorityClass(proc, NORMAL_PRIORITY_CLASS);
    }

    if (g_S.processMainThreadBoost) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    } else {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    }

    DWORD_PTR processMask = 0, systemMask = 0;
    if (GetProcessAffinityMask(proc, &processMask, &systemMask) && systemMask) {
        if (!g_S.processAffinityFastCores) {
            SetProcessAffinityMask(proc, systemMask);
        } else {
            int bits = 0;
            for (DWORD_PTR m = systemMask; m; m >>= 1)
                if (m & 1) ++bits;
            DWORD_PTR selected = systemMask;
            if (bits > 2) {
                int keep = bits / 2;
                selected = 0;
                for (int i = static_cast<int>(sizeof(DWORD_PTR) * 8) - 1; i >= 0 && keep > 0; --i) {
                    const DWORD_PTR bit = (static_cast<DWORD_PTR>(1) << i);
                    if (systemMask & bit) {
                        selected |= bit;
                        --keep;
                    }
                }
                if (!selected) selected = systemMask;
            }
            SetProcessAffinityMask(proc, selected);
        }
    }

    if (g_S.processDisablePowerThrottle) {
        struct {
            ULONG Version;
            ULONG ControlMask;
            ULONG StateMask;
        } throttle{};
        throttle.Version = 1;
        throttle.ControlMask = 0x1;
        throttle.StateMask = 0;
        using SetProcessInformationFn = BOOL (WINAPI*)(HANDLE, int, PVOID, DWORD);
        static SetProcessInformationFn s_setInfo = reinterpret_cast<SetProcessInformationFn>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation"));
        if (s_setInfo)
            s_setInfo(proc, 4, &throttle, sizeof(throttle));
    }

    static bool s_timerOn = false;
    if (g_S.processHighResTimer && !s_timerOn) {
        timeBeginPeriod(1);
        s_timerOn = true;
    } else if (!g_S.processHighResTimer && s_timerOn) {
        timeEndPeriod(1);
        s_timerOn = false;
    }

    static bool s_lastBones = false, s_lastParts = false, s_lastQueue = false, s_lastMulti = false;
    if (g_S.engineThreadedBones != s_lastBones) {
        s_lastBones = g_S.engineThreadedBones;
        setClientConVarStringDirect("cl_threaded_bone_setup", g_S.engineThreadedBones ? "1" : "0", true);
    }
    if (g_S.engineThreadedParticles != s_lastParts) {
        s_lastParts = g_S.engineThreadedParticles;
        setClientConVarStringDirect("r_threaded_particles", g_S.engineThreadedParticles ? "1" : "0", true);
    }
    if (g_S.engineMatQueueMode != s_lastQueue) {
        s_lastQueue = g_S.engineMatQueueMode;
        setClientConVarStringDirect("mat_queue_mode", g_S.engineMatQueueMode ? "2" : "-1", true);
    }
    if (g_S.engineMultiCore != s_lastMulti) {
        s_lastMulti = g_S.engineMultiCore;
        if (g_S.engineMultiCore)
            setClientConVarStringDirect("mat_queue_mode", "2", true);
    }
}

namespace {
static void applyNetworkQosPresetToSettings(int mode) {
    switch (mode) {
    case 1:  
        g_S.networkCompressPackets = false;
        g_S.networkCompressMinSize = 1024.f;
        g_S.networkQueuedPacketThread = true;
        g_S.networkSplitRate = 4.f;
        g_S.networkSplitPacketMaxRate = 200000.f;
        g_S.networkMaxClearTime = 0.015f;
        g_S.networkMaxPacketDrop = 0.f;
        break;
    case 2:  
        g_S.networkCompressPackets = true;
        g_S.networkCompressMinSize = 1024.f;
        g_S.networkQueuedPacketThread = true;
        g_S.networkSplitRate = 2.f;
        g_S.networkSplitPacketMaxRate = 80000.f;
        g_S.networkMaxClearTime = 0.5f;
        g_S.networkMaxPacketDrop = 100.f;
        break;
    case 3:  
        g_S.networkCompressPackets = true;
        g_S.networkCompressMinSize = 256.f;
        g_S.networkQueuedPacketThread = true;
        g_S.networkSplitRate = 4.f;
        g_S.networkSplitPacketMaxRate = 100000.f;
        g_S.networkMaxClearTime = 0.5f;
        g_S.networkMaxPacketDrop = 400.f;
        break;
    default:  
        break;
    }
}

static void applyNetworkPresetToSettings(int profile) {
    switch (profile) {
    case 1:  
        g_S.networkRate = 786432.f;
        g_S.networkCmdRate = 100.f;
        g_S.networkUpdateRate = 100.f;
        g_S.networkInterp = 0.f;
        g_S.networkInterpRatio = 1.f;
        g_S.networkTimeout = 60.f;
        g_S.networkPredict = true;
        g_S.networkPredictWeapons = true;
        g_S.networkLagCompensation = true;
        g_S.networkInterpolate = true;
        g_S.networkSmooth = false;
        g_S.networkSmoothTime = 0.01f;
        g_S.networkInterpNpcs = 0.f;
        g_S.networkPredOptimize = 2.f;
        g_S.networkQosMode = 1;
        applyNetworkQosPresetToSettings(g_S.networkQosMode);
        break;
    case 2:  
        g_S.networkRate = 196608.f;
        g_S.networkCmdRate = 100.f;
        g_S.networkUpdateRate = 100.f;
        g_S.networkInterp = 0.f;
        g_S.networkInterpRatio = 1.f;
        g_S.networkTimeout = 60.f;
        g_S.networkPredict = true;
        g_S.networkPredictWeapons = true;
        g_S.networkLagCompensation = true;
        g_S.networkInterpolate = true;
        g_S.networkSmooth = true;
        g_S.networkSmoothTime = 0.1f;
        g_S.networkInterpNpcs = 0.f;
        g_S.networkPredOptimize = 2.f;
        g_S.networkQosMode = 2;
        applyNetworkQosPresetToSettings(g_S.networkQosMode);
        break;
    case 3:  
        g_S.networkRate = 128000.f;
        g_S.networkCmdRate = 40.f;
        g_S.networkUpdateRate = 40.f;
        g_S.networkInterp = 0.f;
        g_S.networkInterpRatio = 2.f;
        g_S.networkTimeout = 90.f;
        g_S.networkPredict = true;
        g_S.networkPredictWeapons = true;
        g_S.networkLagCompensation = true;
        g_S.networkInterpolate = true;
        g_S.networkSmooth = true;
        g_S.networkSmoothTime = 0.1f;
        g_S.networkInterpNpcs = 0.1f;
        g_S.networkPredOptimize = 2.f;
        g_S.networkQosMode = 3;
        applyNetworkQosPresetToSettings(g_S.networkQosMode);
        break;
    case 4:  
        g_S.networkRate = 786432.f;
        g_S.networkCmdRate = 100.f;
        g_S.networkUpdateRate = 100.f;
        g_S.networkInterp = 0.f;
        g_S.networkInterpRatio = 1.f;
        g_S.networkTimeout = 60.f;
        g_S.networkPredict = true;
        g_S.networkPredictWeapons = true;
        g_S.networkLagCompensation = true;
        g_S.networkInterpolate = true;
        g_S.networkSmooth = false;
        g_S.networkSmoothTime = 0.01f;
        g_S.networkInterpNpcs = 0.f;
        g_S.networkPredOptimize = 2.f;
        g_S.networkQosMode = 1;
        applyNetworkQosPresetToSettings(g_S.networkQosMode);
        break;
    default:
        break;
    }
}

static void formatNetworkNumber(char* out, size_t outSize, float value, bool integer = false) {
    if (!out || outSize == 0) return;
    if (!std::isfinite(value)) value = 0.f;
    if (integer) snprintf(out, outSize, "%.0f", static_cast<double>(value));
    else snprintf(out, outSize, "%g", static_cast<double>(value));
}

struct NetworkCvarOverride {
    const char* name;
    char original[128];
    char applied[128];
    bool captured;
    bool wrote;
};

static NetworkCvarOverride g_NetworkCvars[] = {
    {"rate", {}, {}, false, false},
    {"cl_cmdrate", {}, {}, false, false},
    {"cl_updaterate", {}, {}, false, false},
    {"cl_interp", {}, {}, false, false},
    {"cl_interp_ratio", {}, {}, false, false},
    {"cl_timeout", {}, {}, false, false},
    {"cl_predict", {}, {}, false, false},
    {"cl_predictweapons", {}, {}, false, false},
    {"cl_lagcompensation", {}, {}, false, false},
    {"cl_interpolate", {}, {}, false, false},
    {"cl_smooth", {}, {}, false, false},
    {"cl_smoothtime", {}, {}, false, false},
    {"cl_interp_npcs", {}, {}, false, false},
    {"cl_pred_optimize", {}, {}, false, false},
    {"net_compresspackets", {}, {}, false, false},
    {"net_compresspackets_minsize", {}, {}, false, false},
    {"net_queued_packet_thread", {}, {}, false, false},
    {"net_splitrate", {}, {}, false, false},
    {"net_splitpacket_maxrate", {}, {}, false, false},
    {"net_maxroutable", {}, {}, false, false},
    {"net_maxfragments", {}, {}, false, false},
    {"net_maxcleartime", {}, {}, false, false},
    {"net_maxpacketdrop", {}, {}, false, false},
    {"net_graph", {}, {}, false, false},
    {"net_graphpos", {}, {}, false, false},
    {"net_graphproportionalfont", {}, {}, false, false}
};

static bool networkCvarValuesEqual(const char* a, const char* b) {
    if (!a || !b) return false;
    if (strcmp(a, b) == 0) return true;

    char* endA = nullptr;
    char* endB = nullptr;
    const double valueA = strtod(a, &endA);
    const double valueB = strtod(b, &endB);
    while (endA && *endA && isspace(static_cast<unsigned char>(*endA))) ++endA;
    while (endB && *endB && isspace(static_cast<unsigned char>(*endB))) ++endB;
    if (endA == a || endB == b || !endA || !endB || *endA || *endB ||
        !std::isfinite(valueA) || !std::isfinite(valueB)) return false;
    const double scale = (std::max)(1.0, (std::max)(fabs(valueA), fabs(valueB)));
    return fabs(valueA - valueB) <= 0.000001 * scale;
}

static void restoreNetworkCvar(NetworkCvarOverride& state) {
    if (!state.captured) return;
    if (state.wrote) {
        char current[128]{};

        if (getConVarStringDirect(state.name, current, sizeof(current)) &&
            networkCvarValuesEqual(current, state.applied)) {
            if (!setClientConVarStringDirect(state.name, state.original, false))
                return;  
        }
    }
    state.original[0] = '\0';
    state.applied[0] = '\0';
    state.captured = false;
    state.wrote = false;
}
}  

void applyNetworkSettings(bool force) {
    static DWORD s_lastApply = 0;
    const DWORD now = GetTickCount();
    if (!force && s_lastApply && now - s_lastApply < 250u) return;
    s_lastApply = now;

    if (!g_S.networkSettingsEnabled) {
        for (auto& state : g_NetworkCvars) restoreNetworkCvar(state);
        return;
    }

    char numberValues[26][32]{};
    formatNetworkNumber(numberValues[0], sizeof(numberValues[0]), g_S.networkRate, true);
    formatNetworkNumber(numberValues[1], sizeof(numberValues[1]), g_S.networkCmdRate, true);
    formatNetworkNumber(numberValues[2], sizeof(numberValues[2]), g_S.networkUpdateRate, true);
    formatNetworkNumber(numberValues[3], sizeof(numberValues[3]), g_S.networkInterp);
    formatNetworkNumber(numberValues[4], sizeof(numberValues[4]), g_S.networkInterpRatio);
    formatNetworkNumber(numberValues[5], sizeof(numberValues[5]), g_S.networkTimeout);
    strcpy_s(numberValues[6], g_S.networkPredict ? "1" : "0");
    strcpy_s(numberValues[7], g_S.networkPredictWeapons ? "1" : "0");
    strcpy_s(numberValues[8], g_S.networkLagCompensation ? "1" : "0");
    strcpy_s(numberValues[9], g_S.networkInterpolate ? "1" : "0");
    strcpy_s(numberValues[10], g_S.networkSmooth ? "1" : "0");
    formatNetworkNumber(numberValues[11], sizeof(numberValues[11]), g_S.networkSmoothTime);
    formatNetworkNumber(numberValues[12], sizeof(numberValues[12]), g_S.networkInterpNpcs);
    formatNetworkNumber(numberValues[13], sizeof(numberValues[13]), g_S.networkPredOptimize, true);
    strcpy_s(numberValues[14], g_S.networkCompressPackets ? "1" : "0");
    formatNetworkNumber(numberValues[15], sizeof(numberValues[15]), g_S.networkCompressMinSize, true);
    strcpy_s(numberValues[16], g_S.networkQueuedPacketThread ? "1" : "0");
    formatNetworkNumber(numberValues[17], sizeof(numberValues[17]), g_S.networkSplitRate, true);
    formatNetworkNumber(numberValues[18], sizeof(numberValues[18]), g_S.networkSplitPacketMaxRate, true);
    formatNetworkNumber(numberValues[19], sizeof(numberValues[19]), g_S.networkMaxRoutable, true);
    formatNetworkNumber(numberValues[20], sizeof(numberValues[20]), g_S.networkMaxFragments, true);
    formatNetworkNumber(numberValues[21], sizeof(numberValues[21]), g_S.networkMaxClearTime);
    formatNetworkNumber(numberValues[22], sizeof(numberValues[22]), g_S.networkMaxPacketDrop, true);
    strcpy_s(numberValues[23], g_S.networkNetGraph ? "1" : "0");
    formatNetworkNumber(numberValues[24], sizeof(numberValues[24]), static_cast<float>(g_S.networkNetGraphPos), true);
    strcpy_s(numberValues[25], g_S.networkNetGraphProportionalFont ? "1" : "0");

    for (size_t i = 0; i < sizeof(g_NetworkCvars) / sizeof(g_NetworkCvars[0]); ++i) {
        NetworkCvarOverride& state = g_NetworkCvars[i];
        const char* desired = numberValues[i];
        char current[128]{};
        if (!state.captured) {
            if (!getConVarStringDirect(state.name, current, sizeof(current))) continue;
            strncpy_s(state.original, sizeof(state.original), current, _TRUNCATE);
            state.captured = true;
        } else if (!getConVarStringDirect(state.name, current, sizeof(current))) {
            continue;
        }

        if (networkCvarValuesEqual(current, desired)) {
            if (state.wrote) strncpy_s(state.applied, sizeof(state.applied), current, _TRUNCATE);
            continue;
        }

        setClientConVarStringDirect(state.name, desired, false);
        char after[128]{};
        if (getConVarStringDirect(state.name, after, sizeof(after)) &&
            networkCvarValuesEqual(after, desired)) {
            strncpy_s(state.applied, sizeof(state.applied), after, _TRUNCATE);
            state.wrote = true;
        }
    }
}

struct {
    int x = 80, y = 70;
    int tab = 0;
    int sub[7] = {};
    bool drag = false;
    int dx = 0, dy = 0;
} g_Menu;

bool isDebugTabActive() {
    return g_MenuOpen && g_Menu.tab == 6;
}

int g_Mx, g_My;
bool g_Down, g_Click;
constexpr int kMenuW = 440, kMenuH = 450, kSide = 92, kRowW = 320;

namespace {
static HWND g_InputSubclassWindow = nullptr;
static WNDPROC g_InputOriginalProc = nullptr;
static volatile LONG g_XButton1Event = 0;
static volatile LONG g_XButton2Event = 0;

static volatile LONG g_LastMessageModeCommandTick = 0;
static volatile LONG g_WindowFocusEpoch = 0;
static volatile LONG g_MicFocusLostSignal = 0;
static bool g_MicVoiceActive = false;
static bool g_MicSpamArmedThisSession = false;
static bool g_MicSpamWasForeground = false;
static bool g_MicRestartPending = false;
static DWORD g_MicRestartDue = 0;
static bool g_MicStopPending = false;
static DWORD g_MicStopDue = 0;
static bool g_MicStoppedOutOfGame = false;
static uint8_t g_BindPrev[256]{};
static bool g_BindPrevInitialized = false;

static bool isRecentTick(volatile LONG* tick, DWORD intervalMs) {
    const LONG last = InterlockedCompareExchange(tick, 0, 0);
    return last != 0 && static_cast<DWORD>(GetTickCount() - static_cast<DWORD>(last)) < intervalMs;
}

static void recordFocusTransition(bool lostFocus) {
    InterlockedIncrement(&g_WindowFocusEpoch);
    if (lostFocus) InterlockedExchange(&g_MicFocusLostSignal, 1);
    InterlockedExchange(&g_XButton1Event, 0);
    InterlockedExchange(&g_XButton2Event, 0);
}

static LRESULT CALLBACK nocomInputWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const bool blockEngineInput = g_MenuOpen || g_BindPtr != nullptr;

    if (message == WM_KILLFOCUS ||
        (message == WM_ACTIVATEAPP && wParam == FALSE) ||
        (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE)) {
        recordFocusTransition(true);
    } else if (message == WM_SETFOCUS ||
               (message == WM_ACTIVATEAPP && wParam != FALSE) ||
               (message == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE)) {
        recordFocusTransition(false);
    }

    if (message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK || message == WM_XBUTTONUP) {
        if (message != WM_XBUTTONUP) {
            const WORD button = HIWORD(wParam);
            if (button == XBUTTON1) InterlockedExchange(&g_XButton1Event, 1);
            else if (button == XBUTTON2) InterlockedExchange(&g_XButton2Event, 1);
        }

        if (blockEngineInput) return TRUE;
    }

    if (message == WM_INPUT) {

        RAWINPUT raw{};
        UINT bytes = sizeof(raw);
        const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
                                          &raw, &bytes, sizeof(RAWINPUTHEADER));
        if (read != static_cast<UINT>(-1) && read >= sizeof(RAWINPUTHEADER) && raw.header.dwType == RIM_TYPEMOUSE) {
            const USHORT flags = raw.data.mouse.usButtonFlags;
            if (flags & RI_MOUSE_BUTTON_4_DOWN) InterlockedExchange(&g_XButton1Event, 1);
            if (flags & RI_MOUSE_BUTTON_5_DOWN) InterlockedExchange(&g_XButton2Event, 1);
        }

        if (blockEngineInput) return DefWindowProcA(hwnd, message, wParam, lParam);
    }

    if (blockEngineInput && (message == WM_KEYDOWN || message == WM_KEYUP ||
        message == WM_CHAR || message == WM_DEADCHAR ||
        message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
        message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ||
        message == WM_MBUTTONDOWN || message == WM_MBUTTONUP ||
        message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)) {

        return 0;
    }

    WNDPROC original = g_InputOriginalProc;
    return original ? CallWindowProcA(original, hwnd, message, wParam, lParam)
                    : DefWindowProcA(hwnd, message, wParam, lParam);
}

static void installMenuInputHook(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || hwnd == g_InputSubclassWindow) return;
    if (g_InputSubclassWindow && g_InputOriginalProc && IsWindow(g_InputSubclassWindow) &&
        reinterpret_cast<WNDPROC>(GetWindowLongPtrA(g_InputSubclassWindow, GWLP_WNDPROC)) == nocomInputWndProc) {
        SetWindowLongPtrA(g_InputSubclassWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_InputOriginalProc));
    }
    g_InputSubclassWindow = nullptr;
    g_InputOriginalProc = nullptr;
    SetLastError(0);
    LONG_PTR previous = SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(nocomInputWndProc));
    if (previous || GetLastError() == 0) {
        g_InputSubclassWindow = hwnd;
        g_InputOriginalProc = reinterpret_cast<WNDPROC>(previous);
    }
}

static bool consumeExtraMouseButton(int vk) {
    if (vk == VK_XBUTTON1) return InterlockedExchange(&g_XButton1Event, 0) != 0;
    if (vk == VK_XBUTTON2) return InterlockedExchange(&g_XButton2Event, 0) != 0;
    return false;
}

static void clearExtraMouseButtonEvents() {
    InterlockedExchange(&g_XButton1Event, 0);
    InterlockedExchange(&g_XButton2Event, 0);
}

static HWND resolveGameWindow() {
    HWND game = FindWindowA("Valve001", nullptr);
    if (game) {
        DWORD pid = 0;
        GetWindowThreadProcessId(game, &pid);
        if (pid == GetCurrentProcessId()) return game;
    }
    HWND fg = GetForegroundWindow();
    if (!fg) return nullptr;
    HWND root = GetAncestor(fg, GA_ROOT);
    if (!root) return nullptr;
    char className[64]{};
    if (GetClassNameA(root, className, static_cast<int>(sizeof(className))) <= 0 || _stricmp(className, "Valve001") != 0)
        return nullptr;
    DWORD pid = 0;
    GetWindowThreadProcessId(root, &pid);
    return pid == GetCurrentProcessId() ? root : nullptr;
}

static bool isGameWindowForeground() {
    HWND game = resolveGameWindow();
    HWND fg = GetForegroundWindow();
    if (!game || !fg) return false;
    return fg == game || IsChild(game, fg) || GetAncestor(fg, GA_ROOT) == game;
}

static void syncBindEdgesToCurrent() {
    for (int k = 1; k < 256; ++k) {
        if (k == VK_XBUTTON1 || k == VK_XBUTTON2) continue;
        g_BindPrev[k] = (GetAsyncKeyState(k) & 0x8000) ? 1 : 0;
    }
    g_BindPrevInitialized = true;
    clearExtraMouseButtonEvents();
}

static bool containsTextNoCase(const char* text, const char* token) {
    if (!text || !token || !*token) return false;
    const size_t tokenLength = strlen(token);
    for (const char* p = text; *p; ++p) {
        if (_strnicmp(p, token, tokenLength) == 0) return true;
    }
    return false;
}

static bool isExplicitTextInputName(const char* text) {
    static const char* const tokens[] = {
        "ChatInput", "ChatInputLine", "MessageMode", "MessageModePanel",
        "messagemode", "messagemode2", "SayTextEntry", "HudChatInput",
        "ChatEntry", "ChatEntryPanel"
    };
    for (const char* token : tokens) {
        if (containsTextNoCase(text, token)) return true;
    }
    return false;
}

static bool isGenericTextInputName(const char* text) {
    return containsTextNoCase(text, "TextEntry") ||
           containsTextNoCase(text, "CTextEntry") ||
           containsTextNoCase(text, "InputLine") ||
           containsTextNoCase(text, "TextInput") ||
           containsTextNoCase(text, "TextField") ||
           containsTextNoCase(text, "TextBox") ||
           containsTextNoCase(text, "Autocomplete");
}

}  

static bool dispatchMicSpamCommand(const char* command) {
    if (!g_Engine || !command) return false;
    __try {

        VCall<void, const char*>(g_Engine, 7, command);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void setMicSpamEnabled(bool enabled) {
    g_S.micSpam = enabled;
    if (!enabled) {

        dispatchMicSpamCommand("-voicerecord");
        g_MicVoiceActive = false;
        g_MicSpamArmedThisSession = false;
        g_MicSpamWasForeground = false;
        g_MicRestartPending = false;
        g_MicStopPending = false;
        g_MicStoppedOutOfGame = false;
        return;
    }

    g_MicSpamArmedThisSession = true;
    g_MicStopPending = false;
    g_MicStoppedOutOfGame = false;
    if (InGame() && isGameWindowForeground()) {
        g_MicVoiceActive = dispatchMicSpamCommand("-voicerecord;+voicerecord");
        g_MicSpamWasForeground = true;
        g_MicRestartPending = !g_MicVoiceActive;
        g_MicRestartDue = GetTickCount() + 500u;
    } else {

        g_MicVoiceActive = false;
        g_MicSpamWasForeground = false;
        g_MicRestartPending = true;
        g_MicRestartDue = GetTickCount() + 1000u;
    }
}

void restartMicSpamForMapLoad() {
    if (!g_S.micSpam || !g_MicSpamArmedThisSession) return;

    g_MicVoiceActive = false;
    g_MicRestartPending = true;
    g_MicRestartDue = GetTickCount() + 1500u;
    g_MicStopPending = false;
    g_MicStoppedOutOfGame = false;
}

void forceStopMicSpam() {
    if (g_MicVoiceActive || g_S.micSpam || g_MicRestartPending)
        dispatchMicSpamCommand("-voicerecord");
    g_MicVoiceActive = false;
    g_MicRestartPending = false;
    g_MicStopPending = false;
    g_MicStoppedOutOfGame = false;
}

void updateMicSpamLifecycle(bool inGame, bool focused) {
    const DWORD now = GetTickCount();
    if (InterlockedExchange(&g_MicFocusLostSignal, 0) != 0)
        g_MicSpamWasForeground = false;

    if (!g_S.micSpam) {
        forceStopMicSpam();
        g_MicSpamArmedThisSession = false;
        g_MicSpamWasForeground = focused;
        return;
    }

    if (!g_MicSpamArmedThisSession) return;

    if (!inGame) {
        g_MicSpamWasForeground = false;
        g_MicRestartPending = false;
        if (!g_MicStoppedOutOfGame && !g_MicStopPending) {
            g_MicStopPending = true;
            g_MicStopDue = now + 1200u;
        }
        if (g_MicStopPending && static_cast<LONG>(now - g_MicStopDue) >= 0) {
            dispatchMicSpamCommand("-voicerecord");
            g_MicVoiceActive = false;
            g_MicStopPending = false;
            g_MicStoppedOutOfGame = true;
        }
        return;
    }

    g_MicStopPending = false;
    g_MicStoppedOutOfGame = false;

    if (!focused) {

        g_MicSpamWasForeground = false;
        return;
    }

    if (!g_MicSpamWasForeground) {
        g_MicSpamWasForeground = true;
        if (!g_MicRestartPending) {
            g_MicVoiceActive = false;
            g_MicRestartPending = true;
            g_MicRestartDue = now + 300u;
        }
    } else if (!g_MicVoiceActive && !g_MicRestartPending) {
        g_MicRestartPending = true;
        g_MicRestartDue = now + 500u;
    }

    if (g_MicRestartPending && static_cast<LONG>(now - g_MicRestartDue) >= 0) {
        const bool started = dispatchMicSpamCommand("-voicerecord;+voicerecord");
        g_MicVoiceActive = started;
        g_MicRestartPending = !started;
        g_MicRestartDue = now + 1000u;
    }
}

void notifyBindingForBindGuard(const char* binding) {
    if (!binding || !*binding) return;
    const bool messageModeBinding =
        _stricmp(binding, "messagemode") == 0 ||
        _stricmp(binding, "messagemode2") == 0 ||
        _stricmp(binding, "messagemode3") == 0 ||
        _stricmp(binding, "say") == 0 ||
        _stricmp(binding, "say_team") == 0 ||
        _stricmp(binding, "say_party") == 0;
    if (messageModeBinding)
        InterlockedExchange(&g_LastMessageModeCommandTick, static_cast<LONG>(GetTickCount()));
}

static void copyPanelTextGuarded(const char* source, char* destination, size_t capacity) {
    if (!destination || capacity == 0) return;
    destination[0] = '\0';
    if (!source) return;
    __try {
        size_t i = 0;
        for (; i + 1 < capacity; ++i) {
            const char ch = source[i];
            destination[i] = ch;
            if (ch == '\0') return;
        }
        destination[capacity - 1] = '\0';
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        destination[0] = '\0';
    }
}

static void* getVGuiInputInterface() {

    return g_VGuiInput;
}

static bool isVGuiTextEntryFocused() {
    void* input = getVGuiInputInterface();
    if (!input || !g_Panel) {

        return isRecentTick(&g_LastMessageModeCommandTick, 180u);
    }

    unsigned focusedPanel = 0;
    __try {
        // Keep the full instance vtable; the engine calls entries by slot

        focusedPanel = VCall<unsigned>(input, 4);
        if (!focusedPanel) focusedPanel = VCall<unsigned>(input, 5);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        focusedPanel = 0;
    }

    for (int depth = 0; focusedPanel && depth < 16; ++depth) {
        char panelName[96]{};
        char panelClass[96]{};
        __try {
            const char* rawName = VCall<const char*, unsigned>(g_Panel, 36, focusedPanel);
            const char* rawClass = VCall<const char*, unsigned>(g_Panel, 37, focusedPanel);
            copyPanelTextGuarded(rawName, panelName, sizeof(panelName));
            copyPanelTextGuarded(rawClass, panelClass, sizeof(panelClass));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            panelName[0] = panelClass[0] = '\0';
        }

        if (isExplicitTextInputName(panelName) || isExplicitTextInputName(panelClass) ||
            isGenericTextInputName(panelName) || isGenericTextInputName(panelClass))
            return true;

        unsigned parent = 0;
        __try {
            // Keep the full instance vtable; the engine calls entries by slot
            parent = VCall<unsigned, unsigned>(g_Panel, 20, focusedPanel);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            parent = 0;
        }
        if (parent == focusedPanel) break;
        focusedPanel = parent;
    }

    return isRecentTick(&g_LastMessageModeCommandTick, 180u);
}

void resetGameplayBindEdges() {
    syncBindEdgesToCurrent();
}

bool shouldBlockGameplayFeatureInput() {
    return !isGameWindowForeground() || isVGuiTextEntryFocused();
}

int g_Focus = FOCUS_NONE;
static bool g_FontDrop = false;
static int g_FontDropOwner = 0;  
static DWORD g_FontRepeatAt = 0;
static int g_FontRepeatDirection = 0;
static std::vector<std::string> g_FontList;
static std::vector<std::string> g_PrivateFontFiles;
static int g_FontScroll = 0;
static int g_FontAnchorX = 0, g_FontAnchorY = 0;
static int g_FontAnchorW = 0;
static int g_MenuScreenH = 0;
static int g_WatermarkPage = 0;
static bool g_SoundDrop = false;
static int g_SoundDropScroll = 0;
static char g_SoundFilter[64] = {};
static std::string g_SoundFilterEdit;
static std::vector<int> g_SoundFilteredIdx;
static int g_SoundAnchorX = 0, g_SoundAnchorY = 0, g_SoundAnchorW = 0;
static DWORD g_SoundRepeatAt = 0;
static int g_SoundRepeatDirection = 0;
static bool g_AimNpcDrop = false;
static int g_AimNpcAnchorX = 0, g_AimNpcAnchorY = 0, g_AimNpcAnchorW = 0;

static constexpr int kFontPopupH = 126;
static constexpr int kFontPopupHeaderH = 18;
static constexpr int kFontPopupRowH = 15;
static constexpr int kFontPopupVisible = 6;

static bool hover(int x, int y, int w, int h);
static void uiFill(int x0, int y0, int x1, int y1, int r, int g, int b, int a = 255);

static int CALLBACK enumFontCb(const LOGFONTA*, const TEXTMETRICA*, DWORD, LPARAM) {
    return 1;  
}

static int CALLBACK fontEnumProc(const LOGFONTA* lplf, const TEXTMETRICA*, DWORD, LPARAM lp) {
    auto* v = reinterpret_cast<std::vector<std::string>*>(lp);
    if (!lplf || !lplf->lfFaceName[0] || lplf->lfFaceName[0] == '@') return 1;
    std::string n = lplf->lfFaceName;
    for (auto& e : *v) if (e == n) return 1;
    if ((int)v->size() < 100) v->push_back(n);
    return 1;
}

static void ensureFontList() {
    if (!g_FontList.empty()) return;
    const char* presets[] = {
        "Tahoma", "Verdana", "Segoe UI", "Consolas", "Arial", "Calibri",
        "Courier New", "Trebuchet MS", "Georgia", "Lucida Console",
        "Segoe UI Semibold", "Microsoft Sans Serif", "Comic Sans MS", "Impact"
    };
    for (auto* p : presets) g_FontList.push_back(p);
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        LOGFONTA lf{};
        lf.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesExA(hdc, &lf, fontEnumProc, reinterpret_cast<LPARAM>(&g_FontList), 0);
        ReleaseDC(nullptr, hdc);
    }
}

static int fontPopupTop() {
    int py = g_FontAnchorY + 20;
    if (g_MenuScreenH > 0 && py + kFontPopupH > g_MenuScreenH)
        py = g_FontAnchorY - kFontPopupH;
    if (py < 2) py = 2;
    return py;
}

static std::string& activeDropdownFont() {
    return g_FontDropOwner == 1 ? g_S.speedometerFont : g_UiFont;
}

static void changeFontSelection(const std::string& face) {
    if (g_FontDropOwner == 1) {
        g_S.speedometerFont = face;
    } else {
        g_UiFont = face;
        g_Font = 0;
        ensureUiFont();
    }
}

static void handleFontDropdownInput() {
    if (!g_FontDrop) return;
    ensureFontList();

    const int px = g_FontAnchorX;
    const int py = fontPopupTop();
    const int pw = g_FontAnchorW;
    if (pw < 80) { g_FontDrop = false; return; }

    const int upX = px + pw - 42;
    const int downX = px + pw - 21;
    const bool overPopup = hover(px, py, pw, kFontPopupH);
    const int arrowDirection = hover(upX, py, 20, kFontPopupHeaderH) ? -1 :
        (hover(downX, py, 20, kFontPopupHeaderH) ? 1 : 0);
    const int keyDirection = (GetAsyncKeyState(VK_UP) & 0x8000) ? -1 :
        ((GetAsyncKeyState(VK_DOWN) & 0x8000) ? 1 : 0);
    const int direction = arrowDirection && (g_Down || g_Click) ? arrowDirection : keyDirection;
    const DWORD now = GetTickCount();
    if (direction) {
        int maxScroll = (int)g_FontList.size() - kFontPopupVisible;
        if (maxScroll < 0) maxScroll = 0;
        if (g_Click || direction != g_FontRepeatDirection) {
            g_FontScroll += direction;
            g_FontRepeatDirection = direction;
            g_FontRepeatAt = now + 260;
        } else if (now >= g_FontRepeatAt) {
            g_FontScroll += direction;
            g_FontRepeatAt = now + 75;
        }
        if (g_FontScroll < 0) g_FontScroll = 0;
        if (g_FontScroll > maxScroll) g_FontScroll = maxScroll;
        g_Click = false;
        return;
    }
    if (!g_Down && !keyDirection) g_FontRepeatDirection = 0;

    if (g_Click && overPopup) {
        if (!arrowDirection) {
            const int rowStart = py + kFontPopupHeaderH;
            for (int i = 0; i < kFontPopupVisible; ++i) {
                int index = g_FontScroll + i;
                int iy = rowStart + i * kFontPopupRowH;
                if (index >= (int)g_FontList.size()) break;
                if (hover(px, iy, pw, kFontPopupRowH)) {
                    changeFontSelection(g_FontList[index]);
                    g_FontDrop = false;
                    break;
                }
            }
        }
        g_Click = false;  
        return;
    }

    if (g_Click && hover(g_FontAnchorX, g_FontAnchorY, g_FontAnchorW - 30, 18)) {
        g_FontDrop = false;
        g_Click = false;
    } else if (g_Click && !overPopup) {
        g_FontDrop = false;
    }
}

static void drawFontDropdownOverlay() {
    if (!g_FontDrop || g_FontAnchorW < 80) return;
    ensureFontList();

    const int x = g_FontAnchorX;
    const int y = fontPopupTop();
    const int w = g_FontAnchorW;
    const int bottom = y + kFontPopupH;
    uiFill(x - 1, y - 1, x + w + 1, bottom + 1, 60, 60, 60, (int)g_S.uiAlpha);
    uiFill(x, y, x + w, bottom, (int)g_S.uiFieldR, (int)g_S.uiFieldG, (int)g_S.uiFieldB, (int)g_S.uiAlpha);
    uiFill(x, y, x + w, y + kFontPopupHeaderH, (int)g_S.uiSideR, (int)g_S.uiSideG, (int)g_S.uiSideB, (int)g_S.uiAlpha);
    text(x + 6, y + 2, (int)g_S.uiTextR, (int)g_S.uiTextG, (int)g_S.uiTextB, g_FontDropOwner == 1 ? L"Speedometer fonts" : L"UI fonts");

    const int upX = x + w - 42;
    const int downX = x + w - 21;
    uiFill(upX, y, upX + 20, y + kFontPopupHeaderH, (int)g_S.uiBtnR, (int)g_S.uiBtnG, (int)g_S.uiBtnB);
    uiFill(downX, y, downX + 20, y + kFontPopupHeaderH, (int)g_S.uiBtnR, (int)g_S.uiBtnG, (int)g_S.uiBtnB);
    text(upX + 5, y + 1, 230, 230, 230, L"^");
    text(downX + 5, y + 1, 230, 230, 230, L"v");

    int total = (int)g_FontList.size();
    int maxScroll = total - kFontPopupVisible;
    if (maxScroll < 0) maxScroll = 0;
    if (g_FontScroll < 0) g_FontScroll = 0;
    if (g_FontScroll > maxScroll) g_FontScroll = maxScroll;
    const int rowStart = y + kFontPopupHeaderH;
    for (int i = 0; i < kFontPopupVisible; ++i) {
        int index = g_FontScroll + i;
        int iy = rowStart + i * kFontPopupRowH;
        if (index >= total) break;
        bool selected = activeDropdownFont() == g_FontList[index];
        bool hovered = hover(x, iy, w, kFontPopupRowH);
        if (selected) uiFill(x, iy, x + w, iy + kFontPopupRowH, (int)g_S.uiSelR, (int)g_S.uiSelG, (int)g_S.uiSelB, (int)g_S.uiAlpha);
        else if (hovered) uiFill(x, iy, x + w, iy + kFontPopupRowH, (int)g_S.uiHoverR, (int)g_S.uiHoverG, (int)g_S.uiHoverB, (int)g_S.uiAlpha);
        wchar_t fn[64];
        toWide(g_FontList[index], fn, 64);
        text(x + 6, iy + 1, selected ? 255 : (int)g_S.uiTextR, selected ? 255 : (int)g_S.uiTextG,
             selected ? 255 : (int)g_S.uiTextB, fn);
    }

    wchar_t status[64];
    int first = total > 0 ? g_FontScroll + 1 : 0;
    int last = g_FontScroll + kFontPopupVisible;
    if (last > total) last = total;
    swprintf(status, 64, L"%d-%d of %d", first, last, total);
    text(x + 6, y + kFontPopupHeaderH + kFontPopupVisible * kFontPopupRowH + 2,
         160, 160, 160, status);
}

static constexpr int kAimNpcPopupHeaderH = 18;
static constexpr int kAimNpcPopupRowH = 18;
static int aimNpcPopupH() {
    return kAimNpcPopupHeaderH + kAimNpcPopupRowH * kAimExcludeEntryCount + 2;
}

static int aimNpcPopupTop() {
    int py = g_AimNpcAnchorY + 20;
    const int h = aimNpcPopupH();
    if (g_MenuScreenH > 0 && py + h > g_MenuScreenH)
        py = g_AimNpcAnchorY - h;
    return (std::max)(2, py);
}

static void handleAimNpcDropdownInput() {
    if (!g_AimNpcDrop) return;
    const int x = g_AimNpcAnchorX;
    const int y = aimNpcPopupTop();
    const int w = g_AimNpcAnchorW;
    const int h = aimNpcPopupH();
    if (w < 80) { g_AimNpcDrop = false; return; }
    const bool overPopup = hover(x, y, w, h);
    if (g_Click && overPopup) {
        for (int i = 0; i < kAimExcludeEntryCount; ++i) {
            const int rowY = y + kAimNpcPopupHeaderH + i * kAimNpcPopupRowH;
            if (hover(x, rowY, w, kAimNpcPopupRowH)) {
                g_S.aimExcludeMask ^= kAimExcludeEntries[i].bit;
                g_Click = false;
                return;
            }
        }
        g_Click = false;
        return;
    }
    if (g_Click && hover(g_AimNpcAnchorX, g_AimNpcAnchorY, g_AimNpcAnchorW, 18)) {
        g_AimNpcDrop = false;
        g_Click = false;
    } else if (g_Click && !overPopup) {
        g_AimNpcDrop = false;
    }
}

static void drawAimNpcDropdownOverlay() {
    if (!g_AimNpcDrop || g_AimNpcAnchorW < 80) return;
    const int x = g_AimNpcAnchorX;
    const int y = aimNpcPopupTop();
    const int w = g_AimNpcAnchorW;
    const int bottom = y + aimNpcPopupH();
    uiFill(x - 1, y - 1, x + w + 1, bottom + 1, 60, 60, 60, (int)g_S.uiAlpha);
    uiFill(x, y, x + w, bottom, (int)g_S.uiFieldR, (int)g_S.uiFieldG, (int)g_S.uiFieldB, (int)g_S.uiAlpha);
    uiFill(x, y, x + w, y + kAimNpcPopupHeaderH, (int)g_S.uiSideR, (int)g_S.uiSideG, (int)g_S.uiSideB, (int)g_S.uiAlpha);
    text(x + 6, y + 2, (int)g_S.uiTextR, (int)g_S.uiTextG, (int)g_S.uiTextB, L"Exclusions");
    for (int i = 0; i < kAimExcludeEntryCount; ++i) {
        const int rowY = y + kAimNpcPopupHeaderH + i * kAimNpcPopupRowH;
        const bool selected = (g_S.aimExcludeMask & kAimExcludeEntries[i].bit) != 0;
        const bool hovered = hover(x, rowY, w, kAimNpcPopupRowH);
        if (selected) uiFill(x, rowY, x + w, rowY + kAimNpcPopupRowH, (int)g_S.uiSelR, (int)g_S.uiSelG, (int)g_S.uiSelB, (int)g_S.uiAlpha);
        else if (hovered) uiFill(x, rowY, x + w, rowY + kAimNpcPopupRowH, (int)g_S.uiHoverR, (int)g_S.uiHoverG, (int)g_S.uiHoverB, (int)g_S.uiAlpha);

        uiFill(x + 4, rowY + 4, x + 14, rowY + 14, 40, 40, 40, 220);
        if (selected) uiFill(x + 6, rowY + 6, x + 12, rowY + 12, (int)g_S.uiCheckR, (int)g_S.uiCheckG, (int)g_S.uiCheckB, 255);
        text(x + 18, rowY + 2, selected ? 255 : (int)g_S.uiTextR,
             selected ? 255 : (int)g_S.uiTextG,
             selected ? 255 : (int)g_S.uiTextB, kAimExcludeEntries[i].label);
    }
}

static bool selectFileDialog(const char* filter, const char* title, std::string& selected) {

    std::vector<char> file(32768, '\0');
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = resolveGameWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_EXPLORER | OFN_HIDEREADONLY | OFN_ENABLESIZING;
    if (!GetOpenFileNameA(&ofn)) return false;
    selected.assign(file.data());
    if (selected.empty() || selected.size() > 2048) {
        MessageBeep(MB_ICONWARNING);
        selected.clear();
        return false;
    }
    return true;
}

static void browseCustomFont() {
    g_Focus = FOCUS_NONE;
    std::string file;
    if (!selectFileDialog("Font (*.ttf;*.otf)\0*.ttf;*.otf\0All files\0*.*\0",
                          "Add font file (install to Windows Fonts for best results)", file)) return;

    const char* base = strrchr(file.c_str(), '\\');
    base = base ? base + 1 : file.c_str();
    std::string face = base;
    size_t dot = face.find_last_of('.');
    if (dot != std::string::npos) face.resize(dot);
    if (AddFontResourceExA(file.c_str(), FR_PRIVATE, nullptr) != 0)
        g_PrivateFontFiles.push_back(file);
    changeFontSelection(face);
    g_FontList.insert(g_FontList.begin(), face);
    g_FontDrop = false;
}

bool g_FocusHit = false;
int* g_BindPtr = nullptr;
int g_BindArm = 0;
int g_SuppressKey = 0;
int g_ActiveSlider = -1;

static volatile LONG g_PendingMenuMouseEnable = -1;  
static bool g_PendingMenuConfigRefresh = false;

static void setMenuMouseEnabled(bool enabled, bool allowEngineCommands) {
    if (!allowEngineCommands) {
        InterlockedExchange(&g_PendingMenuMouseEnable, enabled ? 1 : 0);
        return;
    }
    InterlockedExchange(&g_PendingMenuMouseEnable, -1);
    if (g_Engine)
        VCall<void, const char*>(g_Engine, 7, enabled ? "cl_mouseenable 1" : "cl_mouseenable 0");
}

static bool hover(int x, int y, int w, int h) {
    return g_Mx >= x && g_Mx < x + w && g_My >= y && g_My < y + h;
}

static void uiFill(int x0, int y0, int x1, int y1, int r, int g, int b, int a) {
    int w = x1 - x0, h = y1 - y0;
    if (!g_S.uiBevel || w < 12 || h < 8) { fill(x0, y0, x1, y1, r, g, b, a); return; }
    int radius = 5;
    if (radius > h / 2) radius = h / 2;
    if (radius > w / 2) radius = w / 2;
    if (radius < 1) { fill(x0, y0, x1, y1, r, g, b, a); return; }

    if (h > radius * 2) fill(x0, y0 + radius, x1, y1 - radius, r, g, b, a);
    for (int row = 0; row < radius; ++row) {
        int dy = radius - row - 1;
        float rr = (float)radius * (float)radius - (float)dy * (float)dy;
        int inset = rr > 0.f ? radius - (int)sqrtf(rr) : radius;
        if (inset > w / 2) inset = w / 2;
        fill(x0 + inset, y0 + row, x1 - inset, y0 + row + 1, r, g, b, a);
        fill(x0 + inset, y1 - row - 1, x1 - inset, y1 - row, r, g, b, a);
    }
}

static void keyLabel(int vk, wchar_t* o, int n) {
    if (!o || n < 2) return;
    const wchar_t* s = nullptr;
    wchar_t tmp[16];
    switch (vk) {
        case 0: s = L"None"; break;
        case VK_LBUTTON: s = L"M1"; break;
        case VK_RBUTTON: s = L"M2"; break;
        case VK_MBUTTON: s = L"M3"; break;
        case VK_XBUTTON1: s = L"M4"; break;
        case VK_XBUTTON2: s = L"M5"; break;
        case VK_BROWSER_BACK: s = L"M6"; break;
        case VK_BROWSER_FORWARD: s = L"M7"; break;
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: s = L"Shift"; break;
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: s = L"Ctrl"; break;
        case VK_MENU: case VK_LMENU: case VK_RMENU: s = L"Alt"; break;
        case VK_SPACE: s = L"Space"; break;
        case VK_TAB: s = L"Tab"; break;
        case VK_CAPITAL: s = L"Caps"; break;
        case VK_INSERT: s = L"Ins"; break;
        case VK_DELETE: s = L"Del"; break;
        case VK_HOME: s = L"Home"; break;
        case VK_END: s = L"End"; break;
        case VK_PRIOR: s = L"PgUp"; break;
        case VK_NEXT: s = L"PgDn"; break;
        case VK_LEFT: s = L"Left"; break;
        case VK_RIGHT: s = L"Right"; break;
        case VK_UP: s = L"Up"; break;
        case VK_DOWN: s = L"Down"; break;
        default:
            if (vk >= 'A' && vk <= 'Z') { tmp[0] = (wchar_t)vk; tmp[1] = 0; s = tmp; }
            else if (vk >= '0' && vk <= '9') { tmp[0] = (wchar_t)vk; tmp[1] = 0; s = tmp; }
            else if (vk >= VK_F1 && vk <= VK_F12) { swprintf(tmp, 16, L"F%d", vk - VK_F1 + 1); s = tmp; }
            else { swprintf(tmp, 16, L"0x%02X", vk); s = tmp; }
            break;
    }
    wcsncpy(o, s, n - 1);
    o[n - 1] = 0;
}

void pollBinds() {
    static bool wasFocused = false;
    static LONG lastFocusEpoch = 0;
    const bool foreground = isGameWindowForeground();
    const LONG focusEpoch = InterlockedCompareExchange(&g_WindowFocusEpoch, 0, 0);

    if (!foreground) {
        wasFocused = false;
        lastFocusEpoch = focusEpoch;
        syncBindEdgesToCurrent();
        return;
    }
    if (!wasFocused || focusEpoch != lastFocusEpoch) {
        wasFocused = true;
        lastFocusEpoch = focusEpoch;
        syncBindEdgesToCurrent();
        return;
    }

    if (g_MenuOpen || g_BindPtr || shouldBlockGameplayFeatureInput()) {
        syncBindEdgesToCurrent();
        return;
    }
    if (g_SuppressKey) {
        if (GetAsyncKeyState(g_SuppressKey) & 0x8000) { syncBindEdgesToCurrent(); return; }
        g_SuppressKey = 0;
    }
    if (!g_BindPrevInitialized) syncBindEdgesToCurrent();
    struct BindSlot { int* key; bool* val; };
    BindSlot slots[] = {

        {&g_S.kEsp, &g_S.esp},
        {&g_S.kAim, &g_S.aim}, {&g_S.kAimVisible, &g_S.aimVisible},
        {&g_S.kAimPlayers, &g_S.aimPlayers}, {&g_S.kAimNpcs, &g_S.aimNpcs},
        {&g_S.kAutoStrafe, &g_S.autoStrafe},
        {&g_S.kJumpbug, &g_S.jumpbug}, {&g_S.kEdgebug, &g_S.edgebug},
        {&g_S.kEdgejump, &g_S.edgejump},
        {&g_S.kMicSpam, nullptr},
        {&g_S.kBackCam, &g_S.backCam},
        {&g_S.kNetStats, &g_S.netStats},
        {&g_S.kFullBright, &g_S.fullBright},
        {&g_S.kDrawClipBrushes, &g_S.drawClipBrushes},
    };
    uint8_t now[256] = {};
    for (int k = 1; k < 256; k++) {
        if (k == VK_XBUTTON1 || k == VK_XBUTTON2) continue;
        now[k] = (GetAsyncKeyState(k) & 0x8000) ? 1 : 0;
    }
    for (auto& s : slots) {
        int k = *s.key;
        if (k <= 0 || k >= 256 || k == g_S.menuKey || k == VK_XBUTTON1 || k == VK_XBUTTON2) continue;
        if (now[k] && !g_BindPrev[k]) {
            if (s.val) *s.val = !*s.val;
            else if (s.key == &g_S.kMicSpam) setMicSpamEnabled(!g_S.micSpam);
        }
    }

    const bool x1 = consumeExtraMouseButton(VK_XBUTTON1);
    const bool x2 = consumeExtraMouseButton(VK_XBUTTON2);
    if (x1 || x2) {
        for (auto& s : slots) {
            if ((x1 && *s.key == VK_XBUTTON1) || (x2 && *s.key == VK_XBUTTON2)) {
                if (s.val) *s.val = !*s.val;
                else if (s.key == &g_S.kMicSpam) setMicSpamEnabled(!g_S.micSpam);
            }
        }
    }
    memcpy(g_BindPrev, now, sizeof g_BindPrev);
}

static void browseImageFile() {
    g_Focus = FOCUS_NONE;
    std::string file;
    if (!selectFileDialog("Images (*.vtf;*.png;*.jpg;*.jpeg;*.gif;*.bmp)\0*.vtf;*.png;*.jpg;*.jpeg;*.gif;*.bmp\0All files\0*.*\0",
                          "Select watermark image", file)) return;
    g_WmImgPath = file;
    g_S.wmImg = true;
    invalidateImageCache();
}

void cancelBindCapture() {
    g_BindPtr = nullptr;
    g_BindArm = 0;
}

static void browseSoundFile(std::string& dest) {
    g_Focus = FOCUS_NONE;
    std::string path;
    if (!selectFileDialog("Audio (*.wav;*.mp3;*.ogg)\0*.wav;*.mp3;*.ogg\0All files\0*.*\0",
                          "Select custom audio", path)) return;
    dest = path;
}

static bool isUiMouseVirtualKey(int key) {

    return key == VK_LBUTTON || key == VK_RBUTTON;
}

static void pollBindCapture() {
    if (!g_BindPtr || g_Focus != FOCUS_NONE) return;

    if (g_BindArm) {
        const bool uiClickHeld =
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if (uiClickHeld) return;
        g_BindArm = 0;
    }

    auto acceptKey = [](int k) -> bool {
        if (k <= 0 || k >= 256) return false;
        if (k == VK_ESCAPE) { cancelBindCapture(); g_SuppressKey = VK_ESCAPE; return true; }
        if (k == VK_DELETE) {
            *g_BindPtr = 0;
            cancelBindCapture();
            g_SuppressKey = k;
            return true;
        }
        if (k == g_S.menuKey) return false;
        *g_BindPtr = k;
        g_SuppressKey = k;
        cancelBindCapture();
        return true;
    };

    if (consumeExtraMouseButton(VK_XBUTTON1)) { acceptKey(VK_XBUTTON1); return; }
    if (consumeExtraMouseButton(VK_XBUTTON2)) { acceptKey(VK_XBUTTON2); return; }

    const int mouseKeys[] = { VK_MBUTTON, VK_BROWSER_BACK, VK_BROWSER_FORWARD };
    for (int k : mouseKeys) {
        if (GetAsyncKeyState(k) & 0x8000) {
            acceptKey(k);
            return;
        }
    }

    for (int k = 1; k < 256; k++) {
        if (!(GetAsyncKeyState(k) & 0x8000)) continue;
        if (isUiMouseVirtualKey(k)) continue;
        if (acceptKey(k)) return;
    }
}

static void pollText() {
    if (g_Focus == FOCUS_NONE) return;
    std::string* dst = &g_NameEdit;
    size_t maxLen = 48;
    if (g_Focus == FOCUS_WM) { dst = &g_WmText; maxLen = 32; }
    else if (g_Focus == FOCUS_IMG) { dst = &g_WmImgPath; maxLen = 2048; }
    else if (g_Focus == FOCUS_FONT) { dst = &g_UiFont; maxLen = 64; }
    else if (g_Focus == FOCUS_DEATH_SND) { dst = &g_S.deathSoundPath; maxLen = 2048; }
    static uint8_t prev[256];
    static DWORD backStart = 0, backLast = 0;
    DWORD now = GetTickCount();
    for (int k = 1; k < 256; k++) {
        bool down = (GetAsyncKeyState(k) & 0x8000) != 0;
        bool edge = down && !prev[k];
        if (k == VK_BACK) {
            if (edge) {
                if (!dst->empty()) dst->pop_back();
                backStart = now;
                backLast = now;
            } else if (down && now - backStart > 350 && now - backLast > 40) {
                if (!dst->empty()) dst->pop_back();
                backLast = now;
            }
            if (!down) backStart = backLast = 0;
        } else if (edge) {
            if (k == VK_ESCAPE || k == VK_RETURN) {
                g_Focus = FOCUS_NONE;
                if (k == VK_ESCAPE) g_SuppressKey = VK_ESCAPE;
            } else if (dst->size() < maxLen) {
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                char c = 0;
                if (k >= 'A' && k <= 'Z') c = shift ? (char)k : (char)(k + 32);
                else if (k >= '0' && k <= '9') c = (char)k;
                else if (k == VK_OEM_MINUS) c = shift ? '_' : '-';
                else if (k == VK_OEM_2 || k == VK_OEM_5) c = '\\';
                else if (k == VK_OEM_PERIOD) c = '.';
                else if (k == VK_SPACE) c = (g_Focus == FOCUS_WM || g_Focus == FOCUS_IMG || g_Focus == FOCUS_FONT || g_Focus == FOCUS_DEATH_SND) ? ' ' : '_';
                else if ((g_Focus == FOCUS_IMG || g_Focus == FOCUS_FONT || g_Focus == FOCUS_DEATH_SND) && k == VK_OEM_1) c = ':';
                if (c) dst->push_back(c);
            }
        }
        prev[k] = down ? 1 : 0;
    }
}

static bool checkbox(const wchar_t* label, bool& v, int x, int y, int* bind) {
    int labelW = bind ? kRowW - 78 : kRowW;
    bool hov = hover(x, y, labelW, 16);
    if (hov && g_Click && !g_BindPtr) v = !v;
    int cr = (int)g_S.uiCheckR, cg = (int)g_S.uiCheckG, cb = (int)g_S.uiCheckB;
    uiFill(x, y + 1, x + 13, y + 14, cr, cg, cb);
    uiFill(x + 1, y + 2, x + 12, y + 13, (int)g_S.uiBgR, (int)g_S.uiBgG, (int)g_S.uiBgB);
    if (v) uiFill(x + 3, y + 4, x + 10, y + 11, (int)g_S.uiAccR, (int)g_S.uiAccG, (int)g_S.uiAccB);
    int tr = hov ? 255 : (int)g_S.uiTextR, tg = hov ? 255 : (int)g_S.uiTextG, tb = hov ? 255 : (int)g_S.uiTextB;
    text(x + 20, y, tr, tg, tb, label);
    if (bind) {
        int bx = x + kRowW - 74;
        bool bh = hover(bx, y - 1, 74, 18);
        bool active = g_BindPtr == bind;
        int br = active || bh ? (int)g_S.uiHoverR : (int)g_S.uiBtnR;
        int bg = active || bh ? (int)g_S.uiHoverG : (int)g_S.uiBtnG;
        int bb = active || bh ? (int)g_S.uiHoverB : (int)g_S.uiBtnB;
        uiFill(bx, y - 1, bx + 74, y + 17, br, bg, bb);
        wchar_t lab[24];
        if (active) wcsncpy(lab, L"Press", 24);
        else keyLabel(*bind, lab, 24);
        text(bx + 6, y, 230, 230, 230, lab);
        if (bh && g_Click) { g_BindPtr = bind; g_BindArm = 1; g_Focus = FOCUS_NONE; }
    }
    return v;
}

static void bindRow(const wchar_t* label, int* bind, int x, int y) {
    text(x, y, (int)g_S.uiTextR, (int)g_S.uiTextG, (int)g_S.uiTextB, label);
    int bx = x + kRowW - 74;
    bool bh = hover(bx, y - 1, 74, 18);
    bool active = g_BindPtr == bind;
    int br = active || bh ? (int)g_S.uiHoverR : (int)g_S.uiBtnR;
    uiFill(bx, y - 1, bx + 74, y + 17, br, br == (int)g_S.uiHoverR ? (int)g_S.uiHoverG : (int)g_S.uiBtnG,
         br == (int)g_S.uiHoverR ? (int)g_S.uiHoverB : (int)g_S.uiBtnB);
    wchar_t lab[24];
    if (active) wcsncpy(lab, L"Press", 24);
    else keyLabel(*bind, lab, 24);
    text(bx + 6, y, 230, 230, 230, lab);
    if (bh && g_Click) { g_BindPtr = bind; g_BindArm = 1; g_Focus = FOCUS_NONE; }
}

static void slider(const wchar_t* label, float& v, float lo, float hi, int x, int y, const wchar_t* fmt, int id) {
    wchar_t buf[64];
    swprintf(buf, 64, fmt, v);
    text(x, y, (int)g_S.uiTextR, (int)g_S.uiTextG, (int)g_S.uiTextB, label);
    text(x + kRowW - 48, y, 230, 230, 230, buf);
    int by = y + 16;
    uiFill(x, by, x + kRowW, by + 4, 42, 42, 42);
    float f = (hi == lo) ? 0.f : (v - lo) / (hi - lo);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    uiFill(x, by, x + (int)(kRowW * f), by + 4, (int)g_S.uiAccR, (int)g_S.uiAccG, (int)g_S.uiAccB);
    bool zone = hover(x, by - 8, kRowW, 18);
    if (g_Click && zone) {
        g_ActiveSlider = id;
        g_Focus = FOCUS_NONE;
        g_FocusHit = true;
    }

    if (g_Down && g_ActiveSlider == id) {
        float nf = (float)(g_Mx - x) / (float)kRowW;
        if (nf < 0.f) nf = 0.f;
        if (nf > 1.f) nf = 1.f;
        v = lo + nf * (hi - lo);
        if (!std::isfinite(v)) v = lo;
    }
    if (!g_Down && g_ActiveSlider == id) g_ActiveSlider = -1;
}

static bool button(const wchar_t* label, int x, int y, int w) {
    bool hov = hover(x, y, w, 20);
    int r = hov ? (int)g_S.uiHoverR : (int)g_S.uiBtnR;
    int g = hov ? (int)g_S.uiHoverG : (int)g_S.uiBtnG;
    int b = hov ? (int)g_S.uiHoverB : (int)g_S.uiBtnB;
    uiFill(x, y, x + w, y + 20, r, g, b);
    text(x + 8, y + 3, 240, 240, 240, label);
    return hov && g_Click && !g_BindPtr;
}

static void selector(const wchar_t* label, int& v, const wchar_t* const* names, int count, int x, int y) {
    bool hov = hover(x, y, kRowW, 18);
    if (hov && g_Click && !g_BindPtr) v = (v + 1) % count;
    int r = hov ? (int)g_S.uiHoverR : (int)g_S.uiBtnR;
    uiFill(x, y, x + kRowW, y + 18, r, hov ? (int)g_S.uiHoverG : (int)g_S.uiBtnG, hov ? (int)g_S.uiHoverB : (int)g_S.uiBtnB);
    text(x + 6, y + 2, (int)g_S.uiTextR, (int)g_S.uiTextG, (int)g_S.uiTextB, label);
    text(x + kRowW - 90, y + 2, 240, 240, 240, names[v]);
}

static void textField(const wchar_t* label, const std::string& value, int focusId, int x, int y, int w) {
    text(x, y, 180, 180, 180, label);
    int by = y + 14;
    bool hov = hover(x, by, w, 20);
    if (hov && g_Click) { g_Focus = focusId; g_FocusHit = true; g_BindPtr = nullptr; }
    else if (hov) g_FocusHit = g_Focus == focusId;
    bool on = g_Focus == focusId;
    int fr = on ? (int)g_S.uiSelR : (int)g_S.uiFieldR;
    int fg = on ? (int)g_S.uiSelG : (int)g_S.uiFieldG;
    int fb = on ? (int)g_S.uiSelB : (int)g_S.uiFieldB;
    uiFill(x, by, x + w, by + 20, fr, fg, fb);
    if (on) uiFill(x, by, x + 2, by + 20, (int)g_S.uiAccR, (int)g_S.uiAccG, (int)g_S.uiAccB);
    wchar_t buf[48];
    std::string displayValue = value;
    if ((focusId == FOCUS_IMG || focusId == FOCUS_DEATH_SND) && displayValue.size() > 39)
        displayValue = "..." + displayValue.substr(displayValue.size() - 36);
    toWide(displayValue, buf, 40);
    if (on && wcslen(buf) < 40) wcscat(buf, L"|");
    if (!buf[0]) wcscpy(buf, L" ");
    text(x + 8, by + 3, 235, 235, 235, buf);
}

static int subTabs(int x, int y, int w, int& sub, const wchar_t* const* names, int n) {

    if (n <= 0) return 0;
    if (sub < 0 || sub >= n) sub = 0;
    const int columns = (n > 4) ? 4 : n;
    const int rows = (n + columns - 1) / columns;
    const int cellW = w / columns;
    const int rowH = 20;
    for (int i = 0; i < n; ++i) {
        const int row = i / columns;
        const int col = i % columns;
        const int sx = x + col * cellW;
        const int sy = y + row * rowH;
        const int sw = (col == columns - 1) ? (x + w - sx) : cellW;
        const bool sel = sub == i;
        const bool hov = hover(sx, sy, sw - 2, rowH - 2);
        if (hov && g_Click && !g_BindPtr) sub = i;
        if (sel) uiFill(sx, sy, sx + sw - 2, sy + rowH - 2,
                        (int)g_S.uiSelR, (int)g_S.uiSelG, (int)g_S.uiSelB);
        else if (hov) uiFill(sx, sy, sx + sw - 2, sy + rowH - 2,
                             (int)g_S.uiHoverR, (int)g_S.uiHoverG, (int)g_S.uiHoverB);
        text(sx + 4, sy + 2, sel ? 255 : 170, sel ? 255 : 170, sel ? 255 : 170, names[i]);
    }
    return rows * rowH;
}

static constexpr int kSoundPopupHeaderH = 18;
static constexpr int kSoundPopupRowH = 15;
static constexpr int kSoundPopupVisible = 10;

static int soundPopupH() {
    return kSoundPopupHeaderH + 18 + kSoundPopupRowH * kSoundPopupVisible + 16;
}

static int soundPopupTop() {
    int py = g_SoundAnchorY + 20;
    const int h = soundPopupH();
    if (g_MenuScreenH > 0 && py + h > g_MenuScreenH)
        py = g_SoundAnchorY - h;
    return (std::max)(2, py);
}

static void rebuildSoundFilter() {
    ensureGameSoundCatalog();
    g_SoundFilteredIdx.clear();
    const auto& sounds = gameSoundCatalog();
    const std::string filter = g_SoundFilterEdit;
    for (int i = 0; i < static_cast<int>(sounds.size()); ++i) {
        if (!filter.empty()) {

            const std::string& s = sounds[i];
            bool match = false;
            if (filter.size() <= s.size()) {
                for (size_t pos = 0; pos + filter.size() <= s.size(); ++pos) {
                    if (_strnicmp(s.c_str() + pos, filter.c_str(), filter.size()) == 0) {
                        match = true;
                        break;
                    }
                }
            }
            if (!match) continue;
        }
        g_SoundFilteredIdx.push_back(i);
    }
}

static void pollSoundFilterKeys() {
    if (!g_SoundDrop) return;
    static uint8_t prev[256];
    for (int k = 1; k < 256; k++) {
        bool down = (GetAsyncKeyState(k) & 0x8000) != 0;
        bool edge = down && !prev[k];
        prev[k] = down ? 1 : 0;
        if (!edge) continue;
        if (k == VK_BACK) {
            if (!g_SoundFilterEdit.empty()) g_SoundFilterEdit.pop_back();
            g_SoundDropScroll = 0;
            continue;
        }
        if (k == VK_ESCAPE) { g_SoundDrop = false; continue; }
        if (g_SoundFilterEdit.size() >= 48) continue;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        char c = 0;
        if (k >= 'A' && k <= 'Z') c = shift ? (char)k : (char)(k + 32);
        else if (k >= '0' && k <= '9') c = (char)k;
        else if (k == VK_OEM_MINUS) c = shift ? '_' : '-';
        else if (k == VK_OEM_2 || k == VK_OEM_5) c = '/';
        else if (k == VK_OEM_PERIOD) c = '.';
        else if (k == VK_SPACE) c = ' ';
        if (c) {
            g_SoundFilterEdit.push_back(c);
            g_SoundDropScroll = 0;
        }
    }
}

static void handleSoundDropdownInput() {
    if (!g_SoundDrop) return;
    ensureGameSoundCatalog();
    rebuildSoundFilter();
    const auto& sounds = gameSoundCatalog();
    const int count = static_cast<int>(g_SoundFilteredIdx.size());
    const int px = g_SoundAnchorX;
    const int py = soundPopupTop();
    const int pw = g_SoundAnchorW;
    if (pw < 80) { g_SoundDrop = false; return; }
    const int upX = px + pw - 42;
    const int downX = px + pw - 21;
    const bool overPopup = hover(px, py, pw, soundPopupH());
    const int arrowDirection = hover(upX, py, 20, kSoundPopupHeaderH) ? -1 :
        (hover(downX, py, 20, kSoundPopupHeaderH) ? 1 : 0);
    const int keyDirection = (GetAsyncKeyState(VK_UP) & 0x8000) ? -1 :
        ((GetAsyncKeyState(VK_DOWN) & 0x8000) ? 1 : 0);
    const int direction = arrowDirection && (g_Down || g_Click) ? arrowDirection : keyDirection;
    const DWORD now = GetTickCount();
    const int maxScroll = (std::max)(0, count - kSoundPopupVisible);
    if (direction) {
        if (g_Click || direction != g_SoundRepeatDirection) {
            g_SoundDropScroll += direction;
            g_SoundRepeatDirection = direction;
            g_SoundRepeatAt = now + 260;
        } else if (now >= g_SoundRepeatAt) {
            g_SoundDropScroll += direction;
            g_SoundRepeatAt = now + 50;
        }
        if (g_SoundDropScroll < 0) g_SoundDropScroll = 0;
        if (g_SoundDropScroll > maxScroll) g_SoundDropScroll = maxScroll;
    } else {
        g_SoundRepeatDirection = 0;
    }
    if (g_Click && overPopup) {
        const int rowStart = py + kSoundPopupHeaderH + 18;  
        for (int i = 0; i < kSoundPopupVisible; ++i) {
            const int rowIndex = g_SoundDropScroll + i;
            if (rowIndex < 0 || rowIndex >= count) break;
            const int iy = rowStart + i * kSoundPopupRowH;
            if (hover(px, iy, pw - 44, kSoundPopupRowH)) {
                const int soundIdx = g_SoundFilteredIdx[rowIndex];
                if (soundIdx >= 0 && soundIdx < static_cast<int>(sounds.size())) {
                    g_S.deathSoundPath = sounds[soundIdx];
                }
                g_SoundDrop = false;
                break;
            }
        }
    }
    if (g_Click && !overPopup && !hover(g_SoundAnchorX, g_SoundAnchorY, g_SoundAnchorW, 18))
        g_SoundDrop = false;
}

static void drawSoundDropdownOverlay() {
    if (!g_SoundDrop || g_SoundAnchorW < 80) return;
    ensureGameSoundCatalog();
    rebuildSoundFilter();
    const auto& sounds = gameSoundCatalog();
    const int count = static_cast<int>(g_SoundFilteredIdx.size());
    const int x = g_SoundAnchorX;
    const int y = soundPopupTop();
    const int w = g_SoundAnchorW;
    const int h = soundPopupH();
    uiFill(x, y, x + w, y + h, (int)g_S.uiBgR, (int)g_S.uiBgG, (int)g_S.uiBgB, (int)g_S.uiAlpha);
    uiFill(x, y, x + w, y + kSoundPopupHeaderH, (int)g_S.uiSideR, (int)g_S.uiSideG, (int)g_S.uiSideB, (int)g_S.uiAlpha);
    wchar_t title[64]{};
    swprintf(title, 64, L"Death sounds (%d)", count);
    text(x + 6, y + 2, (int)g_S.uiTextR, (int)g_S.uiTextG, (int)g_S.uiTextB, title);
    text(x + w - 40, y + 2, 180, 180, 180, L"▲▼");

    const int searchY = y + kSoundPopupHeaderH;
    uiFill(x + 4, searchY + 1, x + w - 4, searchY + 17, (int)g_S.uiFieldR, (int)g_S.uiFieldG, (int)g_S.uiFieldB);
    wchar_t filterW[72]{};
    MultiByteToWideChar(CP_UTF8, 0, g_SoundFilterEdit.c_str(), -1, filterW, 72);
    if (!filterW[0]) wcscpy(filterW, L"Search...");
    text(x + 8, searchY + 2, 200, 200, 200, filterW);

    const std::string& cur = g_S.deathSoundPath;
    const int rowStart = searchY + 18;
    for (int i = 0; i < kSoundPopupVisible; ++i) {
        const int rowIndex = g_SoundDropScroll + i;
        if (rowIndex < 0 || rowIndex >= count) break;
        const int soundIdx = g_SoundFilteredIdx[rowIndex];
        if (soundIdx < 0 || soundIdx >= static_cast<int>(sounds.size())) continue;
        const int iy = rowStart + i * kSoundPopupRowH;
        const bool isSelected = _stricmp(sounds[soundIdx].c_str(), cur.c_str()) == 0;
        const bool hovered = hover(x, iy, w - 44, kSoundPopupRowH);
        if (isSelected) uiFill(x, iy, x + w, iy + kSoundPopupRowH, (int)g_S.uiSelR, (int)g_S.uiSelG, (int)g_S.uiSelB, (int)g_S.uiAlpha);
        else if (hovered) uiFill(x, iy, x + w, iy + kSoundPopupRowH, (int)g_S.uiHoverR, (int)g_S.uiHoverG, (int)g_S.uiHoverB, (int)g_S.uiAlpha);
        wchar_t label[96]{};
        MultiByteToWideChar(CP_UTF8, 0, sounds[soundIdx].c_str(), -1, label, 96);
        text(x + 8, iy + 1, isSelected ? 255 : (int)g_S.uiTextR,
             isSelected ? 255 : (int)g_S.uiTextG,
             isSelected ? 255 : (int)g_S.uiTextB, label);
    }
    wchar_t status[64]{};
    const int first = count > 0 ? g_SoundDropScroll + 1 : 0;
    const int last = (std::min)(count, g_SoundDropScroll + kSoundPopupVisible);
    swprintf(status, 64, L"%d-%d / %d  (type to filter)", first, last, count);
    text(x + 6, rowStart + kSoundPopupVisible * kSoundPopupRowH + 1, 150, 150, 150, status);
}

void drawMenu(int sw, int sh) {
    g_MenuScreenH = sh;
    int& mx = g_Menu.x, &my = g_Menu.y;
    g_FocusHit = false;
    if (g_Menu.drag) {
        if (!g_Down) g_Menu.drag = false;
        else { mx = g_Mx - g_Menu.dx; my = g_My - g_Menu.dy; }
    } else if (g_Click && hover(mx, my, kMenuW - 28, 26) && !g_BindPtr) {
        g_Menu.drag = true;
        g_Menu.dx = g_Mx - mx;
        g_Menu.dy = g_My - my;
    }
    if (mx < 4) mx = 4;
    if (my < 4) my = 4;
    if (sw > kMenuW + 8 && mx > sw - kMenuW - 4) mx = sw - kMenuW - 4;
    if (sh > kMenuH + 8 && my > sh - kMenuH - 4) my = sh - kMenuH - 4;

    int bgR = (int)g_S.uiBgR, bgG = (int)g_S.uiBgG, bgB = (int)g_S.uiBgB;
    int sdR = (int)g_S.uiSideR, sdG = (int)g_S.uiSideG, sdB = (int)g_S.uiSideB;
    int txR = (int)g_S.uiTextR, txG = (int)g_S.uiTextG, txB = (int)g_S.uiTextB;
    int acR = (int)g_S.uiAccR, acG = (int)g_S.uiAccG, acB = (int)g_S.uiAccB;

    int uiA = (int)g_S.uiAlpha;
    if (uiA < 40) uiA = 40;
    if (uiA > 255) uiA = 255;

    uiFill(mx - 1, my - 1, mx + kMenuW + 1, my + kMenuH + 1, 70, 70, 70, uiA);

    if (g_S.uiBlur) {
        for (int i = 8; i >= 1; i--) {
            int aa = uiA / (2 + i);
            if (aa < 10) aa = 10;
            int tint = 20 + i * 4;
            uiFill(mx - i, my - i, mx + kMenuW + i, my + kMenuH + i,
                 bgR + tint / 4, bgG + tint / 4, bgB + tint / 3, aa);
        }
        for (int yy = my; yy < my + kMenuH; yy += 2)
            uiFill(mx, yy, mx + kMenuW, yy + 1, 255, 255, 255, (yy / 2) % 2 ? 10 : 4);

        uiFill(mx, my, mx + kMenuW, my + 2, 255, 255, 255, 40);
    }
    uiFill(mx, my, mx + kMenuW, my + kMenuH, bgR, bgG, bgB, g_S.uiBlur ? (uiA * 3 / 4) : uiA);
    uiFill(mx, my, mx + kMenuW, my + 26, sdR, sdG, sdB, uiA);
    text(mx + 12, my + 6, txR, txG, txB, L"NOCOM");

    int cx = mx + kMenuW - 24, cy = my + 4;
    bool xhov = hover(cx, cy, 18, 18);
    uiFill(cx, cy, cx + 18, cy + 18, xhov ? (int)g_S.uiCloseR : (int)g_S.uiBtnR,
         xhov ? (int)g_S.uiCloseG : (int)g_S.uiBtnG, xhov ? (int)g_S.uiCloseB : (int)g_S.uiBtnB);
    int xTextW = 0, xTextH = 0;
    measureText(g_Font, L"X", xTextW, xTextH);
    text(cx + (18 - xTextW) / 2, cy + (18 - xTextH) / 2, 255, 255, 255, L"X");
    if (xhov && g_Click && !g_BindPtr) {
        g_MenuOpen = false;
        cancelBindCapture();
        g_Focus = FOCUS_NONE;
        VCall<void, const char*>(g_Engine, 7, "cl_mouseenable 1");
    }

    uiFill(mx, my + 26, mx + kSide, my + kMenuH, sdR, sdG, sdB, uiA);
    const wchar_t* tabs[] = {L"Visuals", L"Utilities", L"Configs", L"Settings", L"About", L"Debug"};
    const int tabValues[] = {0, 2, 3, 4, 5, 6};  
    int tabCount = g_S.debugMode ? 6 : 5;
    bool tabVisible = false;
    for (int i = 0; i < tabCount; ++i) if (g_Menu.tab == tabValues[i]) tabVisible = true;
    if (!tabVisible) g_Menu.tab = 0;
    for (int i = 0; i < tabCount; i++) {
        int tx = mx + 6, ty = my + 32 + i * 26, tw = kSide - 12;
        bool sel = g_Menu.tab == tabValues[i];
        bool hov = hover(tx, ty, tw, 22);
        if (hov && g_Click && !g_BindPtr) {
            g_Menu.tab = tabValues[i];
        }
        if (sel) uiFill(tx, ty, tx + tw, ty + 22, (int)g_S.uiSelR, (int)g_S.uiSelG, (int)g_S.uiSelB);
        else if (hov) uiFill(tx, ty, tx + tw, ty + 22, (int)g_S.uiHoverR, (int)g_S.uiHoverG, (int)g_S.uiHoverB);
        if (sel) uiFill(tx, ty, tx + 2, ty + 22, acR, acG, acB);
        text(tx + 10, ty + 4, sel ? 255 : 176, sel ? 255 : 176, sel ? 255 : 176, tabs[i]);
    }

    const int ejectX = mx + 6;
    const int ejectY = my + kMenuH - 28;
    const int ejectW = kSide - 12;
    uiFill(ejectX, ejectY - 6, ejectX + ejectW, ejectY - 5, 58, 58, 58, uiA);
    if (button(L"Eject", ejectX, ejectY, ejectW)) requestEject();

    int x = mx + kSide + 10, y = my + 34;
    int contentW = kRowW;
    static DWORD cfgMsgUntil;
    static const wchar_t* cfgMsg = L"";
    int tab = g_Menu.tab;
    if (tab < 0) tab = 0;

    int maxTab = g_S.debugMode ? 6 : 5;
    if (tab > maxTab) tab = maxTab;
    g_Menu.tab = tab;
    int& sub = g_Menu.sub[tab];
    if (g_FontDrop && ((g_FontDropOwner == 0 && (tab != 4 || sub != 2)) ||
                       (g_FontDropOwner == 1 && (tab != 0 || sub != 5 || !g_S.speedometer)))) g_FontDrop = false;
    if (g_AimNpcDrop && (tab != 2 || sub != 0 || !g_S.aimNpcs)) g_AimNpcDrop = false;
    handleFontDropdownInput();
    handleAimNpcDropdownInput();
    pollSoundFilterKeys();
    handleSoundDropdownInput();

    if (tab == 0) {
        static const wchar_t* subs[] = {L"Watermark", L"ESP", L"FOV", L"Camera", L"World", L"Overlay", L"HUD"};
        if (sub < 0 || sub > 6) sub = 0;
        y += subTabs(x, y, contentW, sub, subs, 7) + 4;
        if (sub == 0) {

            const int pageY = y;
            bool textSel = g_WatermarkPage == 0;
            bool imageSel = g_WatermarkPage == 1;
            bool textHov = hover(x, pageY, 82, 20);
            bool imageHov = hover(x + 88, pageY, 82, 20);
            uiFill(x, pageY, x + 82, pageY + 20, textSel ? (int)g_S.uiSelR : (textHov ? (int)g_S.uiHoverR : (int)g_S.uiBtnR),
                 textSel ? (int)g_S.uiSelG : (textHov ? (int)g_S.uiHoverG : (int)g_S.uiBtnG),
                 textSel ? (int)g_S.uiSelB : (textHov ? (int)g_S.uiHoverB : (int)g_S.uiBtnB));
            uiFill(x + 88, pageY, x + 170, pageY + 20, imageSel ? (int)g_S.uiSelR : (imageHov ? (int)g_S.uiHoverR : (int)g_S.uiBtnR),
                 imageSel ? (int)g_S.uiSelG : (imageHov ? (int)g_S.uiHoverG : (int)g_S.uiBtnG),
                 imageSel ? (int)g_S.uiSelB : (imageHov ? (int)g_S.uiHoverB : (int)g_S.uiBtnB));
            text(x + 12, pageY + 3, 235, 235, 235, L"Text");
            text(x + 100, pageY + 3, 235, 235, 235, L"Image");
            if (g_Click && !g_BindPtr) {
                if (textHov) { g_WatermarkPage = 0; g_Focus = FOCUS_NONE; }
                else if (imageHov) { g_WatermarkPage = 1; g_Focus = FOCUS_NONE; }
            }
            y += 24;
            if (g_WatermarkPage == 0) {
                checkbox(L"Text watermark", g_S.watermark, x, y, nullptr); y += 18;
                checkbox(L"Background", g_S.wmBgOn, x, y, nullptr); y += 18;
                textField(L"Text", g_WmText, FOCUS_WM, x, y, contentW); y += 36;
                slider(L"Text scale", g_S.wmScale, 8.f, 72.f, x, y, L"%.0f", 10); y += 28;
                slider(L"Text X %", g_S.wmX, 0.f, 100.f, x, y, L"%.0f", 11); y += 28;
                slider(L"Text Y %", g_S.wmY, 0.f, 100.f, x, y, L"%.0f", 12);
            } else {
                checkbox(L"Image watermark", g_S.wmImg, x, y, nullptr); y += 18;
                checkbox(L"DVD bounce", g_S.wmImgDvd, x, y, nullptr); y += 18;
                textField(L"Image path", g_WmImgPath, FOCUS_IMG, x, y, contentW - 36);
                if (button(L"+", x + contentW - 28, y + 14, 28)) browseImageFile();
                y += 34;
                slider(L"Image scale %", g_S.wmImgScale, 10.f, 400.f, x, y, L"%.0f", 20); y += 28;
                if (g_S.wmImgDvd) {
                    slider(L"DVD speed %", g_S.wmImgDvdSpeed, 10.f, 500.f, x, y, L"%.0f", 25); y += 28;
                } else {
                    slider(L"Image X %", g_S.wmImgX, 0.f, 100.f, x, y, L"%.0f", 22); y += 28;
                    slider(L"Image Y %", g_S.wmImgY, 0.f, 100.f, x, y, L"%.0f", 23); y += 28;
                }
                slider(L"Image alpha", g_S.wmImgAlpha, 0.f, 255.f, x, y, L"%.0f", 24);
            }
        } else if (sub == 1) {

            checkbox(L"ESP", g_S.esp, x, y, &g_S.kEsp); y += 20;
            checkbox(L"Trajectories", g_S.waterBombTrajectories, x, y, nullptr); y += 20;
            checkbox(L"Nade boost radius", g_S.nadeBoostIndicator, x, y, nullptr); y += 20;
            checkbox(L"Prediction", g_S.prediction, x, y, nullptr); y += 20;
            if (g_S.esp) {
                slider(L"Max distance (0=unlimited)", g_S.espMaxDistance, 0.f, 10000.f, x, y, L"%.0f", 230); y += 28;
                slider(L"Refresh interval (ms)", g_S.espUpdateIntervalMs, 16.f, 100.f, x, y, L"%.0f", 231); y += 28;
                checkbox(L"Box", g_S.espBox, x, y, nullptr); y += 18;
                checkbox(L"Bones", g_S.espBones, x, y, nullptr); y += 18;
                checkbox(L"Name", g_S.espName, x, y, nullptr); y += 18;
                checkbox(L"Health (players)", g_S.espHealth, x, y, nullptr); y += 18;
                checkbox(L"Entities", g_S.espEntities, x, y, nullptr); y += 18;
                checkbox(L"Items", g_S.espItems, x, y, nullptr); y += 18;
                checkbox(L"Wires", g_S.espWires, x, y, nullptr); y += 18;
                if (g_S.espWires) {
                    checkbox(L"Players", g_S.espWiresPlayers, x, y, nullptr); y += 18;
                    checkbox(L"NPCs", g_S.espWiresNpcs, x, y, nullptr); y += 18;
                    checkbox(L"Items", g_S.espWiresItems, x, y, nullptr); y += 18;
                    slider(L"Width", g_S.espWireWidth, 1.f, 8.f, x, y, L"%.0f", 120); y += 28;
                }
            }
        } else if (sub == 2) {
            static int fovSub = 0;
            static const wchar_t* fovSubs[] = {L"Global", L"Viewmodel"};
            if (fovSub < 0 || fovSub > 1) fovSub = 0;
            y += subTabs(x, y, contentW, fovSub, fovSubs, 2) + 4;
            if (fovSub == 0) {
                checkbox(L"Custom FOV", g_S.fovOn, x, y, nullptr); y += 22;
                slider(L"Global FOV", g_S.fov, 10.f, 170.f, x, y, L"%.0f", 1);
            } else {
                checkbox(L"Viewmodel FOV override", g_S.viewmodelFovOn, x, y, nullptr); y += 20;
                if (g_S.viewmodelFovOn) {
                    slider(L"Viewmodel FOV", g_S.viewmodelFov, 40.f, 120.f, x, y, L"%.0f", 90); y += 28;
                }
                checkbox(L"Viewmodel position override", g_S.viewmodelOn, x, y, nullptr); y += 20;
                if (g_S.viewmodelOn) {
                    slider(L"Position X", g_S.viewmodelX, -40.f, 40.f, x, y, L"%.1f", 91); y += 28;
                    slider(L"Position Y", g_S.viewmodelY, -40.f, 40.f, x, y, L"%.1f", 92); y += 28;
                    slider(L"Position Z", g_S.viewmodelZ, -40.f, 40.f, x, y, L"%.1f", 93); y += 28;
                    slider(L"Rotation X", g_S.viewmodelRotX, -90.f, 90.f, x, y, L"%.1f", 95); y += 28;
                    slider(L"Rotation Y", g_S.viewmodelRotY, -90.f, 90.f, x, y, L"%.1f", 96); y += 28;
                    slider(L"Rotation Z", g_S.viewmodelRotZ, -90.f, 90.f, x, y, L"%.1f", 97); y += 28;
                }
                checkbox(L"Viewmodel Sway", g_S.viewmodelSway, x, y, nullptr); y += 18;
                if (g_S.viewmodelSway) {
                    slider(L"Sway scale", g_S.viewmodelSwayAmount, 0.f, 25.f, x, y, L"%.1f", 94); y += 28;
                }
            }
        } else if (sub == 3) {

            bindRow(L"Backwards camera", &g_S.kBackCam, x, y); y += 24;
            slider(L"Camera pitch", g_S.backCamPitch, -89.f, 89.f, x, y, L"%.0f", 82);
        } else if (sub == 4) {
            checkbox(L"Fullbright", g_S.fullBright, x, y, &g_S.kFullBright); y += 22;
                        checkbox(L"Custom fog", g_S.customFog, x, y, nullptr); y += 22;
            if (g_S.customFog) {
                slider(L"Fog Start", g_S.fogStart, 0.f, 10000.f, x, y, L"%.0f", 201); y += 27;
                slider(L"Fog End", g_S.fogEnd, 1.f, 20000.f, x, y, L"%.0f", 202); y += 27;
                slider(L"Fog Density", g_S.fogDensity, 0.f, 1.f, x, y, L"%.2f", 203); y += 27;
                slider(L"Fog R", g_S.fogR, 0.f, 255.f, x, y, L"%.0f", 204); y += 27;
                slider(L"Fog G", g_S.fogG, 0.f, 255.f, x, y, L"%.0f", 205); y += 27;
                slider(L"Fog B", g_S.fogB, 0.f, 255.f, x, y, L"%.0f", 206); y += 27;
            }
        } else if (sub == 5) {
            checkbox(L"Speedometer", g_S.speedometer, x, y, nullptr); y += 20;
            if (g_S.speedometer) {
            text(x, y, txR, txG, txB, L"Font"); y += 16;
            ensureFontList();
            bool barHov = hover(x, y, contentW - 30, 18);
            uiFill(x, y, x + contentW - 30, y + 18, (int)g_S.uiFieldR, (int)g_S.uiFieldG, (int)g_S.uiFieldB);
            wchar_t fb[64]{}; swprintf(fb, 64, L"%hs", g_S.speedometerFont.c_str());
            text(x + 6, y + 2, txR, txG, txB, fb);
            text(x + contentW - 48, y + 2, 180, 180, 180, g_FontDrop && g_FontDropOwner == 1 ? L"▲" : L"▼");
            g_FontAnchorX = x; g_FontAnchorY = y; g_FontAnchorW = contentW; g_FontDropOwner = 1;
            if (barHov && g_Click && !g_BindPtr) { g_FontDrop = !(g_FontDrop && g_FontDropOwner == 1); }
            if (button(L"+", x + contentW - 26, y, 26)) { g_FontDropOwner = 1; browseCustomFont(); }
            y += 22;
            float speedFontSize = static_cast<float>(g_S.speedometerFontSize);
            slider(L"Font size", speedFontSize, 10.f, 36.f, x, y, L"%.0f", 100);
            g_S.speedometerFontSize = static_cast<int>(speedFontSize + 0.5f); y += 28;
            checkbox(L"Show best speed", g_S.speedometerShowBest, x, y, nullptr); y += 18;
            checkbox(L"Dark backing", g_S.speedometerBackground, x, y, nullptr); y += 18;
            slider(L"Position X %", g_S.speedometerX, 0.f, 100.f, x, y, L"%.0f", 101); y += 28;
            slider(L"Position Y %", g_S.speedometerY, 0.f, 100.f, x, y, L"%.0f", 102); y += 28;
            }
        } else if (sub == 6) {
            checkbox(L"HUD accent color", g_S.hudColorMod, x, y, nullptr); y += 24;
            slider(L"R", g_S.hudColorR, 0.f, 255.f, x, y, L"%.0f", 320); y += 28;
            slider(L"G", g_S.hudColorG, 0.f, 255.f, x, y, L"%.0f", 321); y += 28;
            slider(L"B", g_S.hudColorB, 0.f, 255.f, x, y, L"%.0f", 322); y += 30;
            const int previewR = (std::max)(0, (std::min)(255, static_cast<int>(g_S.hudColorR + 0.5f)));
            const int previewG = (std::max)(0, (std::min)(255, static_cast<int>(g_S.hudColorG + 0.5f)));
            const int previewB = (std::max)(0, (std::min)(255, static_cast<int>(g_S.hudColorB + 0.5f)));
            uiFill(x, y, x + contentW, y + 28, previewR, previewG, previewB, 255);
            text(x + 8, y + 7, previewR < 110 && previewG < 110 && previewB < 110 ? 255 : 20,
                 previewR < 110 && previewG < 110 && previewB < 110 ? 255 : 20,
                 previewR < 110 && previewG < 110 && previewB < 110 ? 255 : 20,
                 L"Preview accent");
            y += 34;
        }
    } else if (tab == 2) {
        static const wchar_t* subs[] = {L"Aim", L"Bunnyhop", L"Strafe", L"Jumpbug", L"Edgebug", L"Edgejump", L"LadderSpeed", L"MicSpam", L"Overrides"};
        if (sub < 0 || sub > 8) sub = 0;
        y += subTabs(x, y, contentW, sub, subs, 9) + 4;
        if (sub == 0) {
            checkbox(L"Aimbot", g_S.aim, x, y, &g_S.kAim); y += 18;
            bindRow(L"Aim key (0=always)", &g_S.aimKey, x, y); y += 20;
            checkbox(L"Visible only", g_S.aimVisible, x, y, &g_S.kAimVisible); y += 18;
            checkbox(L"NPCs", g_S.aimNpcs, x, y, &g_S.kAimNpcs); y += 18;
            checkbox(L"Players", g_S.aimPlayers, x, y, &g_S.kAimPlayers); y += 18;
            if (g_S.aimNpcs) {
                text(x, y, 180, 180, 180, L"Exclusions"); y += 16;
                int excludedCount = 0;
                for (int i = 0; i < kAimExcludeEntryCount; ++i)
                    if (g_S.aimExcludeMask & kAimExcludeEntries[i].bit) ++excludedCount;
                wchar_t exclSummary[64]{};
                if (excludedCount == 0) swprintf(exclSummary, 64, L"None");
                else swprintf(exclSummary, 64, L"%d selected", excludedCount);
                const bool exclusionHover = hover(x, y, contentW, 18);
                uiFill(x, y, x + contentW, y + 18, (int)g_S.uiFieldR, (int)g_S.uiFieldG, (int)g_S.uiFieldB, (int)g_S.uiAlpha);
                text(x + 6, y + 2, txR, txG, txB, exclSummary);
                text(x + contentW - 17, y + 2, 180, 180, 180, g_AimNpcDrop ? L"▲" : L"▼");
                g_AimNpcAnchorX = x; g_AimNpcAnchorY = y; g_AimNpcAnchorW = contentW;
                if (exclusionHover && g_Click && !g_BindPtr) {
                    g_AimNpcDrop = !g_AimNpcDrop;
                    g_FontDrop = false;
                }
                y += 24;
            } else {
                g_AimNpcDrop = false;
            }
            text(x, y, 180, 180, 180, L"Hitboxes (multi-select)"); y += 16;
            auto boneBox = [&](int bit, const wchar_t* label, int bx, int by) {
                bool on = (g_S.aimBones & (1 << bit)) != 0;
                bool hov = hover(bx, by, 140, 16);
                uiFill(bx, by, bx + 12, by + 12, (int)g_S.uiCheckR, (int)g_S.uiCheckG, (int)g_S.uiCheckB);
                uiFill(bx + 1, by + 1, bx + 11, by + 11, (int)g_S.uiBgR, (int)g_S.uiBgG, (int)g_S.uiBgB);
                if (on) uiFill(bx + 3, by + 3, bx + 9, by + 9, (int)g_S.uiAccR, (int)g_S.uiAccG, (int)g_S.uiAccB);
                text(bx + 16, by, 220, 220, 220, label);
                if (hov && g_Click && !g_BindPtr) {
                    if (on) { int next = g_S.aimBones & ~(1 << bit); if (next == 0) next = (1 << bit); g_S.aimBones = next; }
                    else g_S.aimBones |= (1 << bit);
                }
            };
            boneBox(0, L"Head", x, y); boneBox(1, L"Chest", x + 150, y); y += 18;
            boneBox(2, L"Stomach", x, y); boneBox(3, L"Pelvis", x + 150, y); y += 18;
            boneBox(4, L"Center", x, y); y += 20;
            slider(L"FOV", g_S.aimFov, 1.f, 180.f, x, y, L"%.0f", 2); y += 32;
            slider(L"Smooth", g_S.aimSmooth, 1.f, 20.f, x, y, L"%.1f", 3); y += 30;
            checkbox(L"Auto trigger", g_S.autoTrigger, x, y, nullptr); y += 18;
            if (g_S.autoTrigger) {
                slider(L"Trigger FOV", g_S.autoTriggerFov, 0.5f, 15.f, x, y, L"%.1f", 140); y += 28;
            }
        }
        else if (sub == 1) {
            checkbox(L"Bunnyhop", g_S.bhop, x, y, nullptr); y += 22;
            if (g_S.bhop) slider(L"Success", g_S.bhopChance, 0.f, 100.f, x, y, L"%.0f%%", 4);
        } else if (sub == 2) {
            checkbox(L"Auto strafe", g_S.autoStrafe, x, y, &g_S.kAutoStrafe); y += 22;
            if (g_S.autoStrafe) {
                checkbox(L"Bidirectional", g_S.autoStrafeBidirectional, x, y, nullptr); y += 20;
                if (g_S.autoStrafeBidirectional) {
                    slider(L"Turn scale", g_S.autoStrafeTurnScale, 0.f, 1.f, x, y, L"%.1f", 70); y += 30;
                    slider(L"Max delta", g_S.autoStrafeMaxDelta, 1.f, 180.f, x, y, L"%.0f", 71); y += 30;
                }
            }
        } else if (sub == 3) {
            checkbox(L"Jumpbug", g_S.jumpbug, x, y, &g_S.kJumpbug); y += 20;
            bindRow(L"Hold key (None=always)", &g_S.jbKey, x, y); y += 22;
        } else if (sub == 4) {
            checkbox(L"Edgebug", g_S.edgebug, x, y, &g_S.kEdgebug); y += 20;
            if (g_S.edgebug) {
                slider(L"Ticks", g_S.edgebugTicks, 0.f, 128.f, x, y, L"%.0f", 110); y += 28;
                checkbox(L"Wireframe hitbox", g_S.edgebugIndicator, x, y, nullptr); y += 20;
            }
        } else if (sub == 5) {
            checkbox(L"Edgejump", g_S.edgejump, x, y, &g_S.kEdgejump); y += 22;
            if (g_S.edgejump) {
                slider(L"Minimum speed", g_S.edgejumpMinSpeed, 0.f, 250.f, x, y, L"%.0f", 113); y += 28;
                slider(L"Lookahead ticks", g_S.edgejumpLookaheadTicks, 1.f, 12.f, x, y, L"%.0f", 114);
            }
        } else if (sub == 6) {
            bindRow(L"LadderSpeed", &g_S.kLadderSpeed, x, y); y += 24;
            slider(L"Delay (ms)", g_S.ladderSpeedDelayMs, 1.f, 100.f, x, y, L"%.0f", 325); y += 28;
        } else if (sub == 7) {
            const bool oldMicSpam = g_S.micSpam;
            checkbox(L"MicSpam", g_S.micSpam, x, y, &g_S.kMicSpam);
            if (oldMicSpam != g_S.micSpam) setMicSpamEnabled(g_S.micSpam);
            y += 20;
            const bool oldLoop = g_S.voiceLoopback;
            checkbox(L"Loopback", g_S.voiceLoopback, x, y, nullptr);
            if (oldLoop != g_S.voiceLoopback) {

                dispatchMicSpamCommand(g_S.voiceLoopback ? "voice_loopback 1" : "voice_loopback 0");
            }
        } else if (sub == 8) {
            checkbox(L"sv_cheats", g_S.svCheatsOverride, x, y, nullptr); y += 18;
            checkbox(L"r_drawclipbrushes", g_S.drawClipBrushes, x, y, &g_S.kDrawClipBrushes); y += 18;
            checkbox(L"sv_propjump_enable", g_S.svPropJumpOverride, x, y, nullptr);
        }
    } else if (tab == 3) {
        text(x, y, 150, 150, 150, L"%LOCALAPPDATA%\\NOCOM"); y += 16;
        textField(L"Config name", g_NameEdit, FOCUS_CFG, x, y, contentW); y += 38;
        if (button(L"Save", x, y, 70)) { cfgMsg = saveConfig(g_NameEdit) ? L"Saved" : L"Fail"; cfgMsgUntil = GetTickCount() + 2500; }
        if (button(L"Load", x + 78, y, 70)) { cfgMsg = loadConfig(g_NameEdit) ? L"Loaded" : L"Missing"; cfgMsgUntil = GetTickCount() + 2500; }
        if (button(L"Delete", x + 156, y, 70)) { cfgMsg = deleteConfig(g_NameEdit) ? L"Deleted" : L"No file"; cfgMsgUntil = GetTickCount() + 2500; }
        y += 24;
        if (GetTickCount() < cfgMsgUntil) text(x, y, 190, 190, 190, cfgMsg);
        y += 16;
        int shown = (int)g_Cfgs.size();
        if (shown > 5) shown = 5;
        if (g_Cfgs.empty()) text(x, y, 140, 140, 140, L"No configs");
        for (int i = 0; i < shown; i++) {
            bool sel = g_Cfgs[i] == g_CfgName;
            bool hov = hover(x, y, contentW, 16);
            if (hov && g_Click && !g_BindPtr) { g_CfgName = g_Cfgs[i]; g_NameEdit = g_Cfgs[i]; g_Focus = FOCUS_NONE; }
            if (sel || hov) uiFill(x, y, x + contentW, y + 16, sel ? (int)g_S.uiSelR : (int)g_S.uiHoverR, sel ? (int)g_S.uiSelG : (int)g_S.uiHoverG, sel ? (int)g_S.uiSelB : (int)g_S.uiHoverB);
            wchar_t wn[48]{};
            toWide(g_Cfgs[i], wn, 40);
            text(x + 6, y + 1, 230, 230, 230, wn);
            y += 18;
        }
    } else if (tab == 4) {
        static const wchar_t* subs[] = {L"Chrome", L"Widgets", L"GUI", L"Key", L"Misc", L"Performance", L"Network", L"Reset"};
        if (sub < 0 || sub > 7) sub = 0;
        y += subTabs(x, y, contentW, sub, subs, 8) + 4;
        if (sub == 0) {
            static int part = 0;
            static const wchar_t* parts[] = {L"Bg", L"Side", L"Text", L"Sel", L"Acc"};
            y += subTabs(x, y, contentW, part, parts, 5) + 2;
            float *r = &g_S.uiBgR, *g = &g_S.uiBgG, *b = &g_S.uiBgB;
            if (part == 1) { r = &g_S.uiSideR; g = &g_S.uiSideG; b = &g_S.uiSideB; }
            else if (part == 2) { r = &g_S.uiTextR; g = &g_S.uiTextG; b = &g_S.uiTextB; }
            else if (part == 3) { r = &g_S.uiSelR; g = &g_S.uiSelG; b = &g_S.uiSelB; }
            else if (part == 4) { r = &g_S.uiAccR; g = &g_S.uiAccG; b = &g_S.uiAccB; }
            slider(L"R", *r, 0.f, 255.f, x, y, L"%.0f", 30); y += 32;
            slider(L"G", *g, 0.f, 255.f, x, y, L"%.0f", 31); y += 32;
            slider(L"B", *b, 0.f, 255.f, x, y, L"%.0f", 32);
        } else if (sub == 1) {
            static int part = 0;
            static const wchar_t* parts[] = {L"Hover", L"Btn", L"Field", L"Check", L"Close"};
            y += subTabs(x, y, contentW, part, parts, 5) + 2;
            float *r = &g_S.uiHoverR, *g = &g_S.uiHoverG, *b = &g_S.uiHoverB;
            if (part == 1) { r = &g_S.uiBtnR; g = &g_S.uiBtnG; b = &g_S.uiBtnB; }
            else if (part == 2) { r = &g_S.uiFieldR; g = &g_S.uiFieldG; b = &g_S.uiFieldB; }
            else if (part == 3) { r = &g_S.uiCheckR; g = &g_S.uiCheckG; b = &g_S.uiCheckB; }
            else if (part == 4) { r = &g_S.uiCloseR; g = &g_S.uiCloseG; b = &g_S.uiCloseB; }
            slider(L"R", *r, 0.f, 255.f, x, y, L"%.0f", 40); y += 32;
            slider(L"G", *g, 0.f, 255.f, x, y, L"%.0f", 41); y += 32;
            slider(L"B", *b, 0.f, 255.f, x, y, L"%.0f", 42);
        } else if (sub == 2) {
            ensureFontList();
            text(x, y, txR, txG, txB, L"UI font"); y += 16;
            bool barHov = hover(x, y, contentW - 30, 18);
            uiFill(x, y, x + contentW - 30, y + 18, (int)g_S.uiFieldR, (int)g_S.uiFieldG, (int)g_S.uiFieldB);
            wchar_t fb[64];
            swprintf(fb, 64, L"%hs", g_UiFont.c_str());
            text(x + 6, y + 2, txR, txG, txB, fb);
            text(x + contentW - 48, y + 2, 180, 180, 180, g_FontDrop ? L"▲" : L"▼");
            g_FontAnchorX = x;
            g_FontAnchorY = y;
            g_FontAnchorW = contentW;
            g_FontDropOwner = 0;
            if (barHov && g_Click && !g_BindPtr) { g_FontDrop = !(g_FontDrop && g_FontDropOwner == 0); }
            if (button(L"+", x + contentW - 26, y, 26)) browseCustomFont();
            y += 22;
            slider(L"GUI alpha", g_S.uiAlpha, 40.f, 255.f, x, y, L"%.0f", 60); y += 32;
            checkbox(L"Frosted panels", g_S.uiBlur, x, y, nullptr); y += 18;
            checkbox(L"Bevels", g_S.uiBevel, x, y, nullptr); y += 18;
            checkbox(L"Debug mode", g_S.debugMode, x, y, nullptr); y += 18;
        } else if (sub == 3) {
            bindRow(L"Menu key", &g_S.menuKey, x, y);
        } else if (sub == 4) {
            ensureGameSoundCatalog();
            checkbox(L"Death sound", g_S.deathSound, x, y, nullptr); y += 18;
            if (g_S.deathSound) {
                slider(L"Volume %", g_S.deathSoundVolume, 0.f, 100.f, x, y, L"%.0f", 151); y += 28;
                text(x, y, 180, 180, 180, L"Game sound list"); y += 14;
                bool barHov = hover(x, y, contentW - 36, 18);
                uiFill(x, y, x + contentW - 36, y + 18, (int)g_S.uiFieldR, (int)g_S.uiFieldG, (int)g_S.uiFieldB);
                wchar_t cur[96]{};
                std::string soundPathLabel = g_S.deathSoundPath;
                if (soundPathLabel.size() > 92)
                    soundPathLabel = "..." + soundPathLabel.substr(soundPathLabel.size() - 89);
                toWide(soundPathLabel, cur, 96);
                if (!cur[0]) wcscpy(cur, L"(none)");
                text(x + 6, y + 2, 220, 220, 220, cur);
                text(x + contentW - 56, y + 2, 180, 180, 180, L"▼");
                g_SoundAnchorX = x; g_SoundAnchorY = y; g_SoundAnchorW = contentW - 36;
                if (barHov && g_Click && !g_BindPtr) {
                    g_SoundDrop = !g_SoundDrop;
                    const int idx = findGameSoundIndex(g_S.deathSoundPath);
                    g_SoundDropScroll = idx > 0 ? (std::max)(0, idx - 2) : 0;
                }
                if (button(L"+", x + contentW - 28, y, 28)) {
                    browseSoundFile(g_S.deathSoundPath);
                    g_SoundDrop = false;
                }
                y += 22;
                if (button(L"Preview", x, y, 70)) previewNocomSound(g_S.deathSoundPath, static_cast<int>(g_S.deathSoundVolume));
                y += 22;
            }
            y += 8;
            checkbox(L"Discord RPC", g_S.discordRpc, x, y, nullptr); y += 20;
            if (g_S.discordRpc) {
                checkbox(L"Share server address", g_S.discordShowServer, x, y, nullptr); y += 20;
            }
        } else if (sub == 5) {
            const bool wasPri = g_S.processHighPriority;
            const bool wasAbove = g_S.processAboveNormal;
            const bool wasAff = g_S.processAffinityFastCores;
            const bool wasThrottle = g_S.processDisablePowerThrottle;
            const bool wasTimer = g_S.processHighResTimer;
            const bool wasBoost = g_S.processMainThreadBoost;
            const bool wasIdle = g_S.processIdleWhenUnfocused;
            checkbox(L"High priority", g_S.processHighPriority, x, y, nullptr); y += 18;
            if (g_S.processHighPriority) g_S.processAboveNormal = false;
            checkbox(L"Above normal priority", g_S.processAboveNormal, x, y, nullptr); y += 18;
            if (g_S.processAboveNormal) g_S.processHighPriority = false;
            checkbox(L"Prefer fast CPU cores", g_S.processAffinityFastCores, x, y, nullptr); y += 18;
            checkbox(L"Disable power throttling", g_S.processDisablePowerThrottle, x, y, nullptr); y += 18;
            checkbox(L"High-res timer (1ms)", g_S.processHighResTimer, x, y, nullptr); y += 18;
            checkbox(L"Boost main thread", g_S.processMainThreadBoost, x, y, nullptr); y += 18;
            checkbox(L"Idle priority when unfocused", g_S.processIdleWhenUnfocused, x, y, nullptr); y += 18;
            checkbox(L"Threaded bone setup", g_S.engineThreadedBones, x, y, nullptr); y += 18;
            checkbox(L"Threaded particles", g_S.engineThreadedParticles, x, y, nullptr); y += 18;
            checkbox(L"Material queue mode", g_S.engineMatQueueMode, x, y, nullptr); y += 18;
            checkbox(L"Multi-core rendering", g_S.engineMultiCore, x, y, nullptr); y += 20;
            slider(L"ESP update (ms)", g_S.espUpdateIntervalMs, 16.f, 150.f, x, y, L"%.0f", 210); y += 26;
            slider(L"Aim scan (ms)", g_S.aimScanIntervalMs, 16.f, 200.f, x, y, L"%.0f", 211); y += 26;
            slider(L"Trajectory budget", g_S.trajectoryBudget, 1.f, 4.f, x, y, L"%.0f", 212); y += 26;
            if (wasPri != g_S.processHighPriority || wasAbove != g_S.processAboveNormal ||
                wasAff != g_S.processAffinityFastCores || wasThrottle != g_S.processDisablePowerThrottle ||
                wasTimer != g_S.processHighResTimer || wasBoost != g_S.processMainThreadBoost ||
                wasIdle != g_S.processIdleWhenUnfocused)
                applyGameProcessScheduling();

            applyGameProcessScheduling();
        } else if (sub == 6) {
            static int networkSub = 0;
            static const wchar_t* networkSubs[] = {L"Rates", L"Prediction", L"QoS", L"Advanced"};
            if (networkSub < 0 || networkSub > 3) networkSub = 0;
            y += subTabs(x, y, contentW, networkSub, networkSubs, 4) + 4;

            if (networkSub == 0) {
                checkbox(L"Enable network overrides", g_S.networkSettingsEnabled, x, y, nullptr); y += 20;
                static const wchar_t* profiles[] = {L"Custom", L"Low latency", L"Balanced", L"Loss tolerant", L"LAN"};
                const int previousProfile = g_S.networkPreset;
                selector(L"Network profile", g_S.networkPreset, profiles, 5, x, y); y += 22;
                if (g_S.networkPreset != previousProfile)
                    applyNetworkPresetToSettings(g_S.networkPreset);
                slider(L"Rate (bytes/s)", g_S.networkRate, 20480.f, 786432.f, x, y, L"%.0f", 310); y += 28;
                slider(L"Command rate", g_S.networkCmdRate, 10.f, 128.f, x, y, L"%.0f", 311); y += 28;
                slider(L"Update rate", g_S.networkUpdateRate, 10.f, 128.f, x, y, L"%.0f", 312); y += 28;
                slider(L"Interpolation ratio", g_S.networkInterpRatio, 1.f, 5.f, x, y, L"%.1f", 313); y += 28;
                slider(L"Interpolation (s)", g_S.networkInterp, 0.f, 0.5f, x, y, L"%.3f", 314); y += 28;
            } else if (networkSub == 1) {
                checkbox(L"Client prediction", g_S.networkPredict, x, y, nullptr); y += 20;
                checkbox(L"Predict weapons", g_S.networkPredictWeapons, x, y, nullptr); y += 20;
                checkbox(L"Lag compensation", g_S.networkLagCompensation, x, y, nullptr); y += 20;
                checkbox(L"Interpolate entities", g_S.networkInterpolate, x, y, nullptr); y += 20;
                checkbox(L"Smooth prediction errors", g_S.networkSmooth, x, y, nullptr); y += 20;
                slider(L"Smoothing time (s)", g_S.networkSmoothTime, 0.f, 2.f, x, y, L"%.2f", 315); y += 28;
                slider(L"NPC interpolation (s)", g_S.networkInterpNpcs, 0.f, 0.5f, x, y, L"%.2f", 316); y += 28;
                static const wchar_t* optimizeModes[] = {L"0", L"1", L"2"};
                int optimizeMode = static_cast<int>(g_S.networkPredOptimize + 0.5f);
                selector(L"Prediction optimize", optimizeMode, optimizeModes, 3, x, y);
                g_S.networkPredOptimize = static_cast<float>(optimizeMode);
                y += 22;
            } else if (networkSub == 2) {
                static const wchar_t* qosModes[] = {L"Custom", L"Low latency", L"Balanced", L"Loss tolerant"};
                const int previousQos = g_S.networkQosMode;
                selector(L"QoS profile", g_S.networkQosMode, qosModes, 4, x, y); y += 22;
                if (g_S.networkQosMode != previousQos)
                    applyNetworkQosPresetToSettings(g_S.networkQosMode);
                checkbox(L"Compress packets", g_S.networkCompressPackets, x, y, nullptr); y += 20;
                slider(L"Compression min size", g_S.networkCompressMinSize, 64.f, 2048.f, x, y, L"%.0f", 317); y += 28;
                checkbox(L"Queued packet thread", g_S.networkQueuedPacketThread, x, y, nullptr); y += 20;
                slider(L"Split rate", g_S.networkSplitRate, 1.f, 8.f, x, y, L"%.0f", 318); y += 28;
                slider(L"Split packet max rate", g_S.networkSplitPacketMaxRate, 10000.f, 786432.f, x, y, L"%.0f", 319); y += 28;
                slider(L"Max routable bytes", g_S.networkMaxRoutable, 576.f, 1500.f, x, y, L"%.0f", 320); y += 28;
                slider(L"Max fragments", g_S.networkMaxFragments, 576.f, 4096.f, x, y, L"%.0f", 321); y += 28;
                slider(L"Max clear time (s)", g_S.networkMaxClearTime, 0.f, 1.f, x, y, L"%.3f", 322); y += 28;
            } else {
                checkbox(L"Net graph", g_S.networkNetGraph, x, y, nullptr); y += 20;
                static const wchar_t* graphPositions[] = {L"Left", L"Right", L"Center"};
                selector(L"Net graph position", g_S.networkNetGraphPos, graphPositions, 3, x, y); y += 22;
                checkbox(L"Net graph proportional font", g_S.networkNetGraphProportionalFont, x, y, nullptr); y += 20;
                slider(L"Client timeout (s)", g_S.networkTimeout, 15.f, 180.f, x, y, L"%.0f", 323); y += 28;
                slider(L"Max packet drop", g_S.networkMaxPacketDrop, 0.f, 4000.f, x, y, L"%.0f", 324); y += 28;
            }
        } else {
            static int confirmReset = 0;
            if (confirmReset == 0) {
                if (button(L"Reset GUI settings", x, y, 150)) confirmReset = 1;
                y += 22;
                if (button(L"Reset ALL settings", x, y, 150)) confirmReset = 2;
            } else {
                text(x, y, 220, 140, 80, confirmReset == 1 ? L"Reset GUI only — confirm?" : L"Reset EVERYTHING — confirm?");
                y += 18;
                if (button(L"Yes", x, y, 60)) {
                    if (confirmReset == 1) resetGuiSettings();
                    else resetAllSettings();
                    confirmReset = 0;
                }
                if (button(L"No", x + 70, y, 60)) confirmReset = 0;
            }
        }
        } else if (tab == 5) {
        text(x, y, txR, txG, txB, L"NOCOM is a C++ plugin for Synergy that"); y += 18;
        text(x, y, txR, txG, txB, L"provides to you various"); y += 18;
        int cheatW = 0, cheatH = 0;
        measureText(g_Font, L"cheats", cheatW, cheatH);
        text(x, y, txR, txG, txB, L"cheats");
        line(x, y + 7, x + cheatW, y + 7);
        text(x + cheatW + 4, y, txR, txG, txB, L" utilities and gameplay enhances"); y += 28;
        text(x, y, 200, 200, 200, L"Imagine cheating in a co-op game =)"); y += 20;
    } else if (tab == 6) {
        wchar_t lines[36][128]{};
        const int lineCount = getHookDebugLines(lines, 36);
        for (int i = 0; i < lineCount; ++i) {
            text(x, y, 180, 190, 190, lines[i]);
            y += 12;
        }
        NetStats ns{};
        queryNetStats(ns);
        wchar_t line[160];
        swprintf(line, 160, L"map %hs", ns.level[0] ? ns.level : "?");
        text(x, y, 180, 180, 180, line); y += 12;
        swprintf(line, 160, L"server %hs", ns.address[0] ? ns.address : "(address unavailable)");
        text(x, y, 180, 180, 180, line); y += 12;
        swprintf(line, 160, L"ping %.0fms loss %.1f%% choke %.1f%%",
            ns.pingMs, ns.loss * 100.f, ns.choke * 100.f);
        text(x, y, 180, 180, 180, line); y += 12;
        swprintf(line, 160, L"font=%lu local=%d hp=%d flags=%d vel=%d", g_Font, LocalIndex(), o_Health, o_Flags, o_Velocity);
        text(x, y, 180, 180, 180, line); y += 12;
        text(x, y, 180, 180, 180, discord::Ready() ? L"Discord connected" : L"Discord idle");
    }

    if (g_Click && !g_FocusHit) g_Focus = FOCUS_NONE;

    drawFontDropdownOverlay();
    drawAimNpcDropdownOverlay();
    drawSoundDropdownOverlay();
    if (g_BindPtr) text(mx + kSide + 10, my + kMenuH - 18, 180, 180, 180, L"Press key or mouse button   Esc cancel   Del clear");
    applyNetworkSettings();
}

void pollMenuHotkey(bool allowEngineCommands) {

    static bool previousMenuKeyHeld = false;
    static bool wasForeground = false;
    static LONG lastFocusEpoch = 0;
    static DWORD lastPollTick = 0;
    const DWORD pollNow = GetTickCount();
    if (lastPollTick && pollNow - lastPollTick < 4u) return;
    lastPollTick = pollNow;

    if (allowEngineCommands) {
        const LONG pendingMouse = InterlockedExchange(&g_PendingMenuMouseEnable, -1);
        if (pendingMouse >= 0 && g_Engine) {
            VCall<void, const char*>(g_Engine, 7,
                pendingMouse ? "cl_mouseenable 1" : "cl_mouseenable 0");
        }
        if (g_PendingMenuConfigRefresh) {
            g_PendingMenuConfigRefresh = false;
            if (g_MenuOpen) refreshCfgs();
        }
    }

    HWND game = resolveGameWindow();
    if (game) installMenuInputHook(game);

    const int menuKey = (g_S.menuKey > 0 && g_S.menuKey < 256) ? g_S.menuKey : VK_INSERT;
    const bool menuKeyHeld = (GetAsyncKeyState(menuKey) & 0x8000) != 0;
    const LONG focusEpoch = InterlockedCompareExchange(&g_WindowFocusEpoch, 0, 0);
    if (focusEpoch != lastFocusEpoch) {
        lastFocusEpoch = focusEpoch;
        previousMenuKeyHeld = menuKeyHeld;
        wasForeground = false;
        resetGameplayBindEdges();
    }
    HWND foregroundWindow = GetForegroundWindow();
    const bool foreground = game && foregroundWindow &&
        (foregroundWindow == game || IsChild(game, foregroundWindow) ||
         GetAncestor(foregroundWindow, GA_ROOT) == game);

    if (!foreground) {
        if (wasForeground) resetGameplayBindEdges();
        previousMenuKeyHeld = menuKeyHeld;
        wasForeground = false;
        if (g_BindPtr) cancelBindCapture();
        if (g_MenuOpen) {
            g_MenuOpen = false;
            g_Focus = FOCUS_NONE;
            g_Menu.drag = false;
            g_ActiveSlider = -1;
            g_CmdReset = true;
            setMenuMouseEnabled(true, allowEngineCommands);
            g_PendingMenuConfigRefresh = false;
        }
        return;
    }

    if (!wasForeground) {

        resetGameplayBindEdges();
        wasForeground = true;
    }

    if (menuKeyHeld && !previousMenuKeyHeld) {
        g_MenuOpen = !g_MenuOpen;
        g_CmdReset = true;
        cancelBindCapture();
        g_Focus = FOCUS_NONE;
        setMenuMouseEnabled(!g_MenuOpen, allowEngineCommands);
        if (g_MenuOpen) {
            if (allowEngineCommands) refreshCfgs();
            else g_PendingMenuConfigRefresh = true;
        } else {
            g_PendingMenuConfigRefresh = false;
        }
    }
    previousMenuKeyHeld = menuKeyHeld;
}

void updateMenuInput(int sw, int sh, bool inGame) {
    static bool prevDown;
    HWND game = resolveGameWindow();
    if (game) installMenuInputHook(game);
    const bool focus = isGameWindowForeground();
    updateMicSpamLifecycle(inGame, focus);
    if (!focus) {
        resetGameplayBindEdges();
        cancelBindCapture();
        if (g_MenuOpen) {
            g_MenuOpen = false;
            g_Focus = FOCUS_NONE;
            g_Menu.drag = false;
            g_ActiveSlider = -1;
            g_CmdReset = true;
            if (g_Engine) VCall<void, const char*>(g_Engine, 7, "cl_mouseenable 1");
        }
        g_Down = g_Click = false;
        prevDown = false;
        return;
    }

    if (!g_MenuOpen) pollBinds();
    else resetGameplayBindEdges();
    if (!g_MenuOpen) {
        g_Down = g_Click = false;
        g_ActiveSlider = -1;
        return;
    }
    POINT pt{};
    RECT rc{};
    GetCursorPos(&pt);
    if (game) {
        ScreenToClient(game, &pt);
        GetClientRect(game, &rc);
    }
    if (rc.right > 0 && rc.bottom > 0) {
        g_Mx = pt.x * sw / rc.right;
        g_My = pt.y * sh / rc.bottom;
    }
    g_Down = focus && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
    g_Click = g_Down && !prevDown;
    prevDown = g_Down;

    if (g_BindPtr && !g_BindArm && g_Click) {

        const bool clickedOutside = !hover(g_Menu.x, g_Menu.y, kMenuW, kMenuH);
        cancelBindCapture();
        g_Click = false;
        if (clickedOutside) {
            g_MenuOpen = false;
            g_Focus = FOCUS_NONE;
            g_Menu.drag = false;
            g_ActiveSlider = -1;
            g_CmdReset = true;
            if (g_Engine) VCall<void, const char*>(g_Engine, 7, "cl_mouseenable 1");
            prevDown = false;
            g_Down = false;
            return;
        }
    }

    if (g_Focus != FOCUS_NONE) pollText();
    else pollBindCapture();
}

void cleanupMenuResources() {
    if (g_InputSubclassWindow && g_InputOriginalProc && IsWindow(g_InputSubclassWindow) &&
        reinterpret_cast<WNDPROC>(GetWindowLongPtrA(g_InputSubclassWindow, GWLP_WNDPROC)) == nocomInputWndProc) {
        SetWindowLongPtrA(g_InputSubclassWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_InputOriginalProc));
    }
    g_InputSubclassWindow = nullptr;
    g_InputOriginalProc = nullptr;
    clearExtraMouseButtonEvents();
    for (const std::string& file : g_PrivateFontFiles)
        RemoveFontResourceExA(file.c_str(), FR_PRIVATE, nullptr);
    g_PrivateFontFiles.clear();
    g_FontDrop = false;
    cancelBindCapture();
    g_Focus = FOCUS_NONE;
    g_ActiveSlider = -1;
    g_Menu.drag = false;
}
