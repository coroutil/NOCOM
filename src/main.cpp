#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "sdk.h"
#include "drm.h"
#include "netvar.h"
#include "discord.h"
#include "settings.h"
#include "game.h"
#include "config.h"
#include "render.h"
#include "menu.h"
#include "features.h"
#include "hooking.h"
#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstdarg>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <string>
#include <dbghelp.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dbghelp.lib")

using CreateMoveFn = bool(__fastcall*)(void*, void*, float, CUserCmd*);
CreateMoveFn o_CreateMove;
using KeyInputFn = int(__fastcall*)(void*, void*, int, int, const char*);
static KeyInputFn o_KeyInput = nullptr;

using OverrideViewFn = void(__fastcall*)(void*, void*, void*);
OverrideViewFn o_OverrideView;
using GetViewModelFOVFn = float(__fastcall*)(void*, void*);
static GetViewModelFOVFn o_GetViewModelFOV = nullptr;

using PaintTraverseFn = void(__fastcall*)(void*, void*, unsigned, bool, bool);
PaintTraverseFn o_PaintTraverse;

struct ModelRenderInfoHook {
    Vec3 origin;
    QAngle angles;
    void* renderable;
    const void* model;
    const Matrix3x4* modelToWorld;
    const Matrix3x4* lightingOffset;
    const Vec3* lightingOrigin;
    int flags;
    int entityIndex;
    int skin;
    int body;
    int hitboxSet;
    unsigned short instance;
};
// x86 ABI bridge: ECX is this; EDX is a dummy argument

using DrawModelExecuteFn = void(__fastcall*)(void*, void*, const void*, const ModelRenderInfoHook&, Matrix3x4*);
DrawModelExecuteFn o_DrawModelExecute = nullptr;

static DWORD WINAPI ejectThread(LPVOID);

static bool copyReadableString(const char* source, char* destination, size_t capacity) {
    if (!destination || capacity == 0) return false;
    destination[0] = '\0';
    if (!source || capacity < 2) return false;

    const uintptr_t sourceAddress = reinterpret_cast<uintptr_t>(source);
    size_t copied = 0;
    while (copied < capacity - 1) {
        const uintptr_t address = sourceAddress + copied;
        if (address < sourceAddress) {
            destination[0] = '\0';
            return false;
        }

        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) ||
            mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            destination[0] = '\0';
            return false;
        }

        const DWORD protection = mbi.Protect & 0xFF;
        switch (protection) {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            break;
        default:
            destination[0] = '\0';
            return false;
        }

        const uintptr_t regionBegin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t regionEnd = regionBegin + mbi.RegionSize;
        if (regionEnd <= address || address < regionBegin) {
            destination[0] = '\0';
            return false;
        }

        size_t available = static_cast<size_t>(regionEnd - address);
        const size_t room = capacity - 1 - copied;
        if (available > room) available = room;
        bool terminated = false;
        __try {
            for (size_t i = 0; i < available; ++i) {
                const char ch = source[copied + i];
                destination[copied + i] = ch;
                if (ch == '\0') {
                    terminated = true;
                    break;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            destination[0] = '\0';
            return false;
        }
        if (terminated) return true;
        copied += available;
    }

    destination[0] = '\0';
    return false;
}

using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using SetCursorFn = HCURSOR(WINAPI*)(HCURSOR);
static SetCursorPosFn o_SetCursorPos;
static ClipCursorFn o_ClipCursor;
static SetCursorFn o_SetCursor;
static int g_SetCursorIatPatches = 0;
static int g_SetCursorPosIatPatches = 0;
static int g_ClipCursorIatPatches = 0;

static volatile bool g_HooksReady = false;
static volatile bool g_CleaningUp = false;
static bool g_MoveTypeHookReady = false;
static ShadowVTable g_ClientModeHooks;
static ShadowVTable g_PanelHooks;
static ShadowVTable g_ModelRenderHooks;
static ShadowVTable g_SurfaceHooks;  
using SurfaceDrawColorFn = void(__thiscall*)(void*, int, int, int, int);
static SurfaceDrawColorFn o_SurfaceDrawSetColor = nullptr;
static SurfaceDrawColorFn o_SurfaceDrawSetTextColor = nullptr;
static thread_local int g_HudColorPaintDepth = 0;
static thread_local bool g_NocomOverlayDrawing = false;
static DWORD g_HudColorHookLastAttempt = 0;
static volatile LONG g_HudColorPanelCacheEpoch = 0;
static void* g_ModelRender = nullptr;
static uintptr_t g_ClientModeSourceOffset = 0;
static volatile LONG g_MapTransitionQuiesceUntil = 0;
static volatile LONG g_PendingFogRestore = 0;
static volatile LONG g_PendingUiRefresh = 0;

static bool isMapTransitionQuiescing() {
    const DWORD until = static_cast<DWORD>(InterlockedCompareExchange(&g_MapTransitionQuiesceUntil, 0, 0));
    return until != 0 && static_cast<LONG>(until - GetTickCount()) > 0;
}

static void beginMapTransitionQuiesce(DWORD durationMs) {

    InterlockedIncrement(&g_HudColorPanelCacheEpoch);
    InterlockedExchange(&g_MapTransitionQuiesceUntil,
        static_cast<LONG>(GetTickCount() + durationMs));
}

static int g_OriginalFullBright = 0;
static bool g_OriginalFullBrightValid = false;
static int g_LastAppliedFullBright = -1;
static DWORD g_LastFullBrightCvarAttempt = 0;
static int g_OriginalSvCheats = 0;
static bool g_OriginalSvCheatsValid = false;
static DWORD g_LastSvCheatsCvarAttempt = 0;
static char g_OriginalPropJumpEnable[128] = {};
static char g_LastAppliedPropJumpEnable[128] = {};
static bool g_OriginalPropJumpEnableValid = false;
static DWORD g_LastPropJumpEnableCvarAttempt = 0;
static int g_OriginalDrawClipBrushes = 0;
static bool g_OriginalDrawClipBrushesValid = false;
static int g_LastAppliedDrawClipBrushes = -1;
static DWORD g_LastDrawClipBrushesCvarAttempt = 0;
static volatile LONG g_ViewmodelFovHookCalls = 0;
static volatile LONG g_ViewmodelFovHookWrites = 0;
static volatile LONG g_ClientCvarWriteFailures = 0;
static volatile float g_LastViewmodelFovWritten = 0.0f;

static volatile LONG g_ViewmodelFovSetupWrites = 0;

enum LogLevel { LOG_INFO = 0, LOG_WARN = 1, LOG_ERROR = 2, LOG_CRASH = 3 };

static CRITICAL_SECTION g_LogCs;
static bool g_LogCsInit = false;
static volatile LONG g_LogSeq = 0;
static char g_LogPath[MAX_PATH] = {};
static char g_LogBasePath[MAX_PATH] = {};
static unsigned g_LogCurrentPart = 0;
static constexpr ULONGLONG kAutoCompressThreshold = 8ull * 1024ull * 1024ull;

static void logEnsureCs() {
    if (!g_LogCsInit) {
        InitializeCriticalSection(&g_LogCs);
        g_LogCsInit = true;
    }
}

static const char* logLevelName(LogLevel level) {
    switch (level) {
    case LOG_WARN: return "WARN";
    case LOG_ERROR: return "ERROR";
    case LOG_CRASH: return "CRASH";
    default: return "INFO";
    }
}

static bool fileIsAtLeast(const char* path, ULONGLONG minimumBytes) {
    if (!path || !*path) return false;
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    const ULONGLONG size = (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) |
        static_cast<ULONGLONG>(data.nFileSizeLow);
    return size >= minimumBytes;
}

static void escapePowerShellLiteral(const char* input, char* output, size_t capacity) {
    if (!output || capacity == 0) return;
    output[0] = '\0';
    if (!input) return;
    size_t j = 0;
    for (size_t i = 0; input[i] && j + 2 < capacity; ++i) {
        if (input[i] == '\'') output[j++] = '\'';
        output[j++] = input[i];
    }
    output[j] = '\0';
}

static bool queueZipCompression(const char* sourcePath) {
    if (!sourcePath || !*sourcePath) return false;
    char zipPath[MAX_PATH]{};
    if (strnlen_s(sourcePath, MAX_PATH) + 5 >= MAX_PATH) return false;
    _snprintf_s(zipPath, sizeof(zipPath), _TRUNCATE, "%s.zip", sourcePath);

    char psSource[MAX_PATH * 2]{};
    char psZip[MAX_PATH * 2]{};
    escapePowerShellLiteral(sourcePath, psSource, sizeof(psSource));
    escapePowerShellLiteral(zipPath, psZip, sizeof(psZip));

    char script[8192]{};
    _snprintf_s(script, sizeof(script), _TRUNCATE,
        "& { $src = '%s'; $dst = '%s'; try { "
        "Compress-Archive -LiteralPath $src -DestinationPath $dst -CompressionLevel Optimal -Force -ErrorAction Stop; "
        "if ((Test-Path -LiteralPath $dst) -and ((Get-Item -LiteralPath $dst).Length -gt 0)) "
        "{ Remove-Item -LiteralPath $src -Force -ErrorAction Stop; exit 0 } else { exit 2 } "
        "} catch { exit 1 } }", psSource, psZip);

    char commandLine[12000]{};
    _snprintf_s(commandLine, sizeof(commandLine), _TRUNCATE,
        "powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command \"%s\"",
        script);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessA(nullptr, commandLine, nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (!created) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

static bool queueZipCompressionIfLarge(const char* path) {
    return fileIsAtLeast(path, kAutoCompressThreshold) && queueZipCompression(path);
}

static void makeLogPartPath(const char* basePath, unsigned part, char* output, size_t capacity) {
    if (!output || capacity == 0) return;
    output[0] = '\0';
    if (!basePath) return;
    const size_t length = strlen(basePath);
    const bool hasLogSuffix = length >= 4 && _stricmp(basePath + length - 4, ".log") == 0;
    const size_t stemLength = hasLogSuffix ? length - 4 : length;
    char stem[MAX_PATH]{};
    const size_t copyLength = stemLength < sizeof(stem) - 1 ? stemLength : sizeof(stem) - 1;
    memcpy(stem, basePath, copyLength);
    stem[copyLength] = '\0';
    _snprintf_s(output, capacity, _TRUNCATE, "%s_part%02u.log", stem, part);
}

static void rotateLogIfLarge(const char* writtenPath) {
    if (!writtenPath || !g_LogBasePath[0] || !fileIsAtLeast(writtenPath, kAutoCompressThreshold))
        return;
    if (_stricmp(writtenPath, g_LogPath) != 0) return;

    const unsigned archivedPart = g_LogCurrentPart == 0 ? 1 : g_LogCurrentPart;
    char archivedPath[MAX_PATH]{};
    makeLogPartPath(g_LogBasePath, archivedPart, archivedPath, sizeof(archivedPath));

    if (g_LogCurrentPart == 0) {
        if (!MoveFileExA(writtenPath, archivedPath, MOVEFILE_REPLACE_EXISTING)) return;
    } else if (_stricmp(writtenPath, archivedPath) != 0) {
        if (!MoveFileExA(writtenPath, archivedPath, MOVEFILE_REPLACE_EXISTING)) return;
    }

    g_LogCurrentPart = archivedPart + 1;
    makeLogPartPath(g_LogBasePath, g_LogCurrentPart, g_LogPath, sizeof(g_LogPath));
    queueZipCompression(archivedPath);
}

static const char* logSessionPath() {
    if (g_LogPath[0]) return g_LogPath;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    const std::string dir = cfgDir();
    _snprintf_s(g_LogBasePath, sizeof(g_LogBasePath), _TRUNCATE,
        "%s\\NOCOM_%04u%02u%02u_%02u%02u%02u.log",
        dir.c_str(),
        static_cast<unsigned>(st.wYear), static_cast<unsigned>(st.wMonth),
        static_cast<unsigned>(st.wDay), static_cast<unsigned>(st.wHour),
        static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond));
    strcpy_s(g_LogPath, sizeof(g_LogPath), g_LogBasePath);
    g_LogCurrentPart = 0;
    return g_LogPath;
}

static void logEx(LogLevel level, const char* fmt, ...) {
    if (!fmt) return;
    logEnsureCs();
    EnterCriticalSection(&g_LogCs);

    char body[1024]{};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    char writtenPath[MAX_PATH]{};
    strcpy_s(writtenPath, sizeof(writtenPath), logSessionPath());
    FILE* f = nullptr;
    bool wroteLine = false;
    if (fopen_s(&f, writtenPath, "a") == 0 && f) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        const LONG seq = InterlockedIncrement(&g_LogSeq);
        fprintf(f, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] #%ld [%s] %s\n",
            static_cast<unsigned>(st.wYear), static_cast<unsigned>(st.wMonth),
            static_cast<unsigned>(st.wDay), static_cast<unsigned>(st.wHour),
            static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond),
            static_cast<unsigned>(st.wMilliseconds), static_cast<long>(seq),
            logLevelName(level), body);
        fflush(f);
        fclose(f);
        wroteLine = true;
    }
    if (wroteLine) rotateLogIfLarge(writtenPath);

    LeaveCriticalSection(&g_LogCs);
}

static void logNocom(const char* message) {
    logEx(LOG_INFO, "%s", message ? message : "(null)");
}

static void logModuleBase(const char* label, HMODULE mod) {
    if (!mod) {
        logEx(LOG_WARN, "%s: module not loaded", label ? label : "module");
        return;
    }
    auto base = reinterpret_cast<uint8_t*>(mod);
    unsigned imageSize = 0;
    __try {
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0) {
            auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE)
                imageSize = nt->OptionalHeader.SizeOfImage;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        imageSize = 0;
    }
    char path[MAX_PATH]{};
    GetModuleFileNameA(mod, path, MAX_PATH);
    logEx(LOG_INFO, "%s base=%p size=0x%X path=%s",
        label ? label : "module", reinterpret_cast<void*>(mod), imageSize, path[0] ? path : "?");
}

static const char* exceptionCodeName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
    case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
    case EXCEPTION_SINGLE_STEP: return "SINGLE_STEP";
    case 0xE06D7363: return "CPP_EXCEPTION";
    default: return "UNKNOWN";
    }
}

static void logStackTrace(CONTEXT* ctx) {
    if (!ctx) return;

    CONTEXT walkContext{};
    memcpy(&walkContext, ctx, sizeof(walkContext));
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (!SymInitialize(process, nullptr, TRUE)) {
        logEx(LOG_CRASH, "SymInitialize failed err=%lu", GetLastError());
        return;
    }

    STACKFRAME64 frame{};
    DWORD machine = IMAGE_FILE_MACHINE_I386;
#if defined(_M_IX86)
    frame.AddrPC.Offset = walkContext.Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = walkContext.Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = walkContext.Esp;
    frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_X64)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = walkContext.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = walkContext.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = walkContext.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
#else
    SymCleanup(process);
    return;
#endif

    for (int i = 0; i < 32; ++i) {
        if (!StackWalk64(machine, process, thread, &frame, &walkContext, nullptr,
                SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        if (frame.AddrPC.Offset == 0) break;

        char symbolBuf[sizeof(SYMBOL_INFO) + 256]{};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(symbolBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 displacement = 0;
        const char* name = "???";
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, sym))
            name = sym->Name;

        IMAGEHLP_MODULE64 modInfo{};
        modInfo.SizeOfStruct = sizeof(modInfo);
        const char* modName = "?";
        if (SymGetModuleInfo64(process, frame.AddrPC.Offset, &modInfo))
            modName = modInfo.ModuleName;

        DWORD lineDisp = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisp, &line)) {
            logEx(LOG_CRASH, "  #%02d %s!%s+0x%llX  (%s:%lu)  pc=%p",
                i, modName, name, static_cast<unsigned long long>(displacement),
                line.FileName ? line.FileName : "?", line.LineNumber,
                reinterpret_cast<void*>(static_cast<uintptr_t>(frame.AddrPC.Offset)));
        } else {
            logEx(LOG_CRASH, "  #%02d %s!%s+0x%llX  pc=%p",
                i, modName, name, static_cast<unsigned long long>(displacement),
                reinterpret_cast<void*>(static_cast<uintptr_t>(frame.AddrPC.Offset)));
        }
    }
    SymCleanup(process);
}

static void writeMiniDump(EXCEPTION_POINTERS* ep) {
    if (!ep) return;
    char dumpPath[MAX_PATH]{};
    SYSTEMTIME st{};
    GetLocalTime(&st);
    const std::string dir = cfgDir();
    _snprintf_s(dumpPath, sizeof(dumpPath), _TRUNCATE,
        "%s\\NOCOM_crash_%04u%02u%02u_%02u%02u%02u.dmp",
        dir.c_str(),
        static_cast<unsigned>(st.wYear), static_cast<unsigned>(st.wMonth),
        static_cast<unsigned>(st.wDay), static_cast<unsigned>(st.wHour),
        static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond));

    HANDLE file = CreateFileA(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        logEx(LOG_CRASH, "CreateFile for minidump failed path=%s err=%lu", dumpPath, GetLastError());
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;

    const BOOL ok = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), file,
        static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo),
        &mei, nullptr, nullptr);
    CloseHandle(file);

    if (ok) {
        const bool compressionQueued = queueZipCompressionIfLarge(dumpPath);
        if (compressionQueued) {
            char zipPath[MAX_PATH]{};
            _snprintf_s(zipPath, sizeof(zipPath), _TRUNCATE, "%s.zip", dumpPath);
            logEx(LOG_CRASH, "minidump written (>8 MiB); ZIP compression queued: %s", zipPath);
        } else {
            logEx(LOG_CRASH, "minidump written: %s", dumpPath);
        }
    } else {
        logEx(LOG_CRASH, "MiniDumpWriteDump failed err=%lu path=%s", GetLastError(), dumpPath);
    }
}

static LONG WINAPI nocomUnhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
    logEx(LOG_CRASH, "========== UNHANDLED EXCEPTION ==========");
    if (ep && ep->ExceptionRecord) {
        const DWORD code = ep->ExceptionRecord->ExceptionCode;
        logEx(LOG_CRASH, "code=0x%08lX (%s) address=%p flags=0x%08lX",
            code, exceptionCodeName(code),
            ep->ExceptionRecord->ExceptionAddress,
            ep->ExceptionRecord->ExceptionFlags);
        if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
            logEx(LOG_CRASH, "AV type=%s target=%p",
                ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "read" :
                (ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "DEP"),
                reinterpret_cast<void*>(ep->ExceptionRecord->ExceptionInformation[1]));
        }
    }
    if (ep && ep->ContextRecord) {
#if defined(_M_IX86)
        logEx(LOG_CRASH, "regs EIP=%08X ESP=%08X EBP=%08X EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X",
            ep->ContextRecord->Eip, ep->ContextRecord->Esp, ep->ContextRecord->Ebp,
            ep->ContextRecord->Eax, ep->ContextRecord->Ebx, ep->ContextRecord->Ecx,
            ep->ContextRecord->Edx, ep->ContextRecord->Esi, ep->ContextRecord->Edi);
#endif
        logStackTrace(ep->ContextRecord);
    }
    writeMiniDump(ep);
    logEx(LOG_CRASH, "========== END EXCEPTION ==========");
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI nocomVectoredExceptionHandler(EXCEPTION_POINTERS* ep) {

    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION &&
        code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_IN_PAGE_ERROR)
        return EXCEPTION_CONTINUE_SEARCH;

    static volatile LONG once = 0;
    if (InterlockedCompareExchange(&once, 1, 0) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    logEx(LOG_CRASH, "========== FIRST-CHANCE FATAL EXCEPTION ==========");
    logEx(LOG_CRASH, "code=0x%08lX (%s) address=%p",
        code, exceptionCodeName(code), ep->ExceptionRecord->ExceptionAddress);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        logEx(LOG_CRASH, "AV type=%s target=%p",
            ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "read" :
            (ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "DEP"),
            reinterpret_cast<void*>(ep->ExceptionRecord->ExceptionInformation[1]));
    }
    if (ep->ContextRecord) {
#if defined(_M_IX86)
        logEx(LOG_CRASH, "regs EIP=%08X ESP=%08X EBP=%08X",
            ep->ContextRecord->Eip, ep->ContextRecord->Esp, ep->ContextRecord->Ebp);
#endif
        logStackTrace(ep->ContextRecord);
    }
    writeMiniDump(ep);
    logEx(LOG_CRASH, "========== END FIRST-CHANCE ==========");
    return EXCEPTION_CONTINUE_SEARCH;
}

static void installCrashHandlers() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    SetUnhandledExceptionFilter(nocomUnhandledExceptionFilter);
    AddVectoredExceptionHandler(1, nocomVectoredExceptionHandler);
    logEx(LOG_INFO, "crash handlers installed (UEF + VEH + minidump)");
}

static bool parseBinaryCvar(const char* name, int& value) {
    char text[32]{};
    if (!getConVarStringDirect(name, text, sizeof(text))) return false;
    char* end = nullptr;
    const long parsed = strtol(text, &end, 10);
    while (end && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    if (end == text || !end || *end != '\0' || (parsed != 0 && parsed != 1)) return false;
    value = static_cast<int>(parsed);
    return true;
}

static void captureOriginalClientCvars() {
    int value = 0;
    if (parseBinaryCvar("mat_fullbright", value)) {
        g_OriginalFullBright = value;
        g_OriginalFullBrightValid = true;
        g_LastAppliedFullBright = value;
    }
    if (parseBinaryCvar("sv_cheats", value)) {
        g_OriginalSvCheats = value;
        g_OriginalSvCheatsValid = true;
    }

    g_OriginalPropJumpEnableValid = getConVarStringDirect(
        "sv_propjump_enable", g_OriginalPropJumpEnable, sizeof(g_OriginalPropJumpEnable));
    if (g_OriginalPropJumpEnableValid) {
        strncpy_s(g_LastAppliedPropJumpEnable, sizeof(g_LastAppliedPropJumpEnable),
            g_OriginalPropJumpEnable, _TRUNCATE);
    }

    if (parseBinaryCvar("r_drawclipbrushes", value)) {
        g_OriginalDrawClipBrushes = value;
        g_OriginalDrawClipBrushesValid = true;
        g_LastAppliedDrawClipBrushes = value;
    }
}

static void updateClientFullBright() {
    if (!g_Cvar) return;
    const int desired = g_S.fullBright ? 1 : (g_OriginalFullBrightValid ? g_OriginalFullBright : 0);
    if (desired == g_LastAppliedFullBright) return;
    const DWORD now = GetTickCount();
    if (now - g_LastFullBrightCvarAttempt < 250u) return;
    g_LastFullBrightCvarAttempt = now;
    if (setClientFullBrightDirect(desired)) {
        g_LastAppliedFullBright = desired;
    } else {
        InterlockedIncrement(&g_ClientCvarWriteFailures);
    }
}

static void restoreClientFullBright() {
    if (g_OriginalFullBrightValid && g_Cvar) {
        if (setClientFullBrightDirect(g_OriginalFullBright))
            g_LastAppliedFullBright = g_OriginalFullBright;
    }
}

static void updateClientSvCheatsOverride() {
    if (!g_Cvar || !g_OriginalSvCheatsValid) return;
    const int desired = g_S.svCheatsOverride ? 1 : g_OriginalSvCheats;
    int current = -1;
    const bool hasCurrent = parseBinaryCvar("sv_cheats", current);
    if (hasCurrent && current == desired) return;
    const DWORD now = GetTickCount();
    if (now - g_LastSvCheatsCvarAttempt < 250u) return;
    g_LastSvCheatsCvarAttempt = now;
    const char* value = desired ? "1" : "0";
    if (!setClientConVarStringDirect("sv_cheats", value, true)) {
        InterlockedIncrement(&g_ClientCvarWriteFailures);
    }
}

static void restoreClientSvCheatsOverride() {
    if (!g_Cvar || !g_OriginalSvCheatsValid) return;
    const char* value = g_OriginalSvCheats ? "1" : "0";
    setClientConVarStringDirect("sv_cheats", value, true);
}

static void updateClientPropJumpOverride() {
    if (!g_Cvar || !g_OriginalPropJumpEnableValid) return;

    const char* desired = g_S.svPropJumpOverride ? "1" : g_OriginalPropJumpEnable;
    char current[128] = {};
    if (!getConVarStringDirect("sv_propjump_enable", current, sizeof(current))) return;
    if (strcmp(current, desired) == 0) {
        strncpy_s(g_LastAppliedPropJumpEnable, sizeof(g_LastAppliedPropJumpEnable), current, _TRUNCATE);
        return;
    }

    const DWORD now = GetTickCount();
    if (now - g_LastPropJumpEnableCvarAttempt < 250u) return;
    g_LastPropJumpEnableCvarAttempt = now;
    if (setClientConVarStringDirect("sv_propjump_enable", desired, true)) {
        char applied[128] = {};
        if (getConVarStringDirect("sv_propjump_enable", applied, sizeof(applied)))
            strncpy_s(g_LastAppliedPropJumpEnable, sizeof(g_LastAppliedPropJumpEnable), applied, _TRUNCATE);
    } else {
        InterlockedIncrement(&g_ClientCvarWriteFailures);
    }
}

static void restoreClientPropJumpOverride() {
    if (!g_Cvar || !g_OriginalPropJumpEnableValid) return;
    char current[128] = {};
    if (!getConVarStringDirect("sv_propjump_enable", current, sizeof(current))) return;

    if (g_LastAppliedPropJumpEnable[0] && strcmp(current, g_LastAppliedPropJumpEnable) != 0) return;
    if (setClientConVarStringDirect("sv_propjump_enable", g_OriginalPropJumpEnable, true))
        strncpy_s(g_LastAppliedPropJumpEnable, sizeof(g_LastAppliedPropJumpEnable),
            g_OriginalPropJumpEnable, _TRUNCATE);
}

static void updateClientDrawClipBrushes() {
    if (!g_Cvar) return;
    const int desired = g_S.drawClipBrushes ? 1 : (g_OriginalDrawClipBrushesValid ? g_OriginalDrawClipBrushes : 0);
    if (desired == g_LastAppliedDrawClipBrushes) return;
    const DWORD now = GetTickCount();
    if (now - g_LastDrawClipBrushesCvarAttempt < 250u) return;
    g_LastDrawClipBrushesCvarAttempt = now;
    char buf[16];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d", desired);
    if (setClientConVarStringDirect("r_drawclipbrushes", buf, true)) {
        g_LastAppliedDrawClipBrushes = desired;
    } else {
        InterlockedIncrement(&g_ClientCvarWriteFailures);
    }
}

static void restoreClientDrawClipBrushes() {
    if (!g_Cvar || !g_OriginalDrawClipBrushesValid) return;
    char buf[16];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%d", g_OriginalDrawClipBrushes);
    if (setClientConVarStringDirect("r_drawclipbrushes", buf, true))
        g_LastAppliedDrawClipBrushes = g_OriginalDrawClipBrushes;
}

struct FogCvarState {
    const char* name;
    char original[128];
    char lastApplied[128];
    bool captured;
    bool changed;
};
static FogCvarState g_FogCvars[] = {
    { "fog_override", {}, {}, false, false },
    { "fog_enable", {}, {}, false, false },
    { "fog_start", {}, {}, false, false },
    { "fog_end", {}, {}, false, false },
    { "fog_color", {}, {}, false, false },
    { "fog_maxdensity", {}, {}, false, false },
    { "fog_startskybox", {}, {}, false, false },
    { "fog_endskybox", {}, {}, false, false },
    { "fog_colorskybox", {}, {}, false, false },
    { "fog_maxdensityskybox", {}, {}, false, false },
    { "fog_enableskybox", {}, {}, false, false },
    { "fog_hdrcolorscale", {}, {}, false, false },
    { "fog_radial", {}, {}, false, false },
    { "fog_radialskybox", {}, {}, false, false }
};
static bool g_FogSnapshotReady = false;
static bool g_FogHasApplied = false;
static bool g_FogFailureLogged = false;

static FogCvarState* findFogCvar(const char* name) {
    if (!name) return nullptr;
    for (auto& state : g_FogCvars)
        if (_stricmp(state.name, name) == 0) return &state;
    return nullptr;
}

static bool captureOriginalFogCvars() {
    bool requiredOk = true;
    for (auto& state : g_FogCvars) {
        state.original[0] = '\0';
        state.lastApplied[0] = '\0';
        state.changed = false;
        state.captured = getConVarStringDirect(state.name, state.original, sizeof(state.original));
        if (!state.captured &&
            (!_stricmp(state.name, "fog_override") || !_stricmp(state.name, "fog_enable") ||
             !_stricmp(state.name, "fog_start") || !_stricmp(state.name, "fog_end") ||
             !_stricmp(state.name, "fog_color") || !_stricmp(state.name, "fog_maxdensity"))) {
            requiredOk = false;
        }
    }
    g_FogSnapshotReady = requiredOk;
    return requiredOk;
}

static bool setFogCvarValue(const char* name, const char* value) {
    FogCvarState* state = findFogCvar(name);
    if (!state || !state->captured || !value) return false;
    if (strcmp(state->lastApplied, value) == 0) return true;
    if (!setClientConVarStringDirect(name, value, true)) return false;
    strncpy_s(state->lastApplied, sizeof(state->lastApplied), value, _TRUNCATE);
    state->changed = true;
    return true;
}

static void restoreClientFog() {
    if (!g_FogSnapshotReady) return;
    for (auto& state : g_FogCvars) {
        if (!state.captured || !state.changed) continue;
        setClientConVarStringDirect(state.name, state.original, true);
        state.changed = false;
        state.lastApplied[0] = '\0';
    }
    g_FogSnapshotReady = false;
    g_FogHasApplied = false;
}

static void updateClientFog(bool confirmedInGame) {
    if (!g_S.customFog || !confirmedInGame || !g_Cvar) {

        restoreClientFog();
        return;
    }
    if (!g_FogSnapshotReady && !captureOriginalFogCvars()) {
        if (!g_FogFailureLogged) {
            logNocom("custom fog disabled for this build: required local fog ConVars are unavailable");
            g_FogFailureLogged = true;
        }
        return;
    }

    const int start = static_cast<int>(g_S.fogStart + 0.5f);
    int end = static_cast<int>(g_S.fogEnd + 0.5f);
    if (end <= start) end = start + 1;
    const int red = static_cast<int>(g_S.fogR + 0.5f);
    const int green = static_cast<int>(g_S.fogG + 0.5f);
    const int blue = static_cast<int>(g_S.fogB + 0.5f);
    char startText[24]{}, endText[24]{}, colorText[48]{}, densityText[24]{};
    sprintf_s(startText, "%d", start);
    sprintf_s(endText, "%d", end);
    sprintf_s(colorText, "%d %d %d", red, green, blue);
    sprintf_s(densityText, "%.3f", static_cast<double>(g_S.fogDensity));

    bool requiredOk = true;
    requiredOk = setFogCvarValue("fog_override", "1") && requiredOk;
    requiredOk = setFogCvarValue("fog_enable", "1") && requiredOk;
    requiredOk = setFogCvarValue("fog_start", startText) && requiredOk;
    requiredOk = setFogCvarValue("fog_end", endText) && requiredOk;
    requiredOk = setFogCvarValue("fog_color", colorText) && requiredOk;
    requiredOk = setFogCvarValue("fog_maxdensity", densityText) && requiredOk;

    setFogCvarValue("fog_startskybox", startText);
    setFogCvarValue("fog_endskybox", endText);
    setFogCvarValue("fog_colorskybox", colorText);
    setFogCvarValue("fog_maxdensityskybox", densityText);
    setFogCvarValue("fog_enableskybox", "1");
    setFogCvarValue("fog_hdrcolorscale", "1");
    setFogCvarValue("fog_radial", "0");
    setFogCvarValue("fog_radialskybox", "0");
    g_FogHasApplied = requiredOk;
    if (requiredOk) g_FogFailureLogged = false;
}

static BOOL WINAPI hkSetCursorPos(int x, int y) {
    if (!o_SetCursorPos) return TRUE;
    return g_MenuOpen ? TRUE : o_SetCursorPos(x, y);
}
static BOOL WINAPI hkClipCursor(const RECT* r) {
    if (!o_ClipCursor) return TRUE;
    return o_ClipCursor(g_MenuOpen ? nullptr : r);
}
static HCURSOR WINAPI hkSetCursor(HCURSOR cursor) {
    if (!o_SetCursor) return cursor;

    HCURSOR arrow = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    return o_SetCursor(g_MenuOpen && arrow ? arrow : cursor);
}

static bool readable(const void* p, size_t n = sizeof(void*)) {
    if (!p || n == 0) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(p);
    const uintptr_t regionBegin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const uintptr_t regionEnd = regionBegin + mbi.RegionSize;
    return begin >= regionBegin && begin <= regionEnd && n <= regionEnd - begin;
}

static int patchIatInModule(HMODULE mod, void* target, void* hook) {
    if (!mod || !target || !hook) return 0;
    auto base = reinterpret_cast<uint8_t*>(mod);
    if (!readable(base, sizeof(IMAGE_DOS_HEADER))) return 0;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return 0;
    if (!readable(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS))) return 0;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    const size_t imageSize = nt->OptionalHeader.SizeOfImage;
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    if (!dir.VirtualAddress || !dir.Size || dir.VirtualAddress > imageSize ||
        dir.Size > imageSize - dir.VirtualAddress) return 0;

    auto iat = reinterpret_cast<ULONG_PTR*>(base + dir.VirtualAddress);
    if (!readable(iat, dir.Size)) return 0;
    const size_t count = dir.Size / sizeof(ULONG_PTR);
    int patched = 0;
    for (size_t i = 0; i < count; ++i) {
        if (reinterpret_cast<void*>(iat[i]) != target) continue;
        DWORD oldProtect = 0;
        if (!VirtualProtect(&iat[i], sizeof(iat[i]), PAGE_READWRITE, &oldProtect)) continue;
        iat[i] = reinterpret_cast<ULONG_PTR>(hook);
        DWORD ignored = 0;
        VirtualProtect(&iat[i], sizeof(iat[i]), oldProtect, &ignored);
        ++patched;
    }
    return patched;
}

static int patchIat(void* target, void* hook) {

    static const char* const kModules[] = {
        "client.dll", "engine.dll", "inputsystem.dll", "vguimatsurface.dll",
        "shaderapidx9.dll", "materialsystem.dll", "vgui2.dll"
    };
    int patched = 0;
    for (const char* name : kModules) {
        HMODULE module = GetModuleHandleA(name);
        if (module) patched += patchIatInModule(module, target, hook);
    }
    return patched;
}

static void* findClientMode(HMODULE client) {
    if (!client) return nullptr;
    auto base = reinterpret_cast<uint8_t*>(client);
    if (!readable(base, sizeof(IMAGE_DOS_HEADER))) return nullptr;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return nullptr;
    if (!readable(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS))) return nullptr;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    size_t imgSize = nt->OptionalHeader.SizeOfImage;

    static const uintptr_t kOffs[] = {
        kClientModeOff,  
        0x731B5C, 0x732A9C, 0x733C2C, 0x72262C, 0x74262C, 0x6F262C
    };
    for (uintptr_t off : kOffs) {
        if (off + sizeof(void*) > imgSize) continue;
        void** p = reinterpret_cast<void**>(base + off);
        if (!readable(p)) continue;
        void* cm = *p;
        if (!cm || !readable(cm)) continue;

        void** vt = *reinterpret_cast<void***>(cm);
        if (!readable(vt) || !readable(vt[0])) continue;

        if (!readable(&vt[21]) || !vt[21]) continue;
        if (!readable(&vt[32]) || !vt[32]) continue;
        g_ClientModeSourceOffset = off;
        return cm;
    }

    uintptr_t start = reinterpret_cast<uintptr_t>(base);
    uintptr_t end = start + imgSize - sizeof(void*);
    for (uintptr_t a = start; a < end; a += sizeof(void*)) {
        void** p = reinterpret_cast<void**>(a);
        if (!readable(p)) continue;
        void* cm = *p;
        if (!cm || !readable(cm)) continue;
        void** vt = *reinterpret_cast<void***>(cm);
        if (!vt || !readable(vt)) continue;
        uintptr_t vti = reinterpret_cast<uintptr_t>(vt);
        if (vti < start || vti >= start + imgSize) continue;
        if (!readable(&vt[16]) || !vt[16]) continue;
        if (!readable(&vt[21]) || !vt[21]) continue;
        if (!readable(&vt[32]) || !vt[32]) continue;

        g_ClientModeSourceOffset = a - start;
        return cm;
    }
    return nullptr;
}

static int __fastcall hkKeyInput(void* self, void* edx, int down, int keynum, const char* binding) {

    if (down && binding) notifyBindingForBindGuard(binding);

    if (g_HooksReady && !g_CleaningUp && (g_MenuOpen || g_BindPtr)) return 0;
    return o_KeyInput ? o_KeyInput(self, edx, down, keynum, binding) : 0;
}

static bool __fastcall hkCreateMove(void* self, void* edx, float ft, CUserCmd* cmd) {
    if (!o_CreateMove) return true;
    bool ret = o_CreateMove(self, edx, ft, cmd);
    if (!g_HooksReady || g_CleaningUp || g_EjectRequested || isMapTransitionQuiescing()) return ret;
    __try {

        if (!g_MenuOpen && g_BindPtr) cancelBindCapture();

        if ((g_MenuOpen || g_BindPtr) && cmd) {
            cmd->buttons = 0;
            cmd->forwardmove = 0.f;
            cmd->sidemove = 0.f;
            cmd->upmove = 0.f;
        }
        static int prevCmdNum;
        if (cmd && cmd->command_number) {
            if (cmd->command_number < prevCmdNum - 5) g_CmdReset = true;
            prevCmdNum = cmd->command_number;
        }
        if (!g_MenuOpen && !g_BindPtr && !shouldBlockGameplayFeatureInput()) {
            runCreateMove(ft, cmd);
            if (cmd) updatePredictionAngles(cmd->viewangles);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {

    }
    return ret;
}

static void applyCameras(void* setup) {
    if (!setup || !InGame()) return;
    if (g_S.fovOn)
        Field<float>(setup, 0x38) = g_S.fov;

    float* pitch = reinterpret_cast<float*>(reinterpret_cast<char*>(setup) + 0x4C);
    float* yaw   = reinterpret_cast<float*>(reinterpret_cast<char*>(setup) + 0x50);
    float* ox    = reinterpret_cast<float*>(reinterpret_cast<char*>(setup) + 0x40);
    float* oy    = reinterpret_cast<float*>(reinterpret_cast<char*>(setup) + 0x44);
    float* oz    = reinterpret_cast<float*>(reinterpret_cast<char*>(setup) + 0x48);

    bool backCamActive = false;
    if (!g_MenuOpen && !g_BindPtr && !shouldBlockGameplayFeatureInput() &&
        g_S.kBackCam > 0 && g_S.kBackCam < 256) {
        backCamActive = (GetAsyncKeyState(g_S.kBackCam) & 0x8000) != 0;
    }
    if (backCamActive) {

        *yaw += 180.f;
        while (*yaw > 180.f) *yaw -= 360.f;
        while (*yaw < -180.f) *yaw += 360.f;
        *pitch = g_S.backCamPitch;
    } else if (g_S.projCam && g_Trace && o_Origin >= 0) {

        void* local = Ent(LocalIndex());
        if (!local) return;
        Vec3 eye = eyePos(local);
        float p = *pitch * 0.017453292f;
        float yw = *yaw * 0.017453292f;
        float cp = cosf(p), sp = sinf(p), cy = cosf(yw), sy = sinf(yw);
        Vec3 fwd(cp * cy, cp * sy, -sp);
        Vec3 end(eye.x + fwd.x * 8000.f, eye.y + fwd.y * 8000.f, eye.z + fwd.z * 8000.f);
        Ray_t ray;
        ray.init(eye, end);
        TraceFilter filter;
        filter.skip = local;
        Trace_t tr{};
        VCall<void, const Ray_t&, unsigned, void*, Trace_t*>(g_Trace, 4, ray, 0x200400B, &filter, &tr);
        float dist = g_S.projCamDist;
        if (dist < 16.f) dist = 16.f;

        *ox = tr.endpos.x - fwd.x * dist;
        *oy = tr.endpos.y - fwd.y * dist;
        *oz = tr.endpos.z - fwd.z * dist;

    }
}

static float clampViewmodelFov(float value) {
    if (!std::isfinite(value)) return 54.0f;
    if (value < 40.0f) return 40.0f;
    if (value > 120.0f) return 120.0f;
    return value;
}

static float __fastcall hkGetViewModelFOV(void* self, void* edx) {
    InterlockedIncrement(&g_ViewmodelFovHookCalls);
    if (g_HooksReady && !g_CleaningUp && !isMapTransitionQuiescing() && g_S.viewmodelFovOn) {
        bool inGame = false;
        __try { inGame = InGame(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { inGame = false; }
        if (inGame) {
            const float fov = clampViewmodelFov(g_S.viewmodelFov);
            g_LastViewmodelFovWritten = fov;
            InterlockedIncrement(&g_ViewmodelFovHookWrites);
            return fov;
        }
    }
    return o_GetViewModelFOV ? o_GetViewModelFOV(self, edx) : 54.0f;
}

static float g_SpeedometerBestSpeed = 0.f;

static void drawSpeedometerOverlay(int sw, int sh) {
    if (!g_S.speedometer || sw <= 0 || sh <= 0 || o_Velocity < 0) return;
    void* local = Ent(LocalIndex());
    if (!local) return;

    Vec3 velocity{};
    __try { velocity = Field<Vec3>(local, o_Velocity); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    const float speed = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
    if (!std::isfinite(speed)) return;
    if (speed > g_SpeedometerBestSpeed) g_SpeedometerBestSpeed = speed;

    wchar_t current[64]{};
    wchar_t best[64]{};
    swprintf_s(current, _countof(current), L"Speed: %.0f u/s", speed);
    swprintf_s(best, _countof(best), L"Best: %.0f u/s", g_SpeedometerBestSpeed);
    const unsigned long font = getOrCreateNamedFont(g_S.speedometerFont, g_S.speedometerFontSize);
    int currentW = 0, currentH = 0, bestW = 0, bestH = 0;
    measureText(font, current, currentW, currentH);
    if (g_S.speedometerShowBest) measureText(font, best, bestW, bestH);
    const int textW = currentW > bestW ? currentW : bestW;
    const int lineGap = g_S.speedometerShowBest ? 3 : 0;
    const int height = currentH + (g_S.speedometerShowBest ? bestH + lineGap : 0);
    const int centerX = static_cast<int>(sw * (g_S.speedometerX / 100.f));
    const int top = static_cast<int>(sh * (g_S.speedometerY / 100.f));
    const int left = centerX - textW / 2;
    if (g_S.speedometerBackground)
        fill(left - 8, top - 4, left + textW + 8, top + height + 4, 8, 8, 8, 190);
    textFont(font, centerX - currentW / 2, top, 240, 240, 240, current);
    if (g_S.speedometerShowBest)
        textFont(font, centerX - bestW / 2, top + currentH + lineGap, 190, 210, 255, best);
}

static void __fastcall hkOverrideView(void* self, void* edx, void* setup) {
    if (o_OverrideView) o_OverrideView(self, edx, setup);
    if (!g_HooksReady || g_CleaningUp || isMapTransitionQuiescing()) return;
    __try {
        if (InterlockedExchange(&g_PendingFogRestore, 0) != 0)
            restoreClientFog();
        updateClientFullBright();
        updateClientDrawClipBrushes();
        updateClientSvCheatsOverride();
        updateClientPropJumpOverride();
        applyCameras(setup);

        if (setup && g_S.viewmodelFovOn && InGame() && readable(setup, 0x40)) {
            const float fov = clampViewmodelFov(g_S.viewmodelFov);
            float* field = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(setup) + 0x3C);
            if (!std::isfinite(*field) || fabsf(*field - fov) > 0.01f) {
                *field = fov;
                g_LastViewmodelFovWritten = fov;
                InterlockedIncrement(&g_ViewmodelFovSetupWrites);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

struct StudioHdrPrefixHook {
    int id;
    int version;
    int checksum;
    char name[64];
    int length;
    Vec3 eyePosition;
    Vec3 illuminationPosition;
    Vec3 hullMin;
    Vec3 hullMax;
    Vec3 viewBbMin;
    Vec3 viewBbMax;
    int flags;
    int numBones;
};
static_assert(offsetof(StudioHdrPrefixHook, numBones) == 156, "studiohdr_t prefix mismatch");

struct DrawModelStateHook {
    void* studioHdr;
    void* studioHwData;
    void* renderable;
    const Matrix3x4* modelToWorld;
    unsigned short decals;
    unsigned short padding;
    int drawFlags;
    int lod;
};
static_assert(sizeof(DrawModelStateHook) == 28, "DrawModelState_t layout mismatch for x86 Source 2013");

static float deg2RadVm(float d) { return d * 0.01745329252f; }

static void buildEulerMatrixVm(float pitchDeg, float yawDeg, float rollDeg, Matrix3x4& out) {
    const float p = deg2RadVm(pitchDeg), y = deg2RadVm(yawDeg), r = deg2RadVm(rollDeg);
    const float sp = sinf(p), cp = cosf(p);
    const float sy = sinf(y), cy = cosf(y);
    const float sr = sinf(r), cr = cosf(r);
    out.m[0][0] = cp * cy;  out.m[1][0] = cp * sy;  out.m[2][0] = -sp;
    out.m[0][1] = sr * sp * cy - cr * sy; out.m[1][1] = sr * sp * sy + cr * cy; out.m[2][1] = sr * cp;
    out.m[0][2] = cr * sp * cy + sr * sy; out.m[1][2] = cr * sp * sy - sr * cy; out.m[2][2] = cr * cp;
    out.m[0][3] = out.m[1][3] = out.m[2][3] = 0.f;
}

struct ViewmodelPose {
    float dx = 0.f, dy = 0.f, dz = 0.f;
    float rp = 0.f, ry = 0.f, rr = 0.f;
    bool any = false;
};

static ViewmodelPose computeViewmodelPose(const ModelRenderInfoHook& info) {
    ViewmodelPose pose{};
    const float pitch = deg2RadVm(info.angles.x);
    const float yaw = deg2RadVm(info.angles.y);
    const float sp = sinf(pitch), cp = cosf(pitch);
    const float sy = sinf(yaw), cy = cosf(yaw);

    const float ox = g_S.viewmodelOn ? g_S.viewmodelX : 0.f;
    const float oy = g_S.viewmodelOn ? g_S.viewmodelY : 0.f;
    const float oz = g_S.viewmodelOn ? g_S.viewmodelZ : 0.f;
    pose.dx = (-sy * ox) + (cp * cy * oy);
    pose.dy = ( cy * ox) + (cp * sy * oy);
    pose.dz = oz - sp * oy;

    if (g_S.viewmodelOn) {
        pose.rp = g_S.viewmodelRotX;
        pose.ry = g_S.viewmodelRotY;
        pose.rr = g_S.viewmodelRotZ;
    }

    if (g_S.viewmodelSway && g_S.viewmodelSwayAmount > 0.001f) {
        static float lastFx = 0.f, lastFy = 0.f, lastFz = 1.f;
        static bool haveFacing = false;
        static DWORD lastTick = 0;
        const float fx = cp * cy;
        const float fy = cp * sy;
        const float fz = -sp;
        const DWORD now = GetTickCount();
        float dt = 0.015f;
        if (lastTick != 0 && now > lastTick) {
            dt = (now - lastTick) * 0.001f;
            if (dt < 0.001f) dt = 0.001f;
            if (dt > 0.05f) dt = 0.05f;  
        }
        lastTick = now;
        if (!haveFacing) {
            lastFx = fx; lastFy = fy; lastFz = fz;
            haveFacing = true;
        } else {

            const float dx0 = fx - lastFx;
            const float dy0 = fy - lastFy;
            const float dz0 = fz - lastFz;
            const float flDiff = sqrtf(dx0 * dx0 + dy0 * dy0 + dz0 * dz0);
            constexpr float kMaxViewModelLag = 1.5f;  
            float flSpeed = 5.0f;
            if (flDiff > kMaxViewModelLag && kMaxViewModelLag > 0.f)
                flSpeed *= flDiff / kMaxViewModelLag;

            lastFx += dx0 * (flSpeed * dt);
            lastFy += dy0 * (flSpeed * dt);
            lastFz += dz0 * (flSpeed * dt);
            const float len = sqrtf(lastFx * lastFx + lastFy * lastFy + lastFz * lastFz);
            if (len > 0.0001f) {
                lastFx /= len; lastFy /= len; lastFz /= len;
            }

            const float lagMag = 5.0f * g_S.viewmodelSwayAmount;
            pose.dx += dx0 * (-lagMag);
            pose.dy += dy0 * (-lagMag);
            pose.dz += dz0 * (-lagMag);

            pose.rp += (-dz0) * (lagMag * 0.35f);
            pose.ry += (dx0 * sy - dy0 * cy) * (lagMag * 0.35f);
        }
    }

    pose.any = (fabsf(pose.dx) + fabsf(pose.dy) + fabsf(pose.dz) > 0.0001f) ||
               (fabsf(pose.rp) + fabsf(pose.ry) + fabsf(pose.rr) > 0.0001f);
    return pose;
}

static void applyPoseToBone(Matrix3x4& bone, const ViewmodelPose& pose, float ox, float oy, float oz) {
    bone.m[0][3] += pose.dx;
    bone.m[1][3] += pose.dy;
    bone.m[2][3] += pose.dz;
    if (fabsf(pose.rp) + fabsf(pose.ry) + fabsf(pose.rr) < 0.0001f) return;
    ox += pose.dx; oy += pose.dy; oz += pose.dz;
    Matrix3x4 rot{};
    buildEulerMatrixVm(pose.rp, pose.ry, pose.rr, rot);
    float px = bone.m[0][3] - ox, py = bone.m[1][3] - oy, pz = bone.m[2][3] - oz;
    bone.m[0][3] = ox + rot.m[0][0]*px + rot.m[0][1]*py + rot.m[0][2]*pz;
    bone.m[1][3] = oy + rot.m[1][0]*px + rot.m[1][1]*py + rot.m[1][2]*pz;
    bone.m[2][3] = oz + rot.m[2][0]*px + rot.m[2][1]*py + rot.m[2][2]*pz;
    Matrix3x4 rotated{};
    for (int i = 0; i < 3; ++i) {
        rotated.m[i][0] = rot.m[i][0]*bone.m[0][0] + rot.m[i][1]*bone.m[1][0] + rot.m[i][2]*bone.m[2][0];
        rotated.m[i][1] = rot.m[i][0]*bone.m[0][1] + rot.m[i][1]*bone.m[1][1] + rot.m[i][2]*bone.m[2][1];
        rotated.m[i][2] = rot.m[i][0]*bone.m[0][2] + rot.m[i][1]*bone.m[1][2] + rot.m[i][2]*bone.m[2][2];
        rotated.m[i][3] = bone.m[i][3];
    }
    bone = rotated;
}

static bool isViewModel(const ModelRenderInfoHook& info) {
    if (!info.model || !g_ModelInfo) return false;
    const char* name = VCall<const char*, const void*>(g_ModelInfo, 3, info.model);
    if (!name || !*name) return false;
    return strstr(name, "models/weapons/v_") != nullptr ||
           strstr(name, "models/weapons/c_arms") != nullptr ||
           strstr(name, "models/weapons/v_models/") != nullptr;
}

static void __fastcall hkDrawModelExecute(void* self, void* edx,
                                           const void* state, const ModelRenderInfoHook& info,
                                           Matrix3x4* bones) {
    if (!o_DrawModelExecute) return;
    if (!g_HooksReady || g_CleaningUp || isMapTransitionQuiescing()) {
        o_DrawModelExecute(self, edx, state, info, bones);
        return;
    }

    bool useBoneCopy = false;
    bool useWorldCopy = false;
    Matrix3x4 shiftedBones[kMaxBones];
    Matrix3x4 shiftedWorld{};
    ModelRenderInfoHook infoCopy = info;
    DrawModelStateHook stateCopy{};

    if (g_HooksReady && !g_CleaningUp && (g_S.viewmodelOn || g_S.viewmodelSway) && state && info.model) {
        __try {
            if (isViewModel(info)) {
                const ViewmodelPose pose = computeViewmodelPose(info);
                if (!pose.any) {

                } else if (bones && readable(state, sizeof(DrawModelStateHook))) {
                    const DrawModelStateHook* modelState = static_cast<const DrawModelStateHook*>(state);
                    const StudioHdrPrefixHook* hdr = static_cast<const StudioHdrPrefixHook*>(modelState->studioHdr);
                    if (hdr && readable(hdr, sizeof(StudioHdrPrefixHook))) {
                        const int boneCount = hdr->numBones;
                        if (boneCount > 0 && boneCount <= kMaxBones &&
                            readable(bones, static_cast<size_t>(boneCount) * sizeof(Matrix3x4))) {
                            memcpy(shiftedBones, bones, static_cast<size_t>(boneCount) * sizeof(Matrix3x4));
                            const float ox = shiftedBones[0].m[0][3];
                            const float oy = shiftedBones[0].m[1][3];
                            const float oz = shiftedBones[0].m[2][3];
                            for (int i = 0; i < boneCount; ++i)
                                applyPoseToBone(shiftedBones[i], pose, ox, oy, oz);
                            useBoneCopy = true;
                        }
                    }
                } else if (!bones && readable(state, sizeof(DrawModelStateHook)) &&
                           info.modelToWorld && readable(info.modelToWorld, sizeof(Matrix3x4))) {

                    memcpy(&stateCopy, state, sizeof(stateCopy));
                    memcpy(&shiftedWorld, info.modelToWorld, sizeof(shiftedWorld));
                    applyPoseToBone(shiftedWorld, pose, shiftedWorld.m[0][3], shiftedWorld.m[1][3], shiftedWorld.m[2][3]);
                    stateCopy.modelToWorld = &shiftedWorld;
                    infoCopy.origin.x += pose.dx;
                    infoCopy.origin.y += pose.dy;
                    infoCopy.origin.z += pose.dz;
                    infoCopy.modelToWorld = &shiftedWorld;
                    useWorldCopy = true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            useBoneCopy = false;
            useWorldCopy = false;
        }
    }

    if (useBoneCopy) {
        o_DrawModelExecute(self, edx, state, info, shiftedBones);
    } else if (useWorldCopy) {
        o_DrawModelExecute(self, edx, &stateCopy, infoCopy, nullptr);
    } else {
        o_DrawModelExecute(self, edx, state, info, bones);
    }
}

static bool containsNoCase(const char* text, const char* token);

static void paintBody(unsigned panel) {
    {
        static DWORD s_lastSched = 0;
        const DWORD now = GetTickCount();
        if (!s_lastSched || now - s_lastSched > 2000u) {
            s_lastSched = now;
            applyGameProcessScheduling();
        }
    }

    static DWORD lastTopPanelPaint = 0;
    static unsigned focusOverlayPanel = 0;
    static unsigned gameUiPanel = 0;
    static unsigned frontEndPreferredPanel = 0;
    static int frontEndPreferredRank = 99;
    static DWORD frontEndPreferredLastSeen = 0;

    static unsigned cachedTopPanel = 0;
    static unsigned cachedOverlayPanel = 0;
    static unsigned cachedFocusOverlayPanel = 0;
    static unsigned cachedGameUiPanel = 0;
    static unsigned cachedMenuBackgroundPanel = 0;

    static DWORD s_ingameBucket = 0;
    static bool s_ingameCached = false;
    if (!g_Panel || !g_Surface || !g_Engine) return;

    const DWORD panelNow = GetTickCount();
    const DWORD ingameBucket = panelNow / 16u;
    if (ingameBucket != s_ingameBucket) {
        s_ingameBucket = ingameBucket;
        __try {
            s_ingameCached = InGame();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            s_ingameCached = false;
        }
    }
    const bool ingame = s_ingameCached;

    bool isTopPanel = (panel && panel == cachedTopPanel);
    bool isOverlay = (panel && panel == cachedOverlayPanel);
    bool isFocusOverlay = (panel && panel == cachedFocusOverlayPanel);
    bool isGameUi = (panel && panel == cachedGameUiPanel);
    bool isMenuBackground = (panel && panel == cachedMenuBackgroundPanel);

    static DWORD s_panelCacheValidateAt = 0;
    const bool needNameLookup =
        (!isTopPanel && !isOverlay && !isFocusOverlay && !isGameUi && !isMenuBackground) ||
        (panelNow - s_panelCacheValidateAt >= 2000u);

    if (needNameLookup) {
        if (isTopPanel || isOverlay || isFocusOverlay || isGameUi || isMenuBackground)
            s_panelCacheValidateAt = panelNow;

        const char* nm = nullptr;
        __try {
            nm = VCall<const char*, unsigned>(g_Panel, 36, panel);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return;
        }
        if (!nm) {
            if (isTopPanel) cachedTopPanel = 0;
            if (isOverlay) cachedOverlayPanel = 0;
            if (isFocusOverlay) cachedFocusOverlayPanel = 0;
            if (isGameUi) cachedGameUiPanel = 0;
            if (isMenuBackground) cachedMenuBackgroundPanel = 0;
            return;
        }

        isTopPanel = isOverlay = isFocusOverlay = isGameUi = isMenuBackground = false;
        if (!strcmp(nm, "MatSystemTopPanel")) {
            isTopPanel = true;
            cachedTopPanel = panel;
        } else if (!strcmp(nm, "OverlayPopupPanel")) {
            isOverlay = true;
            cachedOverlayPanel = panel;
        } else if (!strcmp(nm, "FocusOverlayPanel")) {
            isFocusOverlay = true;
            cachedFocusOverlayPanel = panel;
        } else if (!strcmp(nm, "GameUIPanel")) {
            isGameUi = true;
            cachedGameUiPanel = panel;
        } else if (!strcmp(nm, "MainMenuBackground")) {
            isMenuBackground = true;
            cachedMenuBackgroundPanel = panel;
        } else {

            if (panel == cachedTopPanel) cachedTopPanel = 0;
            if (panel == cachedOverlayPanel) cachedOverlayPanel = 0;
            if (panel == cachedFocusOverlayPanel) cachedFocusOverlayPanel = 0;
            if (panel == cachedGameUiPanel) cachedGameUiPanel = 0;
            if (panel == cachedMenuBackgroundPanel) cachedMenuBackgroundPanel = 0;
            if (containsNoCase(nm, "loading") || containsNoCase(nm, "progress"))
            return;
        }
    }

    if (isFocusOverlay) focusOverlayPanel = panel;
    else if (isGameUi) gameUiPanel = panel;
    if (isTopPanel) lastTopPanelPaint = panelNow;

    bool usePanel = false;
    if (ingame) {

        usePanel = isTopPanel;
        frontEndPreferredPanel = 0;
        frontEndPreferredRank = 99;
        frontEndPreferredLastSeen = 0;
        if (!usePanel && panelNow - lastTopPanelPaint > 750) {
            const unsigned fallback = focusOverlayPanel ? focusOverlayPanel : gameUiPanel;
            usePanel = (fallback && panel == fallback) || isOverlay;
        }
    } else {

        if (frontEndPreferredPanel && panelNow - frontEndPreferredLastSeen > 1500) {
            frontEndPreferredPanel = 0;
            frontEndPreferredRank = 99;
        }

        const int rank = isFocusOverlay ? 0 : (isOverlay ? 1 : (isGameUi ? 2 : (isMenuBackground ? 3 : 99)));
        if (rank < 99) {
            if (!frontEndPreferredPanel || rank < frontEndPreferredRank) {
                frontEndPreferredRank = rank;
                frontEndPreferredPanel = panel;
            }
            if (panel == frontEndPreferredPanel) {
                usePanel = true;
                frontEndPreferredLastSeen = panelNow;
            }
        } else if (!frontEndPreferredPanel && isTopPanel) {

            usePanel = true;
        }
    }
    if (!usePanel) return;

    static bool wasIn;
    static DWORD lastFont = 0, lastCheck = 0;
    static char lastKnownLevel[64] = {};
    NetStats currentNetStats{};
    if (ingame) {
        __try { queryNetStats(currentNetStats); }
        __except (EXCEPTION_EXECUTE_HANDLER) { currentNetStats = {}; }
    }
    static bool levelWasTemporarilyUnavailable = false;
    bool mapChanged = false;
    if (ingame && currentNetStats.level[0]) {
        mapChanged = levelWasTemporarilyUnavailable ||
            (lastKnownLevel[0] && _stricmp(lastKnownLevel, currentNetStats.level) != 0);
        levelWasTemporarilyUnavailable = false;
        if (!lastKnownLevel[0] || _stricmp(lastKnownLevel, currentNetStats.level) != 0)
            strncpy_s(lastKnownLevel, sizeof(lastKnownLevel), currentNetStats.level, _TRUNCATE);
    } else if (ingame && lastKnownLevel[0]) {

        levelWasTemporarilyUnavailable = true;
    } else if (!ingame) {
        lastKnownLevel[0] = '\0';
        levelWasTemporarilyUnavailable = false;
    }
    bool transitioned = ingame != wasIn;
    if (transitioned || mapChanged) {

        beginMapTransitionQuiesce(1500u);

        cachedTopPanel = cachedOverlayPanel = cachedFocusOverlayPanel = 0;
        cachedGameUiPanel = cachedMenuBackgroundPanel = 0;
        if (ingame && g_S.micSpam) restartMicSpamForMapLoad();
        else if (!g_S.micSpam) forceStopMicSpam();

        if (ingame && g_S.fullBright) {
            g_LastAppliedFullBright = -1;
            g_LastFullBrightCvarAttempt = 0;
        }
        if (ingame && g_S.drawClipBrushes) {
            g_LastAppliedDrawClipBrushes = -1;
            g_LastDrawClipBrushesCvarAttempt = 0;
        }
        if (!ingame || mapChanged)
            InterlockedExchange(&g_PendingFogRestore, 1);

        g_BindPtr = nullptr;
        g_Focus = FOCUS_NONE;
        g_ActiveSlider = -1;
        g_CmdReset = true;
        InterlockedExchange(&g_PendingUiRefresh, 1);
        g_SpeedometerBestSpeed = 0.f;
        wasIn = ingame;
        return;
    }

    applyNetworkSettings();

    if (InterlockedExchange(&g_PendingUiRefresh, 0) != 0) {
        refreshWatermarkFont();
        refreshNamedFonts();
    }
    bool bad = false;
    if (g_Font && GetTickCount() - lastCheck > 500) {
        lastCheck = GetTickCount();
        __try {
            bad = VCall<int, unsigned long>(g_Surface, 69, g_Font) <= 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            bad = true;
        }
    }
    bool recreate = !g_Font || bad || transitioned || g_CmdReset;
    wasIn = ingame;
    if (recreate) {
        g_CmdReset = false;
        lastFont = GetTickCount();
        if (bad) {
            g_Font = 0;
            refreshWatermarkFont();
        }
        ensureUiFont();
    } else {
        ensureUiFont();
    }

    int sw = 0, sh = 0;
    __try {
        VCall<void, int*, int*>(g_Engine, 5, &sw, &sh);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (sw < 2 || sh < 2) {
        HWND game = FindWindowA("Valve001", nullptr);
        if (game) {
            RECT r{};
            GetClientRect(game, &r);
            if (r.right > 0 && r.bottom > 0) {
                sw = r.right;
                sh = r.bottom;
            }
        }
    }
    if (sw < 2 || sh < 2) return;

    updateMenuInput(sw, sh, ingame);

    bool confirmedInGame = false;
    if (ingame && currentNetStats.level[0] && g_Engine && g_EntList) {
        __try {
            const int localIndex = LocalIndex();
            void* localPlayer = localIndex > 0 ? Ent(localIndex) : nullptr;
            confirmedInGame = localPlayer != nullptr && readable(localPlayer, sizeof(void*));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            confirmedInGame = false;
        }
    }

    updateClientFog(confirmedInGame);
    if (ingame) {

        __try { updateCombatFeedback(nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (ingame && g_S.esp) {
        __try { drawEsp(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    if (ingame && (g_S.waterBombTrajectories || g_S.nadeBoostIndicator)) {
        __try { drawTrajectories(sw, sh); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (ingame && (g_S.prediction || g_S.nadeBoostIndicator)) {
        __try { drawPrediction(sw, sh); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (ingame && g_S.speedometer) {
        __try { drawSpeedometerOverlay(sw, sh); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (ingame && g_S.edgebug && g_S.edgebugIndicator) {
        __try { drawEdgebugHitbox(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    if (!isDebugTabActive()) {
        __try { drawWatermark(sw, sh); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    if (g_MenuOpen) {
        __try { drawMenu(sw, sh); } __except (EXCEPTION_EXECUTE_HANDLER) {}

        HCURSOR arrow = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        if (arrow) SetCursor(arrow);
        ClipCursor(nullptr);
    }

    static DWORD lastDisc;
    static char lastPayload[192];
    static int lastRpcEnabled = -1;
    const int rpcEnabled = g_S.discordRpc ? 1 : 0;
    if (rpcEnabled != lastRpcEnabled) {
        discord::SetAppId(rpcEnabled ? kDiscordAppId : "");
        lastRpcEnabled = rpcEnabled;
        lastPayload[0] = '\0';
    }
    if (rpcEnabled) {

        char details[96] = {};
        char state[32] = {};
        if (ingame) {
            if (!g_S.discordShowServer) {
                snprintf(details, sizeof details, "[ Content Deleted ]");
            } else {
                char serverAddress[64] = {};
                const bool foundAddress = queryServerAddress(serverAddress, sizeof(serverAddress));
                if (foundAddress) {
                    strncpy_s(details, sizeof(details), serverAddress, _TRUNCATE);
                } else if (currentNetStats.address[0]) {
                    strncpy_s(details, sizeof(details), currentNetStats.address, _TRUNCATE);
                } else {

                    snprintf(details, sizeof details, "Connecting...");
                }
            }
            snprintf(state, sizeof state, "In game");
        } else {
            snprintf(details, sizeof details, "NaS");
            snprintf(state, sizeof state, "In menu");
        }
        char payload[192];
        snprintf(payload, sizeof payload, "%s|%s", details, state);
        if (strcmp(payload, lastPayload) != 0 || GetTickCount() - lastDisc > 4000) {
            snprintf(lastPayload, sizeof lastPayload, "%s", payload);
            lastDisc = GetTickCount();
            discord::SetStatus(details, state);
        }
    }

    if (g_EjectRequested) {
        static bool started = false;
        if (!started) {
            started = true;

            forceStopMicSpam();
            restoreClientFullBright();
            restoreClientSvCheatsOverride();
            restoreClientPropJumpOverride();
            restoreClientDrawClipBrushes();
            restoreClientFog();
            g_S.networkSettingsEnabled = false;
            applyNetworkSettings(true);
            if (g_Engine) {
                __try { VCall<void, const char*>(g_Engine, 7, "cl_mouseenable 1"); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            g_CleaningUp = true;
            g_HooksReady = false;
            g_MenuOpen = false;
            HANDLE ejectThread = CreateThread(nullptr, 0, ejectThread, nullptr, 0, nullptr);
            if (ejectThread) CloseHandle(ejectThread);
            else logEx(LOG_ERROR, "CreateThread(EjectThread) failed err=%lu", GetLastError());
        }
    }
}

static bool containsNoCase(const char* text, const char* token) {
    if (!text || !token || !*token) return false;
    const size_t tokenLength = strlen(token);
    for (const char* p = text; *p; ++p) {
        if (_strnicmp(p, token, tokenLength) == 0) return true;
    }
    return false;
}

static bool isHudColorPanel(unsigned panel) {
    if (!g_S.hudColorMod || !g_Panel || !panel) return false;
    struct PanelNameCacheEntry {
        unsigned panel;
        DWORD checkedAt;
        LONG epoch;
        bool isHud;
    };

    static PanelNameCacheEntry cache[512]{};
    const DWORD now = GetTickCount();
    const LONG epoch = InterlockedCompareExchange(&g_HudColorPanelCacheEpoch, 0, 0);
    PanelNameCacheEntry& entry = cache[panel % (sizeof(cache) / sizeof(cache[0]))];
    if (entry.panel == panel && entry.epoch == epoch && now - entry.checkedAt < 1000u)
        return entry.isHud;

    const char* name = nullptr;
    __try {
        name = VCall<const char*, unsigned>(g_Panel, 36, panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        entry.panel = panel;
        entry.checkedAt = now;
        entry.epoch = epoch;
        entry.isHud = false;
        return false;
    }
    char safeName[128]{};
    const bool isHud = copyReadableString(name, safeName, sizeof(safeName)) &&
        containsNoCase(safeName, "hud");
    entry.panel = panel;
    entry.checkedAt = now;
    entry.epoch = epoch;
    entry.isHud = isHud;
    return isHud;
}

static bool isHudAccentColor(int r, int g, int b) {

    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
        return false;
    const float rf = static_cast<float>(r) / 255.0f;
    const float gf = static_cast<float>(g) / 255.0f;
    const float bf = static_cast<float>(b) / 255.0f;
    const float hi = (rf > gf) ? ((rf > bf) ? rf : bf) : ((gf > bf) ? gf : bf);
    const float lo = (rf < gf) ? ((rf < bf) ? rf : bf) : ((gf < bf) ? gf : bf);
    const float delta = hi - lo;
    if (hi < 0.25f || delta < 0.22f || hi <= 0.0f) return false;

    float hue = 0.0f;
    if (delta > 0.0f) {
        if (hi == rf) hue = 60.0f * fmodf((gf - bf) / delta, 6.0f);
        else if (hi == gf) hue = 60.0f * (((bf - rf) / delta) + 2.0f);
        else hue = 60.0f * (((rf - gf) / delta) + 4.0f);
        if (hue < 0.0f) hue += 360.0f;
    }
    const float saturation = delta / hi;
    if (saturation < 0.35f) return false;
    const bool synergyOrange = hue >= 25.0f && hue <= 55.0f;
    const bool synergyPurple = hue >= 250.0f && hue <= 285.0f;
    return synergyOrange || synergyPurple;
}

static void applyHudAccentColor(int& r, int& g, int& b) {
    if (!g_S.hudColorMod || g_HudColorPaintDepth <= 0 || g_NocomOverlayDrawing ||
        g_CleaningUp || !g_HooksReady || !isHudAccentColor(r, g, b))
        return;
    r = (std::max)(0, (std::min)(255, static_cast<int>(g_S.hudColorR + 0.5f)));
    g = (std::max)(0, (std::min)(255, static_cast<int>(g_S.hudColorG + 0.5f)));
    b = (std::max)(0, (std::min)(255, static_cast<int>(g_S.hudColorB + 0.5f)));
}

static void __fastcall hkSurfaceDrawSetColor(void* self, void* edx, int r, int g, int b, int a) {
    applyHudAccentColor(r, g, b);
    if (o_SurfaceDrawSetColor) o_SurfaceDrawSetColor(self, r, g, b, a);
}

static void __fastcall hkSurfaceDrawSetTextColor(void* self, void* edx, int r, int g, int b, int a) {
    applyHudAccentColor(r, g, b);
    if (o_SurfaceDrawSetTextColor) o_SurfaceDrawSetTextColor(self, r, g, b, a);
}

static void syncHudColorHooks() {
    const bool shouldBeInstalled = g_S.hudColorMod && g_HooksReady && !g_CleaningUp && !g_EjectRequested;
    const bool installed = g_SurfaceHooks.IsCommitted();
    if (shouldBeInstalled == installed) return;

    if (!shouldBeInstalled) {
        if (installed && g_SurfaceHooks.Restore()) {
            o_SurfaceDrawSetColor = nullptr;
            o_SurfaceDrawSetTextColor = nullptr;
        }
        return;
    }

    const DWORD now = GetTickCount();
    if (g_HudColorHookLastAttempt && now - g_HudColorHookLastAttempt < 1000u) return;
    g_HudColorHookLastAttempt = now;
    if (!g_Surface || !g_SurfaceHooks.Prepare(g_Surface, 160)) return;

    if (g_SurfaceHooks.EntryCount() < 120) {
        g_SurfaceHooks.Restore();
        return;
    }

    o_SurfaceDrawSetColor = reinterpret_cast<SurfaceDrawColorFn>(g_SurfaceHooks.Original(11));
    o_SurfaceDrawSetTextColor = reinterpret_cast<SurfaceDrawColorFn>(g_SurfaceHooks.Original(19));
    const bool configured = o_SurfaceDrawSetColor && o_SurfaceDrawSetTextColor &&
        g_SurfaceHooks.Set(11, reinterpret_cast<void*>(hkSurfaceDrawSetColor)) &&
        g_SurfaceHooks.Set(19, reinterpret_cast<void*>(hkSurfaceDrawSetTextColor));
    if (!configured || !g_SurfaceHooks.Commit()) {
        g_SurfaceHooks.Restore();
        o_SurfaceDrawSetColor = nullptr;
        o_SurfaceDrawSetTextColor = nullptr;
    }
}

static void callOriginalPaintTraverse(void* self, void* edx, unsigned panel, bool force, bool allow) {
    const bool hudPanelScope = isHudColorPanel(panel);
    if (hudPanelScope) ++g_HudColorPaintDepth;
    if (o_PaintTraverse) o_PaintTraverse(self, edx, panel, force, allow);
    if (hudPanelScope && g_HudColorPaintDepth > 0) --g_HudColorPaintDepth;
}

static void __fastcall hkPaintTraverse(void* self, void* edx, unsigned panel, bool force, bool allow) {

    if (isMapTransitionQuiescing()) {
        if (g_HooksReady && !g_CleaningUp && !g_EjectRequested) {
            static DWORD s_lastQuiescedMenuPollTick = 0;
            const DWORD pollTick = GetTickCount();
            if (!s_lastQuiescedMenuPollTick || pollTick - s_lastQuiescedMenuPollTick >= 8u) {
                s_lastQuiescedMenuPollTick = pollTick;
                __try { pollMenuHotkey(false); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
        callOriginalPaintTraverse(self, edx, panel, force, allow);
        return;
    }

    if (!isMapTransitionQuiescing() && g_HooksReady && !g_CleaningUp && !g_EjectRequested) {
        static DWORD s_lastMenuPollTick = 0;
        const DWORD pollTick = GetTickCount();
        if (!s_lastMenuPollTick || pollTick - s_lastMenuPollTick >= 8u) {
            s_lastMenuPollTick = pollTick;
            __try { pollMenuHotkey(true); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    callOriginalPaintTraverse(self, edx, panel, force, allow);
    if (isMapTransitionQuiescing() || !g_HooksReady || g_CleaningUp || g_EjectRequested) return;
    const bool wasNocomOverlayDrawing = g_NocomOverlayDrawing;
    g_NocomOverlayDrawing = true;
    __try {
        paintBody(panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_NocomOverlayDrawing = wasNocomOverlayDrawing;
    syncHudColorHooks();
}

static void unhookAll() {
    g_CleaningUp = true;
    g_HooksReady = false;

    Sleep(400);
    removeLocalMoveTypeHook();
    g_MoveTypeHookReady = false;
    logNocom("hooks removed; engine-facing restores completed on paint thread");

    g_SurfaceHooks.Restore();
    o_SurfaceDrawSetColor = nullptr;
    o_SurfaceDrawSetTextColor = nullptr;
    g_ModelRenderHooks.Restore();
    g_PanelHooks.Restore();
    g_ClientModeHooks.Restore();
    o_OverrideView = nullptr;
    o_KeyInput = nullptr;
    o_GetViewModelFOV = nullptr;
    o_CreateMove = nullptr;
    o_PaintTraverse = nullptr;
    o_DrawModelExecute = nullptr;

    if (o_SetCursor)
        patchIat(reinterpret_cast<void*>(hkSetCursor), reinterpret_cast<void*>(o_SetCursor));
    if (o_SetCursorPos)
        patchIat(reinterpret_cast<void*>(hkSetCursorPos), reinterpret_cast<void*>(o_SetCursorPos));
    if (o_ClipCursor)
        patchIat(reinterpret_cast<void*>(hkClipCursor), reinterpret_cast<void*>(o_ClipCursor));
}

static DWORD WINAPI ejectThread(LPVOID) {
    logNocom("eject requested");

    Sleep(300);
    unhookAll();
    cleanupMenuResources();
    discord::Stop();
    shutdownGdiplus();

    Sleep(250);
    HMODULE self = g_Self;
    FreeLibraryAndExitThread(self, 0);
    return 0;
}

static bool safeInitNetvars() {
    __try {
        netvar::init(VCall<ClientClass*>(g_Client, 8));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static DWORD WINAPI init(LPVOID) {
    installCrashHandlers();
    logEx(LOG_INFO, "init thread started tid=%lu", GetCurrentThreadId());
    logEx(LOG_INFO, "log path=%s", logSessionPath());

    HMODULE client = nullptr;
    HMODULE engine = nullptr;
    for (int i = 0; i < 120; i++) {
        client = GetModuleHandleA("client.dll");
        engine = GetModuleHandleA("engine.dll");
        if (client && engine && GetModuleHandleA("vguimatsurface.dll") &&
            GetModuleHandleA("vgui2.dll") && GetModuleHandleA("vstdlib.dll")) {
            logEx(LOG_INFO, "game modules ready after %d poll(s)", i + 1);
            break;
        }
        if (i == 0 || (i % 10) == 0)
            logEx(LOG_INFO, "waiting for modules... poll=%d client=%p engine=%p",
                i, reinterpret_cast<void*>(client), reinterpret_cast<void*>(engine));
        Sleep(500);
        client = nullptr;
        engine = nullptr;
    }
    if (!client || !engine) {
        logEx(LOG_ERROR, "timed out waiting for client.dll/engine.dll; aborting init");
        return 0;
    }
    Sleep(2000);

    logModuleBase("client.dll", client);
    logModuleBase("engine.dll", engine);
    logModuleBase("vguimatsurface.dll", GetModuleHandleA("vguimatsurface.dll"));
    logModuleBase("vgui2.dll", GetModuleHandleA("vgui2.dll"));
    logModuleBase("vstdlib.dll", GetModuleHandleA("vstdlib.dll"));
    logModuleBase("NOCOM self", g_Self);

    g_Client = getIface("client.dll", "VClient017");
    logEx(LOG_INFO, "iface VClient017 = %p", g_Client);
    g_Engine = getIface("engine.dll", "VEngineClient014");
    logEx(LOG_INFO, "iface VEngineClient014 = %p", g_Engine);
    g_EntList = getIface("client.dll", "VClientEntityList003");
    logEx(LOG_INFO, "iface VClientEntityList003 = %p", g_EntList);
    g_Trace = getIface("engine.dll", "EngineTraceClient003");
    logEx(LOG_INFO, "iface EngineTraceClient003 = %p", g_Trace);
    g_Surface = getIface("vguimatsurface.dll", "VGUI_Surface030");
    if (!g_Surface) {
        g_Surface = getIface("vguimatsurface.dll", "VGUI_Surface031");
        logEx(LOG_INFO, "iface VGUI_Surface030 missing; VGUI_Surface031 = %p", g_Surface);
    } else {
        logEx(LOG_INFO, "iface VGUI_Surface030 = %p", g_Surface);
    }
    g_Panel = getIface("vgui2.dll", "VGUI_Panel009");
    logEx(LOG_INFO, "iface VGUI_Panel009 = %p", g_Panel);

    g_VGuiInput = getIface("vgui2.dll", "VGUI_Input005");
    logEx(LOG_INFO, "iface VGUI_Input005 = %p", g_VGuiInput);
    g_ModelInfo = getIface("engine.dll", "VModelInfoClient006");
    logEx(LOG_INFO, "iface VModelInfoClient006 = %p", g_ModelInfo);

    g_Cvar = getIface("vstdlib.dll", "VEngineCvar004");
    if (!g_Cvar) {
        g_Cvar = getIface("engine.dll", "VEngineCvar004");
        logEx(LOG_INFO, "iface VEngineCvar004 via engine.dll = %p", g_Cvar);
    } else {
        logEx(LOG_INFO, "iface VEngineCvar004 via vstdlib.dll = %p", g_Cvar);
    }
    g_ModelRender = getIface("engine.dll", "VEngineModel016");
    logEx(LOG_INFO, "iface VEngineModel016 = %p", g_ModelRender);

    if (g_Cvar) captureOriginalClientCvars();

    if (!g_Client || !g_Engine || !g_EntList || !g_Trace || !g_Surface || !g_Panel) {
        logEx(LOG_ERROR, "interface lookup failed client=%p engine=%p ent=%p trace=%p surf=%p panel=%p",
            g_Client, g_Engine, g_EntList, g_Trace, g_Surface, g_Panel);
        MessageBoxA(nullptr, "interface lookup failed (wrong game build?)", "NOCOM", MB_OK | MB_ICONERROR);
        return 0;
    }

    if (!safeInitNetvars()) {
        logEx(LOG_ERROR, "netvar init crashed or failed");
        MessageBoxA(nullptr, "netvar init crashed", "NOCOM", MB_OK | MB_ICONERROR);
        return 0;
    }
    logEx(LOG_INFO, "netvar tables initialized");

    const char* hpTables[] = {"DT_BaseEntity", "DT_BaseCombatCharacter", "DT_AI_BaseNPC", "DT_BasePlayer", "DT_HL2MP_Player"};
    for (const char* tn : hpTables)
        if ((o_Health = netvar::Find(tn, "m_iHealth")) >= 0) break;

    o_Armor = -1;
    const char* armorTables[] = {"DT_BasePlayer", "DT_HL2MP_Player", "DT_BaseCombatCharacter", "DT_CSPlayer", "DT_TFPlayer"};
    const char* armorProps[] = {"m_ArmorValue", "m_iArmor", "m_Armor", "m_iArmorValue"};
    for (const char* tableName : armorTables) {
        if (o_Armor >= 0) break;
        for (const char* propName : armorProps) {
            o_Armor = netvar::Find(tableName, propName);
            if (o_Armor >= 0) break;
        }
    }

    o_Origin = netvar::Find("DT_BaseEntity", "m_vecOrigin");
    o_ModelIndex = netvar::Find("DT_BaseEntity", "m_nModelIndex");
    o_TeamNum = netvar::Find("DT_BaseEntity", "m_iTeamNum");
    o_Mins = netvar::Find("DT_BaseEntity", "m_vecMins");
    o_Maxs = netvar::Find("DT_BaseEntity", "m_vecMaxs");
    o_ViewOffset = netvar::Find("DT_BasePlayer", "m_vecViewOffset[0]");
    if (o_ViewOffset < 0) o_ViewOffset = netvar::Find("DT_LocalPlayerExclusive", "m_vecViewOffset[0]");
    o_ActiveWeapon = netvar::Find("DT_BaseCombatCharacter", "m_hActiveWeapon");
    if (o_ActiveWeapon < 0) o_ActiveWeapon = netvar::Find("DT_BasePlayer", "m_hActiveWeapon");
    o_LifeState = netvar::Find("DT_BasePlayer", "m_lifeState");
    o_NpcLife = netvar::Find("DT_AI_BaseNPC", "m_lifeState");
    o_Flags = netvar::Find("DT_BasePlayer", "m_fFlags");
    o_Velocity = netvar::Find("DT_LocalPlayerExclusive", "m_vecVelocity[0]");
    if (o_Velocity < 0) o_Velocity = netvar::Find("DT_BasePlayer", "m_vecVelocity[0]");
    if (o_Velocity < 0) o_Velocity = netvar::Find("DT_BaseEntity", "m_vecVelocity[0]");
    o_PunchAngle = netvar::Find("DT_LocalPlayerExclusive", "m_vecPunchAngle");
    if (o_PunchAngle < 0) o_PunchAngle = netvar::Find("DT_BasePlayer", "m_vecPunchAngle");
    o_PunchAngleVel = netvar::Find("DT_LocalPlayerExclusive", "m_vecPunchAngleVel");
    if (o_PunchAngleVel < 0) o_PunchAngleVel = netvar::Find("DT_BasePlayer", "m_vecPunchAngleVel");
    logEx(LOG_INFO, "netvars health=%d armor=%d origin=%d model=%d team=%d mins=%d maxs=%d viewOff=%d activeWeapon=%d life=%d npcLife=%d flags=%d vel=%d punch=%d punchVel=%d",
        o_Health, o_Armor, o_Origin, o_ModelIndex, o_TeamNum, o_Mins, o_Maxs, o_ViewOffset, o_ActiveWeapon, o_LifeState, o_NpcLife, o_Flags, o_Velocity,
        o_PunchAngle, o_PunchAngleVel);
    g_MoveTypeHookReady = installLocalMoveTypeHook();
    logEx(g_MoveTypeHookReady ? LOG_INFO : LOG_WARN,
        g_MoveTypeHookReady ? "local movetype receive-proxy hook installed" : "local movetype receive-proxy hook unavailable; LadderSpeed detection disabled");

    if (o_Origin < 0 || o_Mins < 0 || o_Maxs < 0) {
        removeLocalMoveTypeHook();
        g_MoveTypeHookReady = false;
        logEx(LOG_ERROR, "critical netvars missing org=%d mins=%d maxs=%d", o_Origin, o_Mins, o_Maxs);
        char m[180];
        snprintf(m, sizeof m, "critical netvars missing org=%d mins=%d maxs=%d", o_Origin, o_Mins, o_Maxs);
        MessageBoxA(nullptr, m, "NOCOM", MB_OK | MB_ICONERROR);
        return 0;
    }

    for (int i = 0; i < 60 && !g_ClientMode; i++) {
        g_ClientMode = findClientMode(client);
        if (!g_ClientMode) {
            if (i == 0 || (i % 10) == 0)
                logEx(LOG_INFO, "ClientMode not ready yet poll=%d", i);
            Sleep(500);
        }
    }
    if (!g_ClientMode) {
        removeLocalMoveTypeHook();
        g_MoveTypeHookReady = false;
        logEx(LOG_ERROR, "ClientMode not found (offset mismatch); sourceOffset=0x%X",
            static_cast<unsigned>(g_ClientModeSourceOffset));
        MessageBoxA(nullptr, "could not find ClientMode (offset mismatch)", "NOCOM", MB_OK | MB_ICONERROR);
        return 0;
    }
    logEx(LOG_INFO, "ClientMode=%p sourceOffset=0x%X", g_ClientMode, static_cast<unsigned>(g_ClientModeSourceOffset));

    srand(GetTickCount());

    cfgDir();
    logEx(LOG_INFO, "Made by unconnected with love.");
    logEx(LOG_INFO, "Initalizing NOCOM!");
    discord::Start();

    loadActiveName();
    refreshCfgs();
    loadConfig(g_CfgName);
    discord::SetAppId(g_S.discordRpc ? kDiscordAppId : "");
    discord::SetStatus("NOCOM", "In menu");

    HMODULE u32 = GetModuleHandleA("user32.dll");
    if (u32) {
        o_SetCursor = reinterpret_cast<SetCursorFn>(GetProcAddress(u32, "SetCursor"));
        o_SetCursorPos = reinterpret_cast<SetCursorPosFn>(GetProcAddress(u32, "SetCursorPos"));
        o_ClipCursor = reinterpret_cast<ClipCursorFn>(GetProcAddress(u32, "ClipCursor"));
        if (o_SetCursor) g_SetCursorIatPatches = patchIat(reinterpret_cast<void*>(o_SetCursor), reinterpret_cast<void*>(hkSetCursor));
        if (o_SetCursorPos) g_SetCursorPosIatPatches = patchIat(reinterpret_cast<void*>(o_SetCursorPos), reinterpret_cast<void*>(hkSetCursorPos));
        if (o_ClipCursor) g_ClipCursorIatPatches = patchIat(reinterpret_cast<void*>(o_ClipCursor), reinterpret_cast<void*>(hkClipCursor));
    }

    const bool clientModePrepared = g_ClientModeHooks.Prepare(g_ClientMode, 64);
    const bool panelPrepared = g_PanelHooks.Prepare(g_Panel, 64);

    const bool modelRenderPrepared = g_ModelRender && g_ModelRenderHooks.Prepare(g_ModelRender, 26);
    logEx(LOG_INFO, "hook prepare clientMode=%d panel=%d modelRender=%d (modelIface=%p)",
        clientModePrepared ? 1 : 0, panelPrepared ? 1 : 0, modelRenderPrepared ? 1 : 0, g_ModelRender);
    if (!clientModePrepared || !panelPrepared) {
        logEx(LOG_ERROR, "failed to prepare Source interface hooks");
        MessageBoxA(nullptr, "could not prepare Source interface hooks", "NOCOM", MB_OK | MB_ICONERROR);
        unhookAll();
        return 0;
    }

    o_OverrideView = reinterpret_cast<OverrideViewFn>(g_ClientModeHooks.Original(16));
    o_KeyInput = reinterpret_cast<KeyInputFn>(g_ClientModeHooks.Original(17));
    o_GetViewModelFOV = reinterpret_cast<GetViewModelFOVFn>(g_ClientModeHooks.Original(32));
    o_CreateMove = reinterpret_cast<CreateMoveFn>(g_ClientModeHooks.Original(21));
    o_PaintTraverse = reinterpret_cast<PaintTraverseFn>(g_PanelHooks.Original(41));
    if (modelRenderPrepared)
        o_DrawModelExecute = reinterpret_cast<DrawModelExecuteFn>(g_ModelRenderHooks.Original(19));
    logEx(LOG_INFO, "orig OverrideView=%p KeyInput=%p GetVMFOV=%p CreateMove=%p PaintTraverse=%p DME=%p",
        reinterpret_cast<void*>(o_OverrideView), reinterpret_cast<void*>(o_KeyInput),
        reinterpret_cast<void*>(o_GetViewModelFOV), reinterpret_cast<void*>(o_CreateMove),
        reinterpret_cast<void*>(o_PaintTraverse), reinterpret_cast<void*>(o_DrawModelExecute));

    const bool baseHooksConfigured = o_OverrideView && o_KeyInput && o_GetViewModelFOV && o_CreateMove && o_PaintTraverse &&
        g_ClientModeHooks.Set(16, reinterpret_cast<void*>(hkOverrideView)) &&
        g_ClientModeHooks.Set(17, reinterpret_cast<void*>(hkKeyInput)) &&
        g_ClientModeHooks.Set(32, reinterpret_cast<void*>(hkGetViewModelFOV)) &&
        g_ClientModeHooks.Set(21, reinterpret_cast<void*>(hkCreateMove)) &&
        g_PanelHooks.Set(41, reinterpret_cast<void*>(hkPaintTraverse));
    const bool modelHookConfigured = modelRenderPrepared && o_DrawModelExecute &&
        g_ModelRenderHooks.Set(19, reinterpret_cast<void*>(hkDrawModelExecute));
    const bool hooksConfigured = baseHooksConfigured;
    const bool clientModeCommitted = hooksConfigured && g_ClientModeHooks.Commit();
    const bool panelCommitted = clientModeCommitted && g_PanelHooks.Commit();
    if (panelCommitted && modelHookConfigured)
        g_ModelRenderHooks.Commit();
    logEx(LOG_INFO, "hook commit base=%d clientMode=%d panel=%d model=%d",
        hooksConfigured ? 1 : 0, clientModeCommitted ? 1 : 0, panelCommitted ? 1 : 0, modelHookConfigured ? 1 : 0);
    if (!panelCommitted) {
        logEx(LOG_ERROR, "Source interface hook installation failed");
        MessageBoxA(nullptr, "Source interface hook installation failed", "NOCOM", MB_OK | MB_ICONERROR);
        unhookAll();
        return 0;
    }

    g_HooksReady = true;
    ensureGameSoundCatalog();
    logEx(LOG_INFO, "game sounds catalog size=%zu", gameSoundCatalog().size());
    logEx(LOG_INFO, "Done!");
    return 0;
}

static unsigned int getModuleImageSize(HMODULE module) {
    if (!module) return 0;
    auto base = reinterpret_cast<uint8_t*>(module);
    __try {
        if (!readable(base, sizeof(IMAGE_DOS_HEADER))) return 0;
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return 0;
        if (!readable(base + dos->e_lfanew, sizeof(IMAGE_NT_HEADERS))) return 0;
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        return nt->OptionalHeader.SizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int getHookDebugLines(wchar_t lines[][128], int maxLines) {
    if (!lines || maxLines <= 0) return 0;
    int n = 0;
#define ADD_DEBUG_LINE(...) do { if (n < maxLines) { swprintf(lines[n], 128, __VA_ARGS__); ++n; } } while (0)
    HMODULE clientModule = GetModuleHandleA("client.dll");
    HMODULE engineModule = GetModuleHandleA("engine.dll");
    HMODULE panelModule = GetModuleHandleA("vgui2.dll");
    const auto address = [](const void* p) -> unsigned int {
        return static_cast<unsigned int>(reinterpret_cast<uintptr_t>(p));
    };

    ADD_DEBUG_LINE(L"ready=%d cleanup=%d menu=%d", g_HooksReady ? 1 : 0, g_CleaningUp ? 1 : 0, g_MenuOpen ? 1 : 0);
    ADD_DEBUG_LINE(L"cursor IAT SetCursor=%d Pos=%d Clip=%d", g_SetCursorIatPatches, g_SetCursorPosIatPatches, g_ClipCursorIatPatches);
    ADD_DEBUG_LINE(L"client.dll base=%08X size=%08X", address(clientModule), getModuleImageSize(clientModule));
    ADD_DEBUG_LINE(L"ClientMode=%08X found@+%08X", address(g_ClientMode), static_cast<unsigned int>(g_ClientModeSourceOffset));
    ADD_DEBUG_LINE(L"CM vtable orig=%08X shadow=%08X", address(g_ClientModeHooks.OriginalTable()), address(g_ClientModeHooks.ShadowTable()));
    ADD_DEBUG_LINE(L"OV16 %08X > %08X on=%d", address(reinterpret_cast<void*>(o_OverrideView)), address(reinterpret_cast<void*>(hkOverrideView)), g_ClientModeHooks.IsCommitted() ? 1 : 0);
    ADD_DEBUG_LINE(L"VMFOV32 %08X > %08X on=%d", address(reinterpret_cast<void*>(o_GetViewModelFOV)), address(reinterpret_cast<void*>(hkGetViewModelFOV)), g_ClientModeHooks.IsCommitted() ? 1 : 0);
    ADD_DEBUG_LINE(L"CM17 KeyInput %08X > %08X on=%d", address(reinterpret_cast<void*>(o_KeyInput)), address(reinterpret_cast<void*>(hkKeyInput)), g_ClientModeHooks.IsCommitted() ? 1 : 0);
    ADD_DEBUG_LINE(L"CM21 %08X > %08X on=%d", address(reinterpret_cast<void*>(o_CreateMove)), address(reinterpret_cast<void*>(hkCreateMove)), g_ClientModeHooks.IsCommitted() ? 1 : 0);
    ADD_DEBUG_LINE(L"engine.dll=%08X Engine=%08X", address(engineModule), address(g_Engine));
    ADD_DEBUG_LINE(L"vgui2.dll=%08X Panel=%08X", address(panelModule), address(g_Panel));
    ADD_DEBUG_LINE(L"Panel vtable orig=%08X shadow=%08X", address(g_PanelHooks.OriginalTable()), address(g_PanelHooks.ShadowTable()));
    ADD_DEBUG_LINE(L"PT41 %08X > %08X on=%d", address(reinterpret_cast<void*>(o_PaintTraverse)), address(reinterpret_cast<void*>(hkPaintTraverse)), g_PanelHooks.IsCommitted() ? 1 : 0);
    ADD_DEBUG_LINE(L"ModelRender=%08X ModelInfo=%08X", address(g_ModelRender), address(g_ModelInfo));
    ADD_DEBUG_LINE(L"Model vtable orig=%08X shadow=%08X", address(g_ModelRenderHooks.OriginalTable()), address(g_ModelRenderHooks.ShadowTable()));
    ADD_DEBUG_LINE(L"DME19 %08X > %08X on=%d", address(reinterpret_cast<void*>(o_DrawModelExecute)), address(reinterpret_cast<void*>(hkDrawModelExecute)), g_ModelRenderHooks.IsCommitted() ? 1 : 0);
    ADD_DEBUG_LINE(L"ICvar=%08X VEngineCvar004 FindVar slot=12", address(g_Cvar));
    ADD_DEBUG_LINE(L"FullBright=%d original=%d valid=%d applied=%d failures=%ld", g_S.fullBright ? 1 : 0, g_OriginalFullBright, g_OriginalFullBrightValid ? 1 : 0, g_LastAppliedFullBright, InterlockedCompareExchange(&g_ClientCvarWriteFailures, 0, 0));
    ADD_DEBUG_LINE(L"FullBright flag-bypass calls=%ld", getClientFullBrightFlagBypassCount());
    ADD_DEBUG_LINE(L"VM FOV hook=slot32 on=%d pos=%d val=%.1f", g_S.viewmodelFovOn ? 1 : 0, g_S.viewmodelOn ? 1 : 0, g_S.viewmodelFov);
    ADD_DEBUG_LINE(L"VM FOV calls=%ld writes=%ld setupWrites=%ld last=%.1f", InterlockedCompareExchange(&g_ViewmodelFovHookCalls, 0, 0), InterlockedCompareExchange(&g_ViewmodelFovHookWrites, 0, 0), InterlockedCompareExchange(&g_ViewmodelFovSetupWrites, 0, 0), static_cast<double>(g_LastViewmodelFovWritten));
    ADD_DEBUG_LINE(L"Viewmodel offset X/Y/Z %.1f %.1f %.1f", g_S.viewmodelX, g_S.viewmodelY, g_S.viewmodelZ);
    ADD_DEBUG_LINE(L"client=%08X ents=%08X trace=%08X", address(g_Client), address(g_EntList), address(g_Trace));
    ADD_DEBUG_LINE(L"surface=%08X health=%d origin=%d flags=%d", address(g_Surface), o_Health, o_Origin, o_Flags);
    ADD_DEBUG_LINE(L"MoveType hook=%d local move type=%d on ladder=%d", g_MoveTypeHookReady ? 1 : 0, getLocalMoveType(), isLocalOnLadder() ? 1 : 0);
#undef ADD_DEBUG_LINE
    return n;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_Self = h;
        DisableThreadLibraryCalls(h);

        HANDLE t = CreateThread(nullptr, 0, init, nullptr, 0, nullptr);
        if (t) {
            CloseHandle(t);
        } else {
            OutputDebugStringA("[NOCOM] CreateThread(Init) failed; initialization cancelled.\r\n");
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        // Join the worker before unloading code it may still execute

        if (g_LogCsInit) {
            logEx(LOG_INFO, "DLL_PROCESS_DETACH hooksReady=%d cleaning=%d",
                g_HooksReady ? 1 : 0, g_CleaningUp ? 1 : 0);
        }
        g_HooksReady = false;
        g_CleaningUp = true;
    }
    return TRUE;
}
