#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "render.h"
#include "game.h"
#include "settings.h"
#include "config.h"
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <fstream>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

unsigned long g_Font;
static unsigned long g_WmFont = 0;
static unsigned long g_SpeedFont = 0;
static std::string g_SpeedFontFace;
static int g_SpeedFontHeight = 0;
static int g_WmFontPx = 0;
static ULONG_PTR g_GdipToken = 0;

static int g_ImgTex = -1;
static int g_ImgW = 0, g_ImgH = 0;
static int g_ImgContentW = 0, g_ImgContentH = 0;
static int g_ImgTexContentW = 0, g_ImgTexContentH = 0;  
static std::string g_ImgLoadedPath;

static DWORD g_ImgLastAttempt = 0;
static bool g_ImgLoadFailed = false;
static DWORD g_WmFontLastCheck = 0;
static DWORD g_WmFontLastCreateAttempt = 0;
static DWORD g_WmFontLastConfigured = 0;

struct DecodedWatermarkFrame {
    std::vector<unsigned char> rgba;
    DWORD delayMs = 100;
};

struct DecodedWatermarkImage {
    std::string path;
    int textureWidth = 0, textureHeight = 0;
    int contentWidth = 0, contentHeight = 0;
    std::vector<DecodedWatermarkFrame> frames;
    bool success = false;
};
static std::mutex g_ImgResultMutex;
static std::mutex g_ImgWorkerMutex;
static std::condition_variable g_ImgWorkerCv;
static std::thread g_ImgWorker;
static std::atomic<bool> g_ImgWorkerRunning(false);
static bool g_ImgWorkerStop = false;
static bool g_ImgRequestPending = false;
static std::string g_ImgRequestedPath;
static DecodedWatermarkImage g_ImgResult;
static bool g_ImgResultReady = false;
static std::string g_ImgDesiredPath;
static DWORD g_ImgDesiredSince = 0;
static std::vector<DecodedWatermarkFrame> g_ImgFrames;
static size_t g_ImgFrameIndex = 0;
static DWORD g_ImgFrameStarted = 0;

static float g_DvdX = 40.f, g_DvdY = 40.f;
static float g_DvdVx = 120.f, g_DvdVy = 90.f;
static DWORD g_DvdLast = 0;

void initGdiplus() {
    if (g_GdipToken) return;
    Gdiplus::GdiplusStartupInput in;
    if (Gdiplus::GdiplusStartup(&g_GdipToken, &in, nullptr) != Gdiplus::Ok)
        g_GdipToken = 0;
}

void shutdownGdiplus() {
    // Join the worker before unloading code it may still execute
    {
        std::lock_guard<std::mutex> guard(g_ImgWorkerMutex);
        g_ImgWorkerStop = true;
        g_ImgRequestPending = false;
        g_ImgRequestedPath.clear();
    }
    g_ImgWorkerCv.notify_all();
    if (g_ImgWorker.joinable()) g_ImgWorker.join();
    {
        std::lock_guard<std::mutex> guard(g_ImgWorkerMutex);
        g_ImgWorkerStop = false;
        g_ImgRequestPending = false;
        g_ImgRequestedPath.clear();
    }
    g_ImgWorkerRunning.store(false);
    if (!g_GdipToken) return;
    Gdiplus::GdiplusShutdown(g_GdipToken);
    g_GdipToken = 0;
}

static void clearWatermarkImage();

void invalidateImageCache() {

    clearWatermarkImage();
    g_ImgLastAttempt = 0;
    g_ImgLoadFailed = false;
    g_ImgDesiredPath.clear();
    g_ImgDesiredSince = GetTickCount();
    {
        std::lock_guard<std::mutex> resultGuard(g_ImgResultMutex);
        g_ImgResult = DecodedWatermarkImage{};
        g_ImgResultReady = false;
    }

}

void setColor(int r, int g, int b, int a) {
    VCall<void, int, int, int, int>(g_Surface, 11, r, g, b, a);
}
void fill(int x0, int y0, int x1, int y1, int r, int g, int b, int a) {
    setColor(r, g, b, a);
    VCall<void, int, int, int, int>(g_Surface, 12, x0, y0, x1, y1);
}
void rect(int x0, int y0, int x1, int y1) {
    VCall<void, int, int, int, int>(g_Surface, 14, x0, y0, x1, y1);
}
void line(int x0, int y0, int x1, int y1) {
    VCall<void, int, int, int, int>(g_Surface, 15, x0, y0, x1, y1);
}

void textFont(unsigned long font, int x, int y, int r, int g, int b, const wchar_t* s) {
    if (!s || !s[0] || !font || !g_Surface) return;
    VCall<void, unsigned long>(g_Surface, 17, font);
    VCall<void, int, int, int, int>(g_Surface, 19, r, g, b, 255);
    VCall<void, int, int>(g_Surface, 20, x, y);
    VCall<void, const wchar_t*, int, int>(g_Surface, 22, s, (int)wcslen(s), 0);
}

void text(int x, int y, int r, int g, int b, const wchar_t* s) {
    textFont(g_Font, x, y, r, g, b, s);
}

static bool isNamedFontAlive(unsigned long font) {
    if (!g_Surface || !font) return false;
    __try {
        return VCall<int, unsigned long>(g_Surface, 69, font) > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void refreshNamedFonts() {

    if (g_SpeedFont && !isNamedFontAlive(g_SpeedFont)) g_SpeedFont = 0;
    g_SpeedFontFace.clear();
    g_SpeedFontHeight = 0;
}

unsigned long getOrCreateNamedFont(const std::string& requestedFace, int pixelHeight) {
    if (!g_Surface) return g_Font;
    std::string face = requestedFace.empty() ? std::string("Tahoma") : requestedFace;
    if (face.size() > 63) face.resize(63);
    if (pixelHeight < 10) pixelHeight = 10;
    if (pixelHeight > 36) pixelHeight = 36;

    if (g_SpeedFont && !isNamedFontAlive(g_SpeedFont)) {
        g_SpeedFont = 0;
        g_SpeedFontFace.clear();
        g_SpeedFontHeight = 0;
    }
    if (!g_SpeedFont) g_SpeedFont = VCall<unsigned long>(g_Surface, 66);
    if (!g_SpeedFont) return g_Font;

    if (g_SpeedFontFace != face || g_SpeedFontHeight != pixelHeight) {
        bool ok = VCall<bool, unsigned long, const char*, int, int, int, int, int, int, int>(
            g_Surface, 67, g_SpeedFont, face.c_str(), pixelHeight, 500, 0, 0, 0x200, 0, 0);
        if (!ok && face != "Tahoma") {
            face = "Tahoma";
            ok = VCall<bool, unsigned long, const char*, int, int, int, int, int, int, int>(
                g_Surface, 67, g_SpeedFont, face.c_str(), pixelHeight, 500, 0, 0, 0x200, 0, 0);
        }
        if (ok) {
            g_SpeedFontFace = face;
            g_SpeedFontHeight = pixelHeight;
        }
    }
    return isNamedFontAlive(g_SpeedFont) ? g_SpeedFont : g_Font;
}

void measureText(unsigned long font, const wchar_t* text, int& width, int& height) {
    width = 0; height = 0;
    if (!g_Surface || !font || !text) return;
    VCall<void, unsigned long, const wchar_t*, int*, int*>(
        g_Surface, 75, font, text, &width, &height);
}

void toWide(const std::string& s, wchar_t* o, int n) {
    if (!o || n <= 0) return;
    o[0] = L'\0';
    if (s.empty()) return;

    UINT codePage = CP_ACP;
    int required = MultiByteToWideChar(codePage, 0, s.c_str(), -1, nullptr, 0);
    if (required <= 0) {
        codePage = CP_UTF8;
        required = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
                                       s.c_str(), -1, nullptr, 0);
    }
    if (required <= 0) return;
    std::vector<wchar_t> converted(static_cast<size_t>(required), L'\0');
    if (!MultiByteToWideChar(codePage, codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
                             s.c_str(), -1, converted.data(), required)) return;
    const size_t charsAvailable = converted.empty() ? 0 : converted.size() - 1;
    const size_t charsToCopy = (std::min)(static_cast<size_t>(n - 1), charsAvailable);
    if (charsToCopy) std::copy_n(converted.begin(), charsToCopy, o);
    o[charsToCopy] = L'\0';
}

void ensureUiFont() {

    if (!g_Surface) return;
    static std::string s_face;
    std::string face = g_UiFont.empty() ? "Tahoma" : g_UiFont;
    while (!face.empty() && (unsigned char)face.back() <= 32) face.pop_back();
    if (face.empty()) face = "Tahoma";

    if (!g_Font) {
        g_Font = VCall<unsigned long>(g_Surface, 66);
        if (!g_Font) return;
        s_face.clear();
    }
    if (s_face == face) return;

    bool ok = VCall<bool, unsigned long, const char*, int, int, int, int, int, int, int>(
        g_Surface, 67, g_Font, face.c_str(), 13, 500, 0, 0, 0x200, 0, 0);
    if (!ok && face != "Tahoma") {
        ok = VCall<bool, unsigned long, const char*, int, int, int, int, int, int, int>(
            g_Surface, 67, g_Font, "Tahoma", 13, 500, 0, 0, 0x200, 0, 0);
        face = "Tahoma";
        g_UiFont = "Tahoma";
    }
    if (ok) s_face = face;
}

void refreshWatermarkFont() {

    g_WmFontPx = -1;
    g_WmFontLastCheck = 0;
    g_WmFontLastConfigured = 0;
    g_WmFontLastCreateAttempt = 0;
}

struct Matrix4 { float m[4][4]; };
bool W2S(const Vec3& p, int sw, int sh, int& ox, int& oy) {
    if (!g_Engine || sw < 2 || sh < 2) return false;

    static void* cachedEngine = nullptr;
    static const Matrix4* cachedMatrix = nullptr;
    static DWORD lastRefresh = 0;
    const DWORD now = GetTickCount();
    if (cachedEngine != g_Engine || !cachedMatrix || now - lastRefresh >= 250u) {
        const Matrix4* candidate = nullptr;
        __try { candidate = VCall<const Matrix4*>(g_Engine, 36); }
        __except (EXCEPTION_EXECUTE_HANDLER) { candidate = nullptr; }
        if (!IsReadableProcessRange(candidate, sizeof(Matrix4))) {
            cachedEngine = nullptr;
            cachedMatrix = nullptr;
            return false;
        }
        cachedEngine = g_Engine;
        cachedMatrix = candidate;
        lastRefresh = now;
    }
    const Matrix4* matrix = cachedMatrix;
    __try {
        const Matrix4& M = *matrix;
        const float w = M.m[3][0] * p.x + M.m[3][1] * p.y + M.m[3][2] * p.z + M.m[3][3];
        if (!std::isfinite(w) || w < 0.001f) return false;
        const float inv = 1.f / w;
        const float x = (M.m[0][0] * p.x + M.m[0][1] * p.y + M.m[0][2] * p.z + M.m[0][3]) * inv;
        const float y = (M.m[1][0] * p.x + M.m[1][1] * p.y + M.m[1][2] * p.z + M.m[1][3]) * inv;
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        ox = static_cast<int>(sw * 0.5f + x * sw * 0.5f);
        oy = static_cast<int>(sh * 0.5f - y * sh * 0.5f);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cachedMatrix = nullptr;
        cachedEngine = nullptr;
        return false;
    }
}

static uint16_t readLe16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
static uint32_t readLe32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static bool vtfByteCount(int format, int width, int height, size_t& bytes) {
    if (width < 1 || height < 1 || width > 2048 || height > 2048) return false;
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t blocks = static_cast<size_t>((width + 3) / 4) * static_cast<size_t>((height + 3) / 4);
    switch (format) {
    case 0: case 1: case 11: case 12: case 16: bytes = pixels * 4; return true;  
    case 2: case 3: case 9: case 10: bytes = pixels * 3; return true;  
    case 4: case 17: case 18: case 19: case 21: bytes = pixels * 2; return true;  
    case 5: case 8: bytes = pixels; return true;  
    case 6: bytes = pixels * 2; return true;  
    case 13: case 20: bytes = blocks * 8; return true;  
    case 14: case 15: bytes = blocks * 16; return true;  
    default: bytes = 0; return false;
    }
}

static void decodeRgb565(uint16_t value, unsigned char& r, unsigned char& g, unsigned char& b) {
    r = static_cast<unsigned char>(((value >> 11) & 31) * 255 / 31);
    g = static_cast<unsigned char>(((value >> 5) & 63) * 255 / 63);
    b = static_cast<unsigned char>((value & 31) * 255 / 31);
}

static void decodeDxtColors(const unsigned char* block, unsigned char out[16][4], bool forceFour, bool alphaMode) {
    const uint16_t c0 = readLe16(block), c1 = readLe16(block + 2);
    const uint32_t indexes = readLe32(block + 4);
    unsigned char colors[4][4]{};
    decodeRgb565(c0, colors[0][0], colors[0][1], colors[0][2]);
    decodeRgb565(c1, colors[1][0], colors[1][1], colors[1][2]);
    colors[0][3] = colors[1][3] = 255;
    if (forceFour || c0 > c1) {
        for (int c = 0; c < 3; ++c) {
            colors[2][c] = static_cast<unsigned char>((2 * int(colors[0][c]) + int(colors[1][c])) / 3);
            colors[3][c] = static_cast<unsigned char>((int(colors[0][c]) + 2 * int(colors[1][c])) / 3);
        }
        colors[2][3] = colors[3][3] = 255;
    } else {
        for (int c = 0; c < 3; ++c)
            colors[2][c] = static_cast<unsigned char>((int(colors[0][c]) + int(colors[1][c])) / 2);
        colors[2][3] = 255;
        colors[3][0] = colors[3][1] = colors[3][2] = 0;
        colors[3][3] = alphaMode ? 0 : 255;
    }
    for (int i = 0; i < 16; ++i) {
        const unsigned int which = (indexes >> (i * 2)) & 3;
        memcpy(out[i], colors[which], 4);
    }
}

static void writeDxtBlock(std::vector<unsigned char>& rgba, int width, int height,
                          int blockX, int blockY, const unsigned char pixels[16][4]) {
    for (int i = 0; i < 16; ++i) {
        const int x = blockX * 4 + (i & 3), y = blockY * 4 + (i >> 2);
        if (x < 0 || y < 0 || x >= width || y >= height) continue;
        memcpy(rgba.data() + (static_cast<size_t>(y) * width + x) * 4, pixels[i], 4);
    }
}

static bool decodeVtfBlocks(const unsigned char* data, size_t size, int format,
                            int width, int height, std::vector<unsigned char>& rgba) {
    size_t needed = 0;
    if (!vtfByteCount(format, width, height, needed) || needed > size) return false;
    rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    const int blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            const size_t offset = (static_cast<size_t>(by) * blocksX + bx) * ((format == 13 || format == 20) ? 8 : 16);
            const unsigned char* block = data + offset;
            unsigned char pixels[16][4]{};
            if (format == 13 || format == 20) {
                decodeDxtColors(block, pixels, false, format == 20);
            } else {
                decodeDxtColors(block + 8, pixels, true, false);
                if (format == 14) {
                    for (int i = 0; i < 16; ++i) {
                        const unsigned char nibble = (i & 1) ? (block[i >> 1] >> 4) : (block[i >> 1] & 0x0F);
                        pixels[i][3] = static_cast<unsigned char>(nibble * 17);
                    }
                } else if (format == 15) {
                    unsigned char alpha[8]{};
                    alpha[0] = block[0]; alpha[1] = block[1];
                    if (alpha[0] > alpha[1]) {
                        for (int i = 1; i <= 6; ++i)
                            alpha[i + 1] = static_cast<unsigned char>(((7 - i) * int(alpha[0]) + i * int(alpha[1])) / 7);
                    } else {
                        for (int i = 1; i <= 4; ++i)
                            alpha[i + 1] = static_cast<unsigned char>(((5 - i) * int(alpha[0]) + i * int(alpha[1])) / 5);
                        alpha[6] = 0; alpha[7] = 255;
                    }
                    uint64_t bits = 0;
                    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
                    for (int i = 0; i < 16; ++i) pixels[i][3] = alpha[(bits >> (i * 3)) & 7];
                }
            }
            writeDxtBlock(rgba, width, height, bx, by, pixels);
        }
    }
    return true;
}

static bool decodeVtfPixels(const unsigned char* bytes, size_t size,
                            std::vector<unsigned char>& rgba, int& width, int& height) {
    if (!bytes || size < 64 || memcmp(bytes, "VTF\0", 4) != 0) return false;
    const uint32_t major = readLe32(bytes + 4), minor = readLe32(bytes + 8);
    if (major != 7 || minor > 5) return false;
    const uint32_t headerSize = readLe32(bytes + 12);
    width = readLe16(bytes + 16); height = readLe16(bytes + 18);
    const uint32_t flags = readLe32(bytes + 20);
    const int frames = (std::max)(1, static_cast<int>(readLe16(bytes + 24)));
    const int format = static_cast<int>(readLe32(bytes + 52));
    const int mipCount = (std::max)(1, static_cast<int>(bytes[56]));
    const int lowFormat = static_cast<int>(readLe32(bytes + 57));
    const int lowWidth = bytes[61], lowHeight = bytes[62];
    const int depth = (minor >= 2 && size >= 65) ? (std::max)(1, static_cast<int>(readLe16(bytes + 63))) : 1;

    if (width < 1 || height < 1 || width > 2048 || height > 2048 || (flags & 0x00004000u) ||
        headerSize < 64 || headerSize > size || frames > 512 || mipCount > 16 || depth != 1) return false;

    size_t imageOffset = 0;
    if (minor >= 3) {
        if (size < 80 || headerSize < 80) return false;
        const uint32_t resourceCount = readLe32(bytes + 68);
        const uint64_t resourceTableEnd = 80ull + static_cast<uint64_t>(resourceCount) * 8ull;
        if (resourceCount > 32 || resourceTableEnd > size) return false;
        bool foundImage = false;
        for (uint32_t i = 0; i < resourceCount; ++i) {
            const size_t entry = 80 + static_cast<size_t>(i) * 8;
            if (bytes[entry] == 0x30 && bytes[entry + 1] == 0 && bytes[entry + 2] == 0 &&
                (bytes[entry + 3] & 0x02) == 0) {
                imageOffset = readLe32(bytes + entry + 4);
                foundImage = true;
                break;
            }
        }
        if (!foundImage) return false;
    } else {
        imageOffset = headerSize;
        if (lowWidth > 0 && lowHeight > 0) {
            size_t lowBytes = 0;
            if (!vtfByteCount(lowFormat, lowWidth, lowHeight, lowBytes)) return false;
            if (imageOffset > size || lowBytes > size - imageOffset) return false;
            imageOffset += lowBytes;
        }
    }

    for (int mip = mipCount - 1; mip >= 1; --mip) {
        size_t mipBytes = 0;
        if (!vtfByteCount(format, (std::max)(width >> mip, 1), (std::max)(height >> mip, 1), mipBytes)) return false;
        const uint64_t skip = static_cast<uint64_t>(mipBytes) * static_cast<uint64_t>(frames);
        if (imageOffset > size || skip > size - imageOffset) return false;
        imageOffset += static_cast<size_t>(skip);
    }
    size_t imageBytes = 0;
    if (!vtfByteCount(format, width, height, imageBytes) || imageOffset > size || imageBytes > size - imageOffset) return false;
    const unsigned char* src = bytes + imageOffset;
    if (format == 13 || format == 14 || format == 15 || format == 20)
        return decodeVtfBlocks(src, imageBytes, format, width, height, rgba);

    rgba.resize(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0, pixels = static_cast<size_t>(width) * height; i < pixels; ++i) {
        unsigned char* dst = rgba.data() + i * 4;
        const unsigned char* in = nullptr;
        switch (format) {
        case 0: memcpy(dst, src + i * 4, 4); break;  
        case 1: dst[0]=src[i*4+3]; dst[1]=src[i*4+2]; dst[2]=src[i*4+1]; dst[3]=src[i*4]; break;  
        case 11: dst[0]=src[i*4+1]; dst[1]=src[i*4+2]; dst[2]=src[i*4+3]; dst[3]=src[i*4]; break;  
        case 12: case 16: dst[0]=src[i*4+2]; dst[1]=src[i*4+1]; dst[2]=src[i*4]; dst[3]=(format==16)?255:src[i*4+3]; break;  
        case 2: case 9: dst[0]=src[i*3]; dst[1]=src[i*3+1]; dst[2]=src[i*3+2]; dst[3]=(format==9 && dst[0]==0 && dst[1]==0 && dst[2]==255)?0:255; break;
        case 3: case 10: dst[0]=src[i*3+2]; dst[1]=src[i*3+1]; dst[2]=src[i*3]; dst[3]=(format==10 && dst[0]==0 && dst[1]==0 && dst[2]==255)?0:255; break;
        case 5: dst[0]=dst[1]=dst[2]=src[i]; dst[3]=255; break;  
        case 8: dst[0]=dst[1]=dst[2]=255; dst[3]=src[i]; break;  
        case 6: dst[0]=dst[1]=dst[2]=src[i*2]; dst[3]=src[i*2+1]; break;  
        case 4: case 17: {
            const uint16_t value = readLe16(src + i * 2);
            unsigned char r=0,g=0,b=0; decodeRgb565(value,r,g,b);
            if (format == 4) { dst[0]=r; dst[1]=g; dst[2]=b; }
            else { dst[0]=b; dst[1]=g; dst[2]=r; }
            dst[3]=255; break;
        }
        case 18: case 21: {  
            const uint16_t value = readLe16(src + i * 2);
            const unsigned int b5 = value & 31u;
            const unsigned int g5 = (value >> 5) & 31u;
            const unsigned int r5 = (value >> 10) & 31u;
            dst[0] = static_cast<unsigned char>((r5 * 255u) / 31u);
            dst[1] = static_cast<unsigned char>((g5 * 255u) / 31u);
            dst[2] = static_cast<unsigned char>((b5 * 255u) / 31u);
            dst[3] = (format == 21 && (value & 0x8000u) == 0) ? 0 : 255;
            break;
        }
        case 19: {  
            const uint16_t value = readLe16(src + i * 2);
            dst[0] = static_cast<unsigned char>(((value >> 8) & 0x0Fu) * 17u);
            dst[1] = static_cast<unsigned char>(((value >> 4) & 0x0Fu) * 17u);
            dst[2] = static_cast<unsigned char>((value & 0x0Fu) * 17u);
            dst[3] = static_cast<unsigned char>(((value >> 12) & 0x0Fu) * 17u);
            break;
        }
        default: return false;
        }
        (void)in;
    }
    return true;
}

static bool readVtfFile(const std::string& path, std::vector<unsigned char>& bytes) {
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || !file) return false;
    if (_fseeki64(file, 0, SEEK_END) != 0) { fclose(file); return false; }
    const __int64 length = _ftelli64(file);
    if (length < 64 || length > 32ll * 1024ll * 1024ll || _fseeki64(file, 0, SEEK_SET) != 0) {
        fclose(file); return false;
    }
    bytes.resize(static_cast<size_t>(length));
    const size_t read = fread(bytes.data(), 1, bytes.size(), file);
    fclose(file);
    return read == bytes.size();
}

static bool isExecutableAddress(const void* address) {
    if (!address) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const DWORD p = mbi.Protect & 0xFF;
    return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

static bool isSurfaceSlotCallable(int slot) {
    if (!g_Surface || slot < 0 || slot > 96) return false;
    __try {
        void** table = *reinterpret_cast<void***>(g_Surface);
        if (!table || !isExecutableAddress(table[slot])) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

enum WatermarkSurfaceSlot {
    SURFACE_DRAW_SET_COLOR = 11,
    SURFACE_DRAW_SET_TEXTURE_RGBA = 31,
    SURFACE_DRAW_SET_TEXTURE = 32,
    SURFACE_DRAW_TEXTURED_RECT = 34,
    SURFACE_IS_TEXTURE_ID_VALID = 35,
    SURFACE_DELETE_TEXTURE_BY_ID = 36,
    SURFACE_CREATE_NEW_TEXTURE_ID = 37
};

static bool safeCreateWatermarkTexture(int& textureId) {
    if (!isSurfaceSlotCallable(SURFACE_CREATE_NEW_TEXTURE_ID) ||
        !isSurfaceSlotCallable(SURFACE_IS_TEXTURE_ID_VALID)) return false;
    __try {
        if (textureId >= 0 && VCall<bool, int>(g_Surface, SURFACE_IS_TEXTURE_ID_VALID, textureId)) return true;
        textureId = VCall<int, bool>(g_Surface, SURFACE_CREATE_NEW_TEXTURE_ID, true);
        return textureId >= 0 && VCall<bool, int>(g_Surface, SURFACE_IS_TEXTURE_ID_VALID, textureId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        textureId = -1;
        return false;
    }
}

static void safeDestroyWatermarkTexture(int textureId) {
    if (textureId < 0 || !isSurfaceSlotCallable(SURFACE_DELETE_TEXTURE_BY_ID) ||
        !isSurfaceSlotCallable(SURFACE_IS_TEXTURE_ID_VALID)) return;
    __try {
        if (VCall<bool, int>(g_Surface, SURFACE_IS_TEXTURE_ID_VALID, textureId))
            (void)VCall<bool, int>(g_Surface, SURFACE_DELETE_TEXTURE_BY_ID, textureId);
    } __except (EXCEPTION_EXECUTE_HANDLER) {

    }
}

static void clearWatermarkImage() {
    const int oldTexture = g_ImgTex;
    g_ImgTex = -1;
    if (oldTexture >= 0) safeDestroyWatermarkTexture(oldTexture);
    g_ImgW = g_ImgH = 0;
    g_ImgContentW = g_ImgContentH = 0;
    g_ImgTexContentW = g_ImgTexContentH = 0;
    g_ImgLoadedPath.clear();
    g_ImgFrames.clear();
    g_ImgFrameIndex = 0;
    g_ImgFrameStarted = 0;
}

static bool safeUploadWatermarkTexture(int textureId, const unsigned char* rgba, int width, int height) {
    if (textureId < 0 || !rgba || width < 1 || height < 1 || width > 2048 || height > 2048 ||
        !isSurfaceSlotCallable(SURFACE_IS_TEXTURE_ID_VALID)) return false;
    __try {
        if (!VCall<bool, int>(g_Surface, SURFACE_IS_TEXTURE_ID_VALID, textureId)) return false;

        __try {
            VCall<void, int, const unsigned char*, int, int, int>(
                g_Surface, 119, textureId, rgba, width, height, 0);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        VCall<void, int, const unsigned char*, int, int, int, bool>(
            g_Surface, SURFACE_DRAW_SET_TEXTURE_RGBA, textureId, rgba, width, height, 0, true);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool safeDrawWatermarkTexture(int textureId, int x, int y, int width, int height, int alpha) {
    if (textureId < 0 || width < 1 || height < 1 ||
        !isSurfaceSlotCallable(SURFACE_DRAW_SET_COLOR) ||
        !isSurfaceSlotCallable(SURFACE_DRAW_SET_TEXTURE) ||
        !isSurfaceSlotCallable(SURFACE_DRAW_TEXTURED_RECT) ||
        !isSurfaceSlotCallable(SURFACE_IS_TEXTURE_ID_VALID)) return false;
    __try {
        if (!VCall<bool, int>(g_Surface, SURFACE_IS_TEXTURE_ID_VALID, textureId)) return false;
        VCall<void, int, int, int, int>(g_Surface, SURFACE_DRAW_SET_COLOR, 255, 255, 255, alpha);
        VCall<void, int>(g_Surface, SURFACE_DRAW_SET_TEXTURE, textureId);

        VCall<void, int, int, int, int>(g_Surface, SURFACE_DRAW_TEXTURED_RECT, x, y, x + width, y + height);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static DWORD getWatermarkFrameDelayMs(Gdiplus::Bitmap* bmp, UINT frameIndex) {

    const PROPID kFrameDelayTag = 0x5100;
    if (!bmp) return 100;

    const UINT propSize = bmp->GetPropertyItemSize(kFrameDelayTag);
    if (propSize < sizeof(Gdiplus::PropertyItem)) return 100;
    std::vector<unsigned char> propBuffer(propSize, 0);
    Gdiplus::PropertyItem* item = reinterpret_cast<Gdiplus::PropertyItem*>(propBuffer.data());
    if (bmp->GetPropertyItem(kFrameDelayTag, propSize, item) != Gdiplus::Ok ||
        !item->value || item->length < sizeof(ULONG)) return 100;

    const size_t delayCount = item->length / sizeof(ULONG);
    if (frameIndex >= delayCount) return 100;
    const ULONG* delays = reinterpret_cast<const ULONG*>(item->value);
    ULONG centiseconds = delays[frameIndex];
    if (centiseconds == 0) return 100;

    uint64_t delayMs = static_cast<uint64_t>(centiseconds) * 10ull;
    if (delayMs < 20ull) delayMs = 20ull;
    if (delayMs > 10000ull) delayMs = 10000ull;
    return static_cast<DWORD>(delayMs);
}

static UINT getWatermarkFrameCount(Gdiplus::Bitmap* bmp, GUID& timeDimension) {

    const GUID kFrameDimensionTime = {
        0x6aedbd6d, 0x3fb5, 0x418a, { 0x83, 0xa6, 0x7f, 0x45, 0x22, 0x9d, 0xc8, 0x72 }
    };
    timeDimension = kFrameDimensionTime;
    if (!bmp) return 1;

    const UINT dimensionCount = bmp->GetFrameDimensionsCount();
    if (dimensionCount == 0 || dimensionCount > 16) return 1;
    std::vector<GUID> dimensions(dimensionCount);
    if (bmp->GetFrameDimensionsList(dimensions.data(), dimensionCount) != Gdiplus::Ok)
        return 1;

    for (UINT i = 0; i < dimensionCount; ++i) {
        if (!IsEqualGUID(dimensions[i], kFrameDimensionTime)) continue;
        timeDimension = dimensions[i];
        const UINT count = bmp->GetFrameCount(&timeDimension);
        return count > 0 ? count : 1;
    }
    return 1;
}

static bool convertWatermarkBitmapFrameToRgba(Gdiplus::Bitmap* bmp, int decodedWidth,
                                               int decodedHeight, std::vector<unsigned char>& rgba) {
    if (!bmp || decodedWidth < 1 || decodedHeight < 1 || decodedWidth > 2048 || decodedHeight > 2048)
        return false;

    Gdiplus::Bitmap scaled(decodedWidth, decodedHeight, PixelFormat32bppARGB);
    if (scaled.GetLastStatus() != Gdiplus::Ok) return false;
    {
        Gdiplus::Graphics graphics(&scaled);
        if (graphics.GetLastStatus() != Gdiplus::Ok) return false;
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        if (graphics.DrawImage(bmp, 0, 0, decodedWidth, decodedHeight) != Gdiplus::Ok)
            return false;
    }

    rgba.assign(static_cast<size_t>(decodedWidth) * decodedHeight * 4, 0);
    Gdiplus::BitmapData bd{};
    Gdiplus::rect rc(0, 0, decodedWidth, decodedHeight);
    if (scaled.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok)
        return false;

    const unsigned char* base = reinterpret_cast<const unsigned char*>(bd.Scan0);
    const int stride = bd.Stride;
    for (int y = 0; y < decodedHeight; ++y) {
        const unsigned char* row = stride >= 0
            ? base + static_cast<std::ptrdiff_t>(y) * stride
            : base + static_cast<std::ptrdiff_t>(decodedHeight - 1 - y) * (-stride);
        for (int x = 0; x < decodedWidth; ++x) {
            const unsigned char* src = row + static_cast<size_t>(x) * 4;
            unsigned char* dst = &rgba[(static_cast<size_t>(y) * decodedWidth + x) * 4];

            dst[0] = src[2];  
            dst[1] = src[1];  
            dst[2] = src[0];  
            dst[3] = src[3];  
        }
    }
    scaled.UnlockBits(&bd);
    return true;
}

static bool decodeWatermarkImage(const std::string& sourcePath, DecodedWatermarkImage& result) {
    result.path = sourcePath;
    if (sourcePath.empty()) return false;

    std::string path = sourcePath;
    if (path.find(':') == std::string::npos && path[0] != '\\' && path[0] != '/')
        path = cfgDir() + "\\" + path;

    Gdiplus::Bitmap* bmp = nullptr;

    std::vector<unsigned char> vtfBgra;
    std::string extension = path;
    const size_t dot = extension.find_last_of('.');
    extension = dot == std::string::npos ? std::string() : extension.substr(dot);
    for (char& c : extension) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (extension == ".vtf") {
        std::vector<unsigned char> fileBytes, vtfRgba;
        int vtfWidth = 0, vtfHeight = 0;
        if (!readVtfFile(path, fileBytes) ||
            !decodeVtfPixels(fileBytes.data(), fileBytes.size(), vtfRgba, vtfWidth, vtfHeight))
            return false;
        vtfBgra.resize(vtfRgba.size());
        for (size_t i = 0; i < vtfRgba.size(); i += 4) {
            vtfBgra[i] = vtfRgba[i + 2];
            vtfBgra[i + 1] = vtfRgba[i + 1];
            vtfBgra[i + 2] = vtfRgba[i];
            vtfBgra[i + 3] = vtfRgba[i + 3];
        }
        bmp = new Gdiplus::Bitmap(vtfWidth, vtfHeight, vtfWidth * 4, PixelFormat32bppARGB, vtfBgra.data());
        if (bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return false; }
    } else {
        UINT pathCodePage = CP_ACP;
        int required = MultiByteToWideChar(pathCodePage, 0, path.c_str(), -1, nullptr, 0);
        if (required <= 0) {
            pathCodePage = CP_UTF8;
            required = MultiByteToWideChar(pathCodePage, MB_ERR_INVALID_CHARS,
                                           path.c_str(), -1, nullptr, 0);
        }
        if (required <= 0 || required > 32768) return false;
        std::vector<wchar_t> widePath(static_cast<size_t>(required), L'\0');
        if (!MultiByteToWideChar(pathCodePage, pathCodePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
                                 path.c_str(), -1, widePath.data(), required)) return false;
        bmp = new Gdiplus::Bitmap(widePath.data());
        if (bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return false; }
    }

    const int originalWidth = static_cast<int>(bmp->GetWidth());
    const int originalHeight = static_cast<int>(bmp->GetHeight());
    if (originalWidth <= 0 || originalHeight <= 0 ||
        static_cast<uint64_t>(originalWidth) * static_cast<uint64_t>(originalHeight) > 8ull * 1024ull * 1024ull) {
        delete bmp;
        return false;
    }

    const double sourceFit = (std::min)(1.0, (std::min)(2048.0 / originalWidth, 2048.0 / originalHeight));
    const int decodedWidth = (std::max)(1, static_cast<int>(originalWidth * sourceFit + 0.5));
    const int decodedHeight = (std::max)(1, static_cast<int>(originalHeight * sourceFit + 0.5));
    const size_t bytesPerFrame = static_cast<size_t>(decodedWidth) * decodedHeight * 4;
    const size_t maxDecodedBytes = 64ull * 1024ull * 1024ull;
    const UINT maxDecodedFrames = static_cast<UINT>((std::max)(1ull,
        static_cast<unsigned long long>(maxDecodedBytes / (bytesPerFrame ? bytesPerFrame : 1))));
    const UINT frameLimit = (std::min)(64u, maxDecodedFrames);

    GUID timeDimension{};
    const UINT availableFrames = getWatermarkFrameCount(bmp, timeDimension);
    const UINT framesToDecode = (std::min)(availableFrames, frameLimit);
    size_t totalDecodedBytes = 0;
    result.frames.clear();
    result.frames.reserve(framesToDecode);

    for (UINT frameIndex = 0; frameIndex < framesToDecode; ++frameIndex) {
        if (availableFrames > 1 && bmp->SelectActiveFrame(&timeDimension, frameIndex) != Gdiplus::Ok) {
            delete bmp;
            return false;
        }

        DecodedWatermarkFrame frame{};
        frame.delayMs = availableFrames > 1 ? getWatermarkFrameDelayMs(bmp, frameIndex) : 100;
        if (!convertWatermarkBitmapFrameToRgba(bmp, decodedWidth, decodedHeight, frame.rgba)) {
            delete bmp;
            return false;
        }
        if (frame.rgba.empty() || frame.rgba.size() != bytesPerFrame ||
            totalDecodedBytes > maxDecodedBytes - frame.rgba.size()) {
            if (result.frames.empty()) {
                delete bmp;
                return false;
            }
            break;
        }
        totalDecodedBytes += frame.rgba.size();
        result.frames.push_back(std::move(frame));
    }
    delete bmp;

    if (result.frames.empty()) return false;
    result.textureWidth = decodedWidth;
    result.textureHeight = decodedHeight;

    result.contentWidth = originalWidth;
    result.contentHeight = originalHeight;
    result.success = true;
    return true;
}

static bool decodeWatermarkImageGuarded(const std::string& path, DecodedWatermarkImage& result) {
    __try {
        return decodeWatermarkImage(path, result);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result.path = path;
        result.success = false;
        return false;
    }
}

static void watermarkDecodeWorker() {
    for (;;) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock(g_ImgWorkerMutex);
            g_ImgWorkerCv.wait(lock, [] { return g_ImgWorkerStop || g_ImgRequestPending; });
            if (g_ImgWorkerStop) break;
            path = std::move(g_ImgRequestedPath);
            g_ImgRequestedPath.clear();
            g_ImgRequestPending = false;
        }

        DecodedWatermarkImage decoded{};
        try {
            decoded.success = decodeWatermarkImageGuarded(path, decoded);
        } catch (...) {
            decoded.success = false;
            decoded.path = path;
        }
        {
            std::lock_guard<std::mutex> resultGuard(g_ImgResultMutex);
            g_ImgResult = std::move(decoded);
            g_ImgResultReady = true;
        }
        g_ImgWorkerRunning.store(false);
    }
    g_ImgWorkerRunning.store(false);
}

static void startWatermarkDecode(const std::string& path) {
    std::lock_guard<std::mutex> lifeGuard(g_ImgWorkerMutex);
    if (g_ImgWorkerStop) return;
    if (!g_ImgWorker.joinable()) {
        g_ImgWorker = std::thread(watermarkDecodeWorker);
    }

    g_ImgRequestedPath = path;
    g_ImgRequestPending = true;
    g_ImgWorkerRunning.store(true);
    g_ImgWorkerCv.notify_one();
}

static bool uploadWatermarkImage(DecodedWatermarkImage& decoded) {
    if (!g_Surface || !decoded.success || decoded.textureWidth <= 0 ||
        decoded.textureHeight <= 0 || decoded.textureWidth > 2048 || decoded.textureHeight > 2048 ||
        decoded.frames.empty()) return false;

    const int srcW = decoded.textureWidth;
    const int srcH = decoded.textureHeight;
    const size_t srcBytes = static_cast<size_t>(srcW) * srcH * 4;
    for (const auto& frame : decoded.frames) {
        if (frame.rgba.size() != srcBytes) return false;
    }

    if (g_ImgTex >= 0) {
        safeDestroyWatermarkTexture(g_ImgTex);
        g_ImgTex = -1;
    }
    if (!safeCreateWatermarkTexture(g_ImgTex)) {
        g_ImgTex = -1;
        return false;
    }
    if (!safeUploadWatermarkTexture(g_ImgTex, decoded.frames[0].rgba.data(), srcW, srcH)) {
        safeDestroyWatermarkTexture(g_ImgTex);
        g_ImgTex = -1;
        return false;
    }

    g_ImgW = srcW;
    g_ImgH = srcH;
    g_ImgTexContentW = srcW;
    g_ImgTexContentH = srcH;
    g_ImgContentW = srcW;
    g_ImgContentH = srcH;
    g_ImgLoadedPath = decoded.path;
    g_ImgFrames = std::move(decoded.frames);
    g_ImgFrameIndex = 0;
    g_ImgFrameStarted = GetTickCount();
    return true;
}

static bool advanceWatermarkAnimation(DWORD now) {
    if (g_ImgFrames.size() < 2 || g_ImgTex < 0 || g_ImgFrameIndex >= g_ImgFrames.size())
        return true;

    const size_t previousIndex = g_ImgFrameIndex;
    size_t advanced = 0;
    while (advanced < g_ImgFrames.size()) {
        DWORD delay = g_ImgFrames[g_ImgFrameIndex].delayMs;
        if (delay < 20) delay = 20;
        if (static_cast<DWORD>(now - g_ImgFrameStarted) < delay) break;
        g_ImgFrameStarted += delay;
        g_ImgFrameIndex = (g_ImgFrameIndex + 1) % g_ImgFrames.size();
        ++advanced;
    }

    if (g_ImgFrameIndex == previousIndex) return true;
    const std::vector<unsigned char>& rgba = g_ImgFrames[g_ImgFrameIndex].rgba;
    if (rgba.size() != static_cast<size_t>(g_ImgW) * g_ImgH * 4 ||
        !safeUploadWatermarkTexture(g_ImgTex, rgba.data(), g_ImgW, g_ImgH)) {
        clearWatermarkImage();
        g_ImgLoadFailed = true;
        g_ImgLastAttempt = now;
        return false;
    }
    return true;
}

static void drawImageWatermark(int sw, int sh) {
    if (!g_Surface) return;
    if (!g_S.wmImg || g_WmImgPath.empty()) {

        clearWatermarkImage();
        g_ImgDesiredPath.clear();
        g_ImgDesiredSince = GetTickCount();
        g_ImgLastAttempt = 0;
        g_ImgLoadFailed = false;
        return;
    }
    if (sw < 32 || sh < 32) return;

    const DWORD now = GetTickCount();

    if (g_ImgDesiredPath != g_WmImgPath) {
        clearWatermarkImage();
        g_ImgDesiredPath = g_WmImgPath;
        g_ImgDesiredSince = now;
        g_ImgLoadFailed = false;
        g_ImgLastAttempt = 0;
    }

    DecodedWatermarkImage decoded{};
    bool haveResult = false;
    {
        std::lock_guard<std::mutex> resultGuard(g_ImgResultMutex);
        if (g_ImgResultReady) {
            decoded = std::move(g_ImgResult);
            g_ImgResult = DecodedWatermarkImage{};
            g_ImgResultReady = false;
            haveResult = true;
        }
    }
    if (haveResult && decoded.path == g_WmImgPath) {
        if (!decoded.success || !uploadWatermarkImage(decoded)) {
            clearWatermarkImage();
            g_ImgLoadFailed = true;
            g_ImgLastAttempt = now;
        } else {
            g_ImgLoadFailed = false;
            g_ImgLastAttempt = now;
        }
    }

    bool cacheMatch = g_ImgLoadedPath == g_WmImgPath &&
        g_ImgTex >= 0 && g_ImgContentW > 0 && g_ImgContentH > 0 && !g_ImgFrames.empty();
    const bool workerBusy = g_ImgWorkerRunning.load();
    const DWORD retryMs = g_ImgLoadFailed ? 5000 : 350;
    const bool debounceReady = static_cast<DWORD>(now - g_ImgDesiredSince) >= 200;
    const bool retryReady = g_ImgLastAttempt == 0 || static_cast<DWORD>(now - g_ImgLastAttempt) >= retryMs;
    if (!cacheMatch && !workerBusy && debounceReady && retryReady) {
        initGdiplus();
        g_ImgLastAttempt = now;
        if (!g_GdipToken) {
            g_ImgLoadFailed = true;
        } else {
            try {
                startWatermarkDecode(g_WmImgPath);
            } catch (...) {
                g_ImgWorkerRunning.store(false);
                g_ImgLoadFailed = true;
                g_ImgLastAttempt = now;
            }
        }
    }

    cacheMatch = g_ImgLoadedPath == g_WmImgPath &&
        g_ImgTex >= 0 && g_ImgContentW > 0 && g_ImgContentH > 0 && !g_ImgFrames.empty();
    if (!cacheMatch) return;
    if (!advanceWatermarkAnimation(now)) return;

    int baseW = g_ImgTexContentW > 0 ? g_ImgTexContentW : g_ImgContentW;
    int baseH = g_ImgTexContentH > 0 ? g_ImgTexContentH : g_ImgContentH;
    if (baseW < 1) baseW = 1;
    if (baseH < 1) baseH = 1;
    float displayScale = g_S.wmImgScale / 100.f;
    if (displayScale < 0.05f) displayScale = 0.05f;
    if (displayScale > 8.f) displayScale = 8.f;

    const float screenFitX = static_cast<float>(sw) / static_cast<float>(baseW);
    const float screenFitY = static_cast<float>(sh) / static_cast<float>(baseH);
    const float screenFit = (std::min)(screenFitX, screenFitY);
    if (screenFit > 0.f && displayScale > screenFit) displayScale = screenFit;
    int w = (std::max)(1, static_cast<int>(baseW * displayScale + 0.5f));
    int h = (std::max)(1, static_cast<int>(baseH * displayScale + 0.5f));
    int x, y;

    if (g_S.wmImgDvd) {
        const DWORD moveNow = GetTickCount();
        float dt = g_DvdLast ? (moveNow - g_DvdLast) / 1000.f : 0.016f;
        if (dt > 0.05f) dt = 0.05f;
        g_DvdLast = moveNow;
        float dvdSpeed = g_S.wmImgDvdSpeed / 100.f;
        if (dvdSpeed < 0.1f) dvdSpeed = 0.1f;
        if (dvdSpeed > 5.f) dvdSpeed = 5.f;
        g_DvdX += g_DvdVx * dvdSpeed * dt;
        g_DvdY += g_DvdVy * dvdSpeed * dt;
        if (g_DvdX <= 0.f) { g_DvdX = 0.f; g_DvdVx = fabsf(g_DvdVx); }
        if (g_DvdY <= 0.f) { g_DvdY = 0.f; g_DvdVy = fabsf(g_DvdVy); }
        if (g_DvdX + w >= sw) { g_DvdX = static_cast<float>(sw - w); g_DvdVx = -fabsf(g_DvdVx); }
        if (g_DvdY + h >= sh) { g_DvdY = static_cast<float>(sh - h); g_DvdVy = -fabsf(g_DvdVy); }
        x = static_cast<int>(g_DvdX);
        y = static_cast<int>(g_DvdY);
    } else {
        x = static_cast<int>(sw * g_S.wmImgX / 100.f) - w;
        y = static_cast<int>(sh * g_S.wmImgY / 100.f);
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + w > sw) x = sw - w;
        if (y + h > sh) y = sh - h;
    }

    int a = static_cast<int>(g_S.wmImgAlpha);
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    if (!safeDrawWatermarkTexture(g_ImgTex, x, y, w, h, a)) {
        clearWatermarkImage();
        g_ImgLoadFailed = true;
        g_ImgLastAttempt = now;
    }
}

static void drawTextWatermark(int sw, int sh) {
    if (!g_S.watermark || sw < 32 || sh < 32 || g_WmText.empty() || !g_Surface) return;
    int px = (int)g_S.wmScale;
    if (px < 10) px = 10;
    if (px > 72) px = 72;
    DWORD now = GetTickCount();

    if (g_WmFont && now - g_WmFontLastCheck >= 1000) {
        g_WmFontLastCheck = now;
        int tall = 0;
        __try { tall = VCall<int, unsigned long>(g_Surface, 69, g_WmFont); }
        __except (EXCEPTION_EXECUTE_HANDLER) { tall = 0; }
        if (tall <= 0) { g_WmFont = 0; g_WmFontPx = -1; }
    }
    if (!g_WmFont && (g_WmFontLastCreateAttempt == 0 || now - g_WmFontLastCreateAttempt >= 500)) {
        g_WmFontLastCreateAttempt = now;
        __try { g_WmFont = VCall<unsigned long>(g_Surface, 66); }
        __except (EXCEPTION_EXECUTE_HANDLER) { g_WmFont = 0; }
        g_WmFontPx = -1;
    }
    bool needsGlyphRefresh = g_WmFontLastConfigured == 0 || now - g_WmFontLastConfigured >= 2500;
    if (g_WmFont && (g_WmFontPx != px || needsGlyphRefresh) &&
        (g_WmFontLastCreateAttempt == 0 || now - g_WmFontLastCreateAttempt >= 250)) {
        g_WmFontLastCreateAttempt = now;
        bool ok = false;
        __try {
            ok = VCall<bool, unsigned long, const char*, int, int, int, int, int, int, int>(
                g_Surface, 67, g_WmFont, "Tahoma", px, 700, 0, 0, 0x210, 0, 0);
            if (!ok) ok = VCall<bool, unsigned long, const char*, int, int, int, int, int, int, int>(
                g_Surface, 67, g_WmFont, "Arial", px, 700, 0, 0, 0x210, 0, 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (ok) { g_WmFontPx = px; g_WmFontLastConfigured = now; }
    }
    unsigned long drawFont = (g_WmFont && g_WmFontPx == px) ? g_WmFont : g_Font;
    if (!drawFont) return;
    wchar_t buf[48];
    toWide(g_WmText, buf, 48);
    int len = (int)wcslen(buf);
    if (len <= 0) return;
    int pad = (int)g_S.wmPad;
    if (pad < 2) pad = 2;
    if (pad > 24) pad = 24;

    int tw = 0, th = 0;
    VCall<void, unsigned long, const wchar_t*, int*, int*>(g_Surface, 75, drawFont, buf, &tw, &th);
    if (tw <= 0 || th <= 0) {
        tw = (int)(len * px * 0.62f);
        th = px + 2;
    }
    tw += pad * 2;
    th += pad * 2;
    int x = (int)(sw * g_S.wmX / 100.f) - tw;
    int y = (int)(sh * g_S.wmY / 100.f);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + tw > sw) x = sw - tw;
    if (y + th > sh) y = sh - th;
    if (g_S.wmBgOn) {
        int a = (int)g_S.wmAlpha;
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        fill(x, y, x + tw, y + th, (int)g_S.wmBgR, (int)g_S.wmBgG, (int)g_S.wmBgB, a);
    }
    textFont(drawFont, x + pad, y + pad, (int)g_S.wmTextR, (int)g_S.wmTextG, (int)g_S.wmTextB, buf);
}

void drawWatermark(int sw, int sh) {
    drawTextWatermark(sw, sh);
    drawImageWatermark(sw, sh);
}

void drawNetStats(int sw, int sh) {
    if (!g_S.netStats || !InGame()) return;
    NetStats ns;
    queryNetStats(ns);
    if (!ns.ok) return;
    wchar_t line[128];
    int x = 8, y = sh - 72;
    swprintf(line, 128, L"%hs", ns.level[0] ? ns.level : "map?");
    text(x, y, 200, 200, 200, line); y += 14;
    swprintf(line, 128, L"%hs", ns.address[0] ? ns.address : "local");
    text(x, y, 180, 180, 180, line); y += 14;
    swprintf(line, 128, L"ping %.0fms  loss %.1f%%  choke %.1f%%", ns.pingMs, ns.loss * 100.f, ns.choke * 100.f);
    text(x, y, 180, 180, 180, line); y += 14;
    swprintf(line, 128, L"pkt out %.1f  in %.1f", ns.pktOut, ns.pktIn);
    text(x, y, 160, 160, 160, line);
}

WatermarkDebugState getWatermarkDebugState() {
    WatermarkDebugState s{};
    s.textureId = g_ImgTex;
    s.width = g_ImgContentW;
    s.height = g_ImgContentH;
    s.textFont = g_WmFont;
    s.textFontPx = g_WmFontPx;
    s.gdiplusActive = g_GdipToken != 0;
    s.imageCached = !g_ImgLoadedPath.empty() && g_ImgContentW > 0 && g_ImgContentH > 0;
    s.imageLoading = g_ImgWorkerRunning.load();
    s.imageFailed = g_ImgLoadFailed;
    s.imageLastAttempt = g_ImgLastAttempt;
    return s;
}
