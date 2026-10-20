#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "combat_feedback.h"
#include "game.h"
#include "settings.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <mmsystem.h>

static bool g_DeathSoundPlaying = false;
static volatile LONG g_DeathStateLatched = 0;
static std::vector<std::string> g_GameSounds;
static bool g_GameSoundsReady = false;
static std::vector<std::string> g_SoundSearchRoots;

static void collectSoundFiles(const std::string& root, const std::string& relPrefix) {
    const std::string pattern = root + "\\*";
    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == '.' && fd.cFileName[2] == 0)))
            continue;
        const std::string name = fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            collectSoundFiles(root + "\\" + name,
                relPrefix.empty() ? name : (relPrefix + "/" + name));
            continue;
        }
        const char* dot = strrchr(fd.cFileName, '.');
        if (!dot || _stricmp(dot, ".wav") != 0) continue;
        std::string rel = relPrefix.empty() ? name : (relPrefix + "/" + name);
        if (rel.size() > 4) rel.resize(rel.size() - 4);
        for (char& c : rel) if (c == '\\') c = '/';
        g_GameSounds.push_back(rel);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static void addSoundRoot(std::string path) {
    while (!path.empty() && (path.back() == '\\' || path.back() == '/')) path.pop_back();
    if (path.size() < 6) return;
    for (const std::string& existing : g_SoundSearchRoots)
        if (_stricmp(existing.c_str(), path.c_str()) == 0) return;
    const DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return;
    g_SoundSearchRoots.push_back(path);
}

static void discoverSoundRoots() {
    g_SoundSearchRoots.clear();
    char modulePath[MAX_PATH]{};
    HMODULE modules[] = {
        GetModuleHandleA("engine.dll"), GetModuleHandleA("client.dll"),
        GetModuleHandleA("synergy.exe"), GetModuleHandleA(nullptr)
    };
    for (HMODULE module : modules) {
        if (!module || !GetModuleFileNameA(module, modulePath, MAX_PATH) || !modulePath[0]) continue;
        std::string current = modulePath;
        const size_t slash = current.find_last_of("\\/");
        if (slash == std::string::npos) continue;
        current.resize(slash);
        for (int up = 0; up < 6; ++up) {
            addSoundRoot(current + "\\sound");
            addSoundRoot(current + "\\hl2\\sound");
            addSoundRoot(current + "\\synergy\\sound");
            addSoundRoot(current + "\\synergy\\hl2\\sound");
            addSoundRoot(current + "\\hl2\\hl2\\sound");
            const size_t parent = current.find_last_of("\\/");
            if (parent == std::string::npos) break;
            current.resize(parent);
        }
    }
}

void ensureGameSoundCatalog() {
    if (g_GameSoundsReady) return;
    g_GameSoundsReady = true;
    g_GameSounds.clear();
    discoverSoundRoots();
    for (const std::string& root : g_SoundSearchRoots) collectSoundFiles(root, "");
    std::sort(g_GameSounds.begin(), g_GameSounds.end());
    g_GameSounds.erase(std::unique(g_GameSounds.begin(), g_GameSounds.end()), g_GameSounds.end());
}

const std::vector<std::string>& gameSoundCatalog() {
    ensureGameSoundCatalog();
    return g_GameSounds;
}

int findGameSoundIndex(const std::string& name) {
    ensureGameSoundCatalog();
    if (name.empty()) return -1;
    for (int i = 0; i < static_cast<int>(g_GameSounds.size()); ++i)
        if (_stricmp(g_GameSounds[i].c_str(), name.c_str()) == 0) return i;
    return -1;
}

static bool looksLikeAbsoluteFile(const char* path) {
    if (!path || !path[0]) return false;
    if (path[0] == '\\' || path[0] == '/') return true;
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':')
        return true;
    const char* dot = strrchr(path, '.');
    return dot && (!_stricmp(dot, ".wav") || !_stricmp(dot, ".mp3") || !_stricmp(dot, ".ogg")) &&
        (strchr(path, '\\') || strchr(path, '/'));
}

static std::string resolveGameSoundToFile(const char* rel) {
    if (!rel || !rel[0]) return {};
    ensureGameSoundCatalog();
    std::string name = rel;
    for (char& c : name) if (c == '\\') c = '/';
    if (name.size() > 4) {
        const char* ext = name.c_str() + name.size() - 4;
        if (!_stricmp(ext, ".wav") || !_stricmp(ext, ".mp3") || !_stricmp(ext, ".ogg"))
            name.resize(name.size() - 4);
    }
    for (const std::string& root : g_SoundSearchRoots) {
        std::string disk = root + "\\" + name + ".wav";
        for (char& c : disk) if (c == '/') c = '\\';
        const DWORD attr = GetFileAttributesA(disk.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) return disk;
    }
    return {};
}

static void stopFileSound(const char* alias) {
    PlaySoundA(nullptr, nullptr, 0);
    if (!alias || !alias[0]) return;
    char cmd[128]{};
    _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "stop %s", alias);
    mciSendStringA(cmd, nullptr, 0, nullptr);
    _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "close %s", alias);
    mciSendStringA(cmd, nullptr, 0, nullptr);
}

static bool playResolvedFile(const char* path, const char* alias, int volumePercent) {
    if (!path || !path[0] || !alias || !alias[0]) return false;
    volumePercent = (std::max)(0, (std::min)(100, volumePercent));
    if (volumePercent == 0) return false;

    char cmd[4096]{};
    _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "close %s", alias);
    mciSendStringA(cmd, nullptr, 0, nullptr);

    const char* ext = strrchr(path, '.');
    const bool wav = ext && !_stricmp(ext, ".wav");
    const bool compressed = ext && (!_stricmp(ext, ".mp3") || !_stricmp(ext, ".ogg"));
    MCIERROR error = 1;
    if (wav) {
        _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "open \"%s\" type waveaudio alias %s", path, alias);
        error = mciSendStringA(cmd, nullptr, 0, nullptr);
    } else if (compressed) {
        _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "open \"%s\" type mpegvideo alias %s", path, alias);
        error = mciSendStringA(cmd, nullptr, 0, nullptr);
    }
    if (error != 0) {
        _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "open \"%s\" alias %s", path, alias);
        error = mciSendStringA(cmd, nullptr, 0, nullptr);
    }
    if (error != 0) {

        if (wav && volumePercent == 100)
            return PlaySoundA(path, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT) != FALSE;
        return false;
    }

    _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "setaudio %s volume to %d", alias, volumePercent * 10);
    mciSendStringA(cmd, nullptr, 0, nullptr);
    _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "play %s from 0", alias);
    if (mciSendStringA(cmd, nullptr, 0, nullptr) == 0) return true;

    _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "close %s", alias);
    mciSendStringA(cmd, nullptr, 0, nullptr);
    if (wav && volumePercent == 100)
        return PlaySoundA(path, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT) != FALSE;
    return false;
}

// Keep SEH inside a POD-only wrapper
static bool safeClientCmd(const char* command) {
    if (!g_Engine || !command || !command[0]) return false;
    __try { VCall<void, const char*>(g_Engine, 7, command); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool playGameSound(const char* name, int volumePercent) {
    if (!name || !name[0]) return false;
    volumePercent = (std::max)(0, (std::min)(100, volumePercent));
    if (volumePercent == 0) return false;

    for (const unsigned char* c = reinterpret_cast<const unsigned char*>(name); *c; ++c) {
        if (!(std::isalnum(*c) || *c == '/' || *c == '\\' || *c == '_' || *c == '-' || *c == '.'))
            return false;
    }

    char cleanName[256]{};
    strncpy_s(cleanName, name, _TRUNCATE);
    const size_t length = strlen(cleanName);
    if (length > 4) {
        char* ext = cleanName + length - 4;
        if (!_stricmp(ext, ".wav") || !_stricmp(ext, ".mp3") || !_stricmp(ext, ".ogg"))
            *ext = '\0';
    }
    char command[320]{};
    _snprintf_s(command, sizeof(command), _TRUNCATE, "playvol %s %.2f", cleanName, volumePercent / 100.f);
    return safeClientCmd(command);
}

static bool playNocomSound(const char* selection, const char* alias, int volumePercent) {
    if (!selection || !selection[0]) return false;
    if (volumePercent <= 0) return false;
    if (looksLikeAbsoluteFile(selection)) {
        const DWORD attr = GetFileAttributesA(selection);
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) return false;
        return playResolvedFile(selection, alias, volumePercent);
    }
    const std::string localFile = resolveGameSoundToFile(selection);
    if (!localFile.empty() && playResolvedFile(localFile.c_str(), alias, volumePercent)) return true;

    return playGameSound(selection, volumePercent);
}

static void stopDeathSound() {
    if (!g_DeathSoundPlaying) return;
    g_DeathSoundPlaying = false;
    stopFileSound("nocom_death");
}

static bool startDeathSound() {
    if (g_DeathSoundPlaying) return true;
    if (g_S.deathSoundVolume <= 0.f) {

        g_DeathSoundPlaying = true;
        return true;
    }
    bool started = playNocomSound(g_S.deathSoundPath.c_str(), "nocom_death",
                                  static_cast<int>(g_S.deathSoundVolume));
    if (!started && g_S.deathSoundPath != "buttons/button14")
        started = playNocomSound("buttons/button14", "nocom_death", static_cast<int>(g_S.deathSoundVolume));
    g_DeathSoundPlaying = started;
    return started;
}

static int safeEntityInt(void* entity, int offset, int fallback) {
    if (!entity || offset < 0) return fallback;
    __try { return Field<int>(entity, offset); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}

static int safeEntityU8(void* entity, int offset, int fallback) {
    if (!entity || offset < 0) return fallback;
    __try { return static_cast<int>(Field<uint8_t>(entity, offset)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}

void previewNocomSound(const std::string& selection, int volumePercent) {
    playNocomSound(selection.c_str(), "nocom_preview", volumePercent);
}

void updateCombatFeedback(CUserCmd* cmd) {
    (void)cmd;
    if (!g_S.deathSound || !InGame()) {
        InterlockedExchange(&g_DeathStateLatched, 0);
        if (g_DeathSoundPlaying) stopDeathSound();
        return;
    }

    void* local = Ent(LocalIndex());
    if (!local) return;
    bool dead = false;
    if (o_LifeState >= 0 && safeEntityU8(local, o_LifeState, 0) != 0) dead = true;
    if (o_Health >= 0 && safeEntityInt(local, o_Health, 1) <= 0) dead = true;

    if (dead) {
        if (InterlockedCompareExchange(&g_DeathStateLatched, 1, 0) == 0) {
            if (!startDeathSound()) InterlockedExchange(&g_DeathStateLatched, 0);
        }
    } else {
        const LONG wasLatched = InterlockedExchange(&g_DeathStateLatched, 0);
        if (wasLatched != 0 || g_DeathSoundPlaying) stopDeathSound();
    }
}
