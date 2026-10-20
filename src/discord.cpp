#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "discord.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
CRITICAL_SECTION g_cs;
bool g_csReady = false;
HANDLE g_thread = nullptr;
volatile bool g_stop = false;
volatile bool g_ready = false;
volatile bool g_reconnect = false;
char g_appId[24] = {};
char g_details[96] = "NOCOM";
char g_state[96] = "Idle";
unsigned long long g_started = 0;

void copyLocked(char* id, char* details, char* state, bool* reconnect) {
    EnterCriticalSection(&g_cs);
    snprintf(id, 24, "%s", g_appId);
    snprintf(details, 96, "%s", g_details);
    snprintf(state, 96, "%s", g_state);
    *reconnect = g_reconnect;
    g_reconnect = false;
    LeaveCriticalSection(&g_cs);
}

bool writeAll(HANDLE pipe, const void* data, DWORD bytes) {
    const unsigned char* cursor = static_cast<const unsigned char*>(data);
    DWORD sent = 0;
    while (sent < bytes) {
        DWORD written = 0;
        if (!WriteFile(pipe, cursor + sent, bytes - sent, &written, nullptr) || written == 0) return false;
        sent += written;
    }
    return true;
}

bool readExact(HANDLE pipe, void* data, DWORD bytes, DWORD timeoutMs) {
    unsigned char* cursor = static_cast<unsigned char*>(data);
    DWORD received = 0;
    const DWORD start = GetTickCount();
    while (received < bytes) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return false;
        if (available > 0) {
            DWORD chunk = bytes - received;
            if (chunk > available) chunk = available;
            DWORD got = 0;
            if (!ReadFile(pipe, cursor + received, chunk, &got, nullptr) || got == 0) return false;
            received += got;
        } else {
            if (GetTickCount() - start >= timeoutMs) return false;
            Sleep(5);
        }
    }
    return true;
}

bool readFrame(HANDLE pipe, unsigned& op, std::string& payload, DWORD timeoutMs) {
    unsigned hdr[2] = {};
    if (!readExact(pipe, hdr, sizeof(hdr), timeoutMs) || hdr[1] > (1u << 20)) return false;
    op = hdr[0];
    payload.assign(hdr[1], '\0');
    if (hdr[1] && !readExact(pipe, &payload[0], hdr[1], timeoutMs)) return false;
    return true;
}

void appendJsonString(char* out, size_t capacity, size_t& at, const char* in) {
    if (!out || capacity == 0 || at >= capacity) return;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in ? in : ""); *p && at + 2 < capacity; ++p) {
        switch (*p) {
        case '\\': out[at++] = '\\'; out[at++] = '\\'; break;
        case '"': out[at++] = '\\'; out[at++] = '"'; break;
        case '\b': out[at++] = '\\'; out[at++] = 'b'; break;
        case '\f': out[at++] = '\\'; out[at++] = 'f'; break;
        case '\n': out[at++] = '\\'; out[at++] = 'n'; break;
        case '\r': out[at++] = '\\'; out[at++] = 'r'; break;
        case '\t': out[at++] = '\\'; out[at++] = 't'; break;
        default:
            if (*p < 0x20) out[at++] = ' ';
            else out[at++] = static_cast<char>(*p);
            break;
        }
    }
    out[at] = '\0';
}

bool writeFrame(HANDLE pipe, unsigned op, const char* json) {
    unsigned hdr[2] = {op, static_cast<unsigned>(strlen(json))};
    return writeAll(pipe, hdr, sizeof(hdr)) && writeAll(pipe, json, hdr[1]);
}

void drain(HANDLE pipe);

void clearActivity(HANDLE pipe, int& nonce) {
    if (pipe == INVALID_HANDLE_VALUE) return;
    char json[192];
    snprintf(json, sizeof(json),
             "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%d\",\"args\":{\"pid\":%lu,\"activity\":null}}",
             nonce++, GetCurrentProcessId());
    if (writeFrame(pipe, 1, json)) drain(pipe);
}

void drain(HANDLE pipe) {
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr) || avail < 8) return;
        unsigned hdr[2] = {};
        DWORD r = 0;
        if (!ReadFile(pipe, hdr, 8, &r, nullptr) || r != 8) return;
        unsigned len = hdr[1];
        if (len > (1u << 20)) return;
        unsigned got = 0;
        char junk[512];
        while (got < len) {
            unsigned chunk = len - got;
            if (chunk > sizeof junk) chunk = (unsigned)sizeof junk;
            DWORD n = 0;
            if (!ReadFile(pipe, junk, chunk, &n, nullptr) || n == 0) return;
            got += n;
        }
    }
}

HANDLE openPipe() {
    for (int i = 0; i < 10; i++) {
        wchar_t name[64];
        swprintf(name, 64, L"\\\\.\\pipe\\discord-ipc-%d", i);
        HANDLE h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) return h;
    }
    return INVALID_HANDLE_VALUE;
}

bool push(HANDLE pipe, const char* details, const char* state, int& nonce) {
    char escapedDetails[512] = {};
    char escapedState[512] = {};
    size_t detailsAt = 0, stateAt = 0;
    appendJsonString(escapedDetails, sizeof(escapedDetails), detailsAt, details);
    appendJsonString(escapedState, sizeof(escapedState), stateAt, state);
    char json[1536];
    snprintf(json, sizeof json,
             "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%d\",\"args\":{\"pid\":%lu,\"activity\":{" 
             "\"details\":\"%s\",\"state\":\"%s\",\"timestamps\":{\"start\":%llu},"
             "\"assets\":{\"large_text\":\"NOCOM\"},\"instance\":false}}}",
             nonce++, GetCurrentProcessId(), escapedDetails, escapedState, g_started);
    if (!writeFrame(pipe, 1, json)) return false;
    drain(pipe);
    return true;
}

DWORD WINAPI threadProc(LPVOID) {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    int nonce = 1;
    char sentDetails[96] = {};
    char sentState[96] = {};
    DWORD lastPush = 0;
    while (!g_stop) {
        char id[24], details[96], state[96];
        bool reconnect = false;
        copyLocked(id, details, state, &reconnect);
        if (reconnect && pipe != INVALID_HANDLE_VALUE) {
            clearActivity(pipe, nonce);
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
            g_ready = false;
        }
        if (!id[0]) {
            g_ready = false;
            if (pipe != INVALID_HANDLE_VALUE) { CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE; }
            Sleep(400);
            continue;
        }
        if (pipe == INVALID_HANDLE_VALUE) {
            pipe = openPipe();
            if (pipe == INVALID_HANDLE_VALUE) { Sleep(2000); continue; }
            char hello[80];
            snprintf(hello, sizeof hello, "{\"v\":1,\"client_id\":\"%s\"}", id);
            if (!writeFrame(pipe, 0, hello)) {
                CloseHandle(pipe);
                pipe = INVALID_HANDLE_VALUE;
                Sleep(2000);
                continue;
            }
            unsigned readyOpcode = 0;
            std::string readyPayload;
            if (!readFrame(pipe, readyOpcode, readyPayload, 3000) || readyOpcode != 1 ||
                readyPayload.find("\"READY\"") == std::string::npos) {
                CloseHandle(pipe);
                pipe = INVALID_HANDLE_VALUE;
                g_ready = false;
                Sleep(1000);
                continue;
            }
            g_ready = true;
            sentDetails[0] = sentState[0] = 0;
            lastPush = 0;
        }
        bool changed = strcmp(sentDetails, details) != 0 || strcmp(sentState, state) != 0;
        DWORD now = GetTickCount();
        if (changed || now - lastPush > 20000) {
            if (!push(pipe, details, state, nonce)) {
                CloseHandle(pipe);
                pipe = INVALID_HANDLE_VALUE;
                g_ready = false;
                Sleep(1000);
                continue;
            }
            snprintf(sentDetails, sizeof sentDetails, "%s", details);
            snprintf(sentState, sizeof sentState, "%s", state);
            lastPush = now;
        } else {
            drain(pipe);
        }
        Sleep(250);
    }
    if (pipe != INVALID_HANDLE_VALUE) {
        clearActivity(pipe, nonce);
        CloseHandle(pipe);
    }
    g_ready = false;
    return 0;
}
}

void discord::Start() {
    if (g_thread) return;
    if (!g_csReady) {
        if (!InitializeCriticalSectionAndSpinCount(&g_cs, 2000))
            return;
        g_csReady = true;
    }
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    g_started = u.QuadPart / 10000000ULL - 11644473600ULL;
    g_stop = false;
    g_thread = CreateThread(nullptr, 0, threadProc, nullptr, 0, nullptr);
    if (!g_thread && g_csReady) {
        DeleteCriticalSection(&g_cs);
        g_csReady = false;
    }
}

void discord::Stop() {
    g_stop = true;
    if (g_thread) {
        // Join the worker before unloading code it may still execute

        CancelSynchronousIo(g_thread);
        const DWORD waitResult = WaitForSingleObject(g_thread, INFINITE);
        if (waitResult != WAIT_OBJECT_0)
            return;
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    g_ready = false;
    if (g_csReady) {
        DeleteCriticalSection(&g_cs);
        g_csReady = false;
    }
}

void discord::SetAppId(const char* id) {
    if (!g_csReady) return;
    char clean[24] = {};
    int j = 0;
    if (id) {
        for (int i = 0; id[i] && j < 20; i++)
            if (id[i] >= '0' && id[i] <= '9') clean[j++] = id[i];
    }
    EnterCriticalSection(&g_cs);
    if (strcmp(g_appId, clean) != 0) {
        snprintf(g_appId, sizeof g_appId, "%s", clean);
        g_reconnect = true;
    }
    LeaveCriticalSection(&g_cs);
}

void discord::SetStatus(const char* details, const char* state) {
    if (!g_csReady) return;
    EnterCriticalSection(&g_cs);
    snprintf(g_details, sizeof g_details, "%s", details && details[0] ? details : "NOCOM");
    snprintf(g_state, sizeof g_state, "%s", state && state[0] ? state : "Idle");
    LeaveCriticalSection(&g_cs);
}

bool discord::Ready() { return g_ready; }

void discord::CopyAppId(char* out, int n) {
    if (!out || n < 1) return;
    out[0] = 0;
    if (!g_csReady) return;
    EnterCriticalSection(&g_cs);
    snprintf(out, n, "%s", g_appId);
    LeaveCriticalSection(&g_cs);
}
