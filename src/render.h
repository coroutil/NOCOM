#pragma once
#include "sdk.h"
#include <string>
#include <wchar.h>

extern unsigned long g_Font;

void setColor(int r, int g, int b, int a);
void fill(int x0, int y0, int x1, int y1, int r, int g, int b, int a = 255);
void rect(int x0, int y0, int x1, int y1);
void line(int x0, int y0, int x1, int y1);
void textFont(unsigned long font, int x, int y, int r, int g, int b, const wchar_t* s);
unsigned long getOrCreateNamedFont(const std::string& face, int pixelHeight);
void refreshNamedFonts();
void measureText(unsigned long font, const wchar_t* text, int& width, int& height);
void text(int x, int y, int r, int g, int b, const wchar_t* s);
void toWide(const std::string& s, wchar_t* o, int n);

bool W2S(const Vec3& p, int sw, int sh, int& ox, int& oy);
void ensureUiFont();
void refreshWatermarkFont();
void drawWatermark(int sw, int sh);
struct WatermarkDebugState {
    int textureId;
    int width;
    int height;
    unsigned long textFont;
    int textFontPx;
    bool gdiplusActive;
    bool imageCached;
    bool imageLoading;
    bool imageFailed;
    unsigned long imageLastAttempt;
};
WatermarkDebugState getWatermarkDebugState();
void drawNetStats(int sw, int sh);
void initGdiplus();
void shutdownGdiplus();
void invalidateImageCache();
