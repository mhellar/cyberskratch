#include "Arduino.h"
#include <vector>
#include <string>
uint32_t fakeMs = 1000;
SerialC Serial; EspC ESP;
void (*i2sHook)(const int16_t*, size_t) = nullptr;
#include "game.inc"
static bool capture = false; static std::string capName;
void U8G2_SH1107_PIMORONI_128X128_F_HW_I2C::sendBuffer() {
  if (!capture) return;
  capture = false;
  for (int x = 0; x < 128; x++) for (int y = 0; y < 128; y++) px[y * 128 + x] = (raw[(x >> 3) * 128 + 127 - y] >> (x & 7)) & 1;
  FILE* f = fopen(("out/" + capName + ".pgm").c_str(), "wb");
  fprintf(f, "P5 128 128 255\n");
  for (int i = 0; i < 128 * 128; i++) fputc(px[i] ? 255 : 0, f);
  fclose(f);
}
struct Done {};
struct Shot { float at; const char* name; void (*set)(); };
static void base() { fx = FX_CLEAN; underOn = reverseOn = stutterOn = revFreeze = tapeOn = false; vjView = true; }
static Shot SHOTS[] = {
  {0.35f, "s00_0.3", [] {}},
  {0.85f, "s01_0.8", [] {}},
  {1.35f, "s02_1.3", [] {}},
  {1.75f, "s03_1.7", [] {}},
  {2.15f, "s04_2.1", [] {}},
  {2.65f, "s05_2.6", [] {}},
  {3.05f, "s06_3.0", [] {}},
  {3.65f, "s07_3.6", [] {}},
  {4.25f, "s08_4.2", [] {}},
  {5.25f, "s09_5.2", [] {}},
  {5.95f, "s10_5.9", [] {}},
  {6.65f, "s11_6.6", [] {}},
  {7.45f, "s12_7.4", [] {}},
  {8.35f, "s13_8.3", [] {}},
  {9.25f, "s14_9.2", [] {}},
  {10.05f, "s15_10.0", [] {}},
  {10.45f, "s16_10.4", [] {}},
  {11.05f, "s17_11.0", [] {}},
  {12.05f, "s18_12.0", [] {}},
  {13.55f, "s19_13.5", [] {}},
};
static double tms = 0; static size_t blocks = 0; static int shot = 0; static float nextFrame = 0, nextWord = 0;
static void hook(const int16_t*, size_t) {
  blocks++;
  tms += 1000.0 * BLOCK / FS; fakeMs = 1000 + (uint32_t)tms;
  float t = tms / 1000.0f;
  if (t >= nextWord) {                               // something to look at: words keep coming
    nextWord = t + 0.55f;
    if (true) {} else if (mode == M_DJ) { int w = randomWord(); if (t > 20.3f) deckAuto(w, 4, random(6)); else deckPlay(w, false); curWord = w; }
  }
  if (t >= nextFrame) {
    nextFrame = t + 0.05f;
    if (shot < (int)(sizeof SHOTS / sizeof SHOTS[0])) {
      Shot& s = SHOTS[shot];
      
      if (t >= s.at) { capture = true; capName = s.name; shot++; }
    } else throw Done();
    drawFrame();
  }
}
int main() {
  setup();
  renderBank(0, false);
  playing = false;
  i2sHook = hook;
  try { audioTask(nullptr); } catch (Done&) {}
  printf("frames done, %zu audio blocks\n", blocks);
}
