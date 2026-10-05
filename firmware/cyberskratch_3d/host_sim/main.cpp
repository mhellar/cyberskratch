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
  {1.3f, "01_dj_clean", [] { base(); mode = M_DJ; }},
  {2.3f, "02_dj_echo", [] { base(); fx = FX_ECHO; }},
  {3.3f, "03_dj_radio", [] { base(); fx = FX_RADIO; }},
  {4.3f, "04_dj_crush", [] { base(); fx = FX_CRUSH; }},
  {5.3f, "05_dj_choir", [] { base(); fx = FX_CHOIR; }},
  {6.3f, "06_dj_under", [] { base(); underOn = true; }},
  {7.3f, "07_dj_swarm", [] { base(); fx = FX_SWARM; }},
  {8.3f, "08_dj_vox", [] { base(); fx = FX_VOX; }},
  {9.3f, "09_dj_grain", [] { base(); fx = FX_GRAIN; }},
  {10.3f, "10_dj_deep", [] { base(); fx = FX_DEEP; }},
  {11.3f, "11_dj_reverse", [] { base(); reverseOn = true; }},
  {12.8f, "12_dj_infinite", [] { base(); revFreeze = true; }},
  {13.8f, "13_dj_stutter", [] { base(); stutterOn = true; }},
  {15.3f, "14_talk", [] { base(); mode = M_TALK; }},
  {16.3f, "15_talk_echo", [] { base(); mode = M_TALK; fx = FX_ECHO; }},
  {17.8f, "16_beat", [] { base(); mode = M_BEAT; grooveIdx = 2; }},
  {18.8f, "17_beat_radio", [] { base(); mode = M_BEAT; fx = FX_RADIO; }},
  {19.8f, "18_beat_infinite", [] { base(); mode = M_BEAT; revFreeze = true; }},
  {20.8f, "19_dj_scratch", [] { base(); mode = M_DJ; grooveIdx = 1; }},
  {21.8f, "20_hud", [] { base(); vjView = false; }},
};
static double tms = 0; static size_t blocks = 0; static int shot = 0; static float nextFrame = 0, nextWord = 0;
static void hook(const int16_t*, size_t) {
  blocks++;
  tms += 1000.0 * BLOCK / FS; fakeMs = 1000 + (uint32_t)tms;
  float t = tms / 1000.0f;
  if (t >= nextWord) {                               // something to look at: words keep coming
    nextWord = t + 0.55f;
    if (mode == M_TALK) { talkPlay(random(12), false); curTalk = 0; }
    else if (mode == M_DJ) { int w = randomWord(); if (t > 20.3f) deckAuto(w, 4, random(6)); else deckPlay(w, false); curWord = w; }
  }
  if (t >= nextFrame) {
    nextFrame = t + 0.05f;
    if (shot < (int)(sizeof SHOTS / sizeof SHOTS[0])) {
      Shot& s = SHOTS[shot];
      if (t >= s.at - 0.9f) s.set();
      if (t >= s.at) { capture = true; capName = s.name; shot++; }
    } else throw Done();
    drawFrame();
  }
}
int main() {
  setup();
  renderBank(0, false);
  splashOn = false; playing = true;
  i2sHook = hook;
  try { audioTask(nullptr); } catch (Done&) {}
  printf("frames done, %zu audio blocks\n", blocks);
}
