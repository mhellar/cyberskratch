#pragma once
#include <cstdint>
#include <cstring>
typedef const uint8_t* fontp;
static const uint8_t u8g2_font_4x6_tr[1] = {4}, u8g2_font_4x6_tf[1] = {4}, u8g2_font_5x8_tf[1] = {5}, u8g2_font_6x10_tf[1] = {6}, u8g2_font_helvB10_tr_[1] = {7}, u8g2_font_5x7_tr[1] = {5}, u8g2_font_helvB12_tr[1] = {8}, u8g2_font_helvB10_tr[1] = {7};
#define U8X8_PIN_NONE 255
#define U8G2_R1 1
// a 128x128 1-bit canvas; text is drawn as little blocks per character
struct U8G2_SH1107_PIMORONI_128X128_F_HW_I2C {
  uint8_t px[128 * 128]; int color = 1, cw = 4, ch = 6;
  U8G2_SH1107_PIMORONI_128X128_F_HW_I2C(int, int, int, int) { memset(px, 0, sizeof px); }
  void setBusClock(int) {} void begin() {} void setContrast(int) {}
  void clearBuffer() { memset(px, 0, sizeof px); memset(raw, 0, sizeof raw); }
  void sendBuffer();
  uint8_t raw[2048];
  uint8_t* getBufferPtr() { return raw; }
  void setFont(const uint8_t* f) { cw = f[0]; ch = f[0] + 2; }
  void setFontMode(int) {}
  void setDrawColor(int c) { color = c; }
  void drawPixel(int x, int y) { if (x < 0 || y < 0 || x > 127 || y > 127) return; uint8_t& p = raw[(x >> 3) * 128 + 127 - y]; uint8_t m = 1 << (x & 7); if (color == 2) p ^= m; else if (color) p |= m; else p &= ~m; }
  void drawBox(int x, int y, int w, int h) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) drawPixel(x + i, y + j); }
  void drawFrame(int x, int y, int w, int h) { drawHLine(x, y, w); drawHLine(x, y + h - 1, w); for (int j = 1; j < h - 1; j++) { drawPixel(x, y + j); drawPixel(x + w - 1, y + j); } }
  void drawVLine(int x, int y, int h) { for (int j = 0; j < h; j++) drawPixel(x, y + j); }
  void drawDisc(int x, int y, int r) { for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r) drawPixel(x + i, y + j); }
  void drawTriangle(int, int, int, int, int, int) {}
  void drawHLine(int x, int y, int w) { for (int i = 0; i < w; i++) drawPixel(x + i, y); }
  void drawLine(int x0, int y0, int x1, int y1) { int n = std::max(abs(x1 - x0), abs(y1 - y0)); for (int i = 0; i <= n; i++) drawPixel(x0 + (n ? (x1 - x0) * i / n : 0), y0 + (n ? (y1 - y0) * i / n : 0)); }
  int getStrWidth(const char* s) { return (int)strlen(s) * cw; }
  void drawStr(int x, int y, const char* s) {
    for (int i = 0; s[i]; i++) if (s[i] != ' ') for (int j = 0; j < ch - 2; j++) for (int k = 0; k < cw - 1; k++) if ((j + k + s[i]) % 3) drawPixel(x + i * cw + k, y - (ch - 2) + j + 1);
  }
};
