/*
  CYBERSKRATCH 3D - CyberSkratch (robodj_keypad, kept untouched) with a VJ view: abstract 3D glitch visuals that play
  along with the music (see "VJ view" below). SEQ + SHIFT held ~0.6 s = switch between the VJ view and the classic HUD.
  Holding MODE / FX / SEQ / SHIFT still shows the pad menu in both views.
  ===========================================================================================================
  ROBO//DJ KEYPAD ("CYBERSKRATCH" on screen) - the handheld Robo DJ grown up for keypad_synth (rev A / B, or rev C)
  A robot DJ: SAM (Software Automatic Mouth) words on a turntable deck you scratch - tempo-synced by holding a pad,
  or by hand with the knob - a Speak & Spell mouth (Talkie LPC) you can bend and freeze, a hip-hop drum machine,
  a 16-step sequencer, and the full Blasteroid effect rack (vocoder, grains, radio, echo, big reverb SPACE...).
  Grown out of s3_handheld_revA/handheld_robodj + blasteroid_megasynth (both left untouched).

  SAM (C core from earlephilhower/ESP8266SAM) and the Talkie LPC vocabularies (Peter Knight / Armin Joachimsmeyer)
  are bundled in this folder; both GPL v3, so this sketch is too.

  KEYPAD (keypad hanging below the board, S1 top-left)
     [MODE ] [ pad 0 ] [ pad 1 ] [ pad 2 ]
     [FX   ] [ pad 3 ] [ pad 4 ] [ pad 5 ]
     [SEQ  ] [ pad 6 ] [ pad 7 ] [ pad 8 ]
     [SHIFT] [ pad 9 ] [ pad 10] [ pad 11]
  Left column: tap for the quick action, HOLD it and the screen shows what the 12 pads do.
     MODE  tap = next mode (DJ > TALK > BEAT)   hold: modes, word page, volume, talk bank, chord prog, robot voice, replay
     FX    tap = next voice FX                  hold: CLEAN CRUSH ECHO RADIO GRAIN SWARM DEEP VOX CHOIR, STUTTER/REVERSE/TAPE
     SEQ   tap = REC on/off                     hold: play, groove, auto, BPM, mutate, crosstalk, clears, silence
     SHIFT tap = PLAY/STOP                      hold: per-mode extras (+ UNDERWATER and INFINITE reverb in every mode)
     MODE + SHIFT held ~1 s = the intro film with credits
  KNOB (the rotary pot)
     alone          = the current voice FX's main control (pitch, bits, echo feedback, radio tuning, grain density...)
     SHIFT + knob   = SCRATCH the current word by hand (SAM or Talkie), like a record under your fingers
     hold a pad + knob = scratch THAT word (DJ / TALK) or tune that drum (BEAT)
     FX + knob      = SPACE: from dry and close to a huge, long reverb
     SEQ + knob     = tempo        MODE + knob = volume
  Pads by mode:
     DJ    24 SAM words in two pages of 12. tap = say it, HOLD = tempo-synced turntable scratch (style on SHIFT)
     TALK  12 Speak & Spell words (4 banks). tap = say it, hold = freeze the mouth on one frame, humming the chord
     BEAT  12 drums (KICK SNARE CLAP / HAT OPEN RIM / TOM-L TOM-H 808 / COWBL ZAP CRASH). hold = roll
  With REC on (and the sequencer playing) words, scratches and drums are quantized into the 16 steps.

  Board: "ESP32S3 Dev Module", USB CDC On Boot: Enabled, PSRAM: enabled
  (arduino-cli FQBN esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=enabled; a DevKitC-1 N8R8 needs PSRAM=opi).
  Libraries: U8g2. ESP32 core 3.x.
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ESP_I2S.h>
#include "esp_dsp.h"
#include "SamData.h"
#include "sam.h"
#include "reciter.h"
#include "Vocab_US_Large.h"
#include "Vocab_AstroBlaster.h"
#include "Vocab_Soundbites.h"

// ---------------- pins: pick the board ----------------
#define BOARD_REV_C 0     // 0 = keypad_synth rev A / rev B (SuperMini), 1 = rev C (full-size ESP32-S3-DevKitC-1)
#if BOARD_REV_C
#define PIN_SDA    9
#define PIN_SCL    10
#define AMP_LRC    12
#define AMP_BCLK   13
#define AMP_DIN    14
#define PIN_RGB    48     // DevKitC v1.0 (v1.1 boards have it on 38)
#define PIN_POT    5      // wiper
#define PIN_POT_HI 4      // driven HIGH: powers the pot's high leg
const int ROW_PINS[4] = {1, 2, 42, 41};
const int COL_PINS[4] = {40, 39, 47, 21};
#else
#define PIN_SDA    9
#define PIN_SCL    8
#define AMP_LRC    1
#define AMP_BCLK   44
#define AMP_DIN    43
#define PIN_RGB    48
#define PIN_POT    10     // wiper
#define PIN_POT_HI 11     // driven HIGH: powers the pot's high leg
const int ROW_PINS[4] = {2, 3, 4, 5};
const int COL_PINS[4] = {6, 7, 13, 12};
#endif

#define STATUS_LOG 0      // 1 = print a status line every 2 s (only needed for debugging)
#define FS     22050      // SAM's native rate
#define INV_FS (1.0f / FS)
#define BLOCK  128

const float VOLS[6] = {0.05f, 0.09f, 0.14f, 0.2f, 0.28f, 0.38f};
volatile int volIdx = 2;

// ---------------- types first (Arduino auto-prototypes) ----------------
struct Word { const char* text; uint32_t off, len; };
struct Voice { const char* name; uint8_t speed, pitch, throat, mouth; };
struct Step { int8_t word; uint8_t src, flags; uint16_t drum; bool skip; };
struct LpcFrame { uint8_t energy, period; int16_t k1, k2; int8_t k[8]; };
struct TalkWord { const char* name; const uint8_t* data; uint32_t first, n; };
struct PercDef { const char* name; uint8_t wave; float f0, f1, ptime, dec, gain, noise, drive; };
struct ModKey { bool down, used; uint32_t tDown; };
struct SVF { float lo = 0, bp = 0; };                 // Chamberlin state-variable filter state
struct Rng {                                         // small deterministic RNG
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed * 2654435761u + 12345u) {}
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float uni(float a, float b) { return a + (b - a) * (next() & 0xFFFFFF) / 16777216.0f; }
  int range(int a, int b) { return a + (int)(next() % (uint32_t)(b - a)); }
};
struct Grain { int word; float pos, inc, age, len, amp; bool on; };
struct Groove { const char* name; const char* lane[4]; uint8_t drum[4]; };
struct Prog { const char* name; int8_t root[4]; uint8_t q[4]; };
// VJ view (3D glitch visuals)
struct V3 { float x, y, z; };
struct Tri3 { uint8_t a, b, c; };
struct Mesh { V3 v[64]; Tri3 t[128]; uint8_t nv, nt; };
struct PTri { float z; int16_t x[3], y[3]; uint8_t shade, wire; };
struct Ring { float z, rot, size; uint8_t kind; bool on; };
struct Part { float x, y, z; };
volatile uint32_t wordTrig = 0;                     // counts every word the deck / mouth starts (the visuals react)
volatile int trigWord = -1, trigSrc = 0;

enum { M_DJ, M_TALK, M_BEAT, M_COUNT };
const char* MODE_NAMES[] = {"DJ", "TALK", "BEAT"};
enum { K_MODE, K_FX, K_SEQ, K_SHIFT };
const char* MOD_NAMES[] = {"MODE", "FX", "SEQ", "SHIFT"};
enum { SRC_SAM, SRC_TALK };
enum { SF_SCR = 1, SF_REV = 2 };
enum { FX_CLEAN, FX_CRUSH, FX_ECHO, FX_RADIO, FX_GRAIN, FX_SWARM, FX_DEEP, FX_VOX, FX_CHOIR, FX_COUNT };
const char* FX_NAMES[] = {"CLEAN", "CRUSH", "ECHO", "RADIO", "GRAIN", "SWARM", "DEEP", "VOX", "CHOIR"};
const char* FX_KNOB[] = {"PITCH", "BITS", "FEEDBACK", "TUNING", "DENSITY", "SPREAD", "DEPTH", "WET", "BREATH"};
enum { SC_BABY, SC_CHIRP, SC_TRANS, SC_TEAR, SC_FLARE, SC_CRAB, SC_COUNT };
const char* SC_NAMES[] = {"BABY", "CHIRP", "TRANSFORM", "TEAR", "FLARE", "CRAB"};
const char* SC_SHORT[] = {"BABY", "CHIRP", "TRANS", "TEAR", "FLARE", "CRAB"};
enum { DK_PLAY, DK_AUTO, DK_KNOB, DK_FREEZE, DK_SPIN };   // how the deck is moving

const Voice VOICES[] = {
  {"ROBOT", 92, 60, 190, 190}, {"SAM", 72, 64, 128, 128}, {"ELF", 72, 64, 110, 160},
  {"E.T.", 100, 64, 150, 200}, {"STUFFY", 82, 72, 110, 105}, {"OLD LADY", 82, 32, 145, 145},
};
#define NVOICES 6
// two pages of 12 pad words; bank[24..26] = the name and its two halves, cut up in the intro routine
// (spelled for SAM: "SIGHBER SKRATCH")
#define NWORDS 24
#define NAME_WORD 24
#define NAME_A 25
#define NAME_B 26
#define NBANKW 27
Word bank[NBANKW] = {
  // a balanced lexicon (Mark, 10-04): page A = cybernetics, the machine (Norbert Wiener: kybernetes = steersman),
  // page B = mind and wonder, with a little warmth. Picked for sound too: plosives, hiss, long vowels scratch differently.
  // (CYBERNETICS swapped for AUTOMATON - too close to "Dianetics" for comfort)
  // + a little Henry Miller for light (titles and his motto)
  {"AUTOMATON"}, {"FEEDBACK"}, {"ENTROPY"}, {"SIGNAL"}, {"NOISE"}, {"HIGHER STATE"},
  {"HOMEOSTASIS"}, {"INFORMATION"}, {"MESSAGE"}, {"LOOP"}, {"OSCILLATE"}, {"ZERO ONE"},
  {"MEMORY"}, {"PATTERN"}, {"SYNAPSE"}, {"PARADOX"}, {"ECHO"}, {"NEXUS"},
  {"KALEIDOSCOPE"}, {"WONDER"}, {"ALWAYS MERRY AND BRIGHT"}, {"STAND STILL LIKE THE HUMMINGBIRD"}, {"INNER SPACE"}, {"CRYSTALLINE"},
  {"SIGHBER SKRATCH"}, {"SIGHBER"}, {"SKRATCH"},
};

#define NBANKS 5
TalkWord talk[NBANKS][12] = {
  // the Speak & Spell side keeps its machine DNA: these chips spoke for cockpits, radar and test benches
  {{"CONTROL", sp4_CONTROL}, {"INFORMATION", sp4_INFORMATION}, {"FREQUENCY", sp2_FREQUENCY}, {"CIRCUIT", sp2_CIRCUIT},
   {"DEVICE", sp2_DEVICE}, {"SEQUENCE", sp4_SEQUENCE}, {"POSITION", sp2_POSITION}, {"CALIBRATE", sp2_CALIBRATE},
   {"MEASURE", sp2_MEASURE}, {"CONNECT", sp2_CONNECT}, {"AUTOMATIC", sp2_AUTOMATIC}, {"MACHINE", sp2_MACHINE}},
  {{"ONE", sp3_ONE}, {"TWO", sp3_TWO}, {"THREE", sp3_THREE}, {"FOUR", sp3_FOUR},
   {"FIVE", sp3_FIVE}, {"SIX", sp3_SIX}, {"SEVEN", sp3_SEVEN}, {"EIGHT", sp3_EIGHT},
   {"PLUS", sp3_PLUS}, {"MINUS", sp3_MINUS}, {"TIMES", sp3_TIMES}, {"EQUALS", sp3_EQUALS}},
  {{"AUTOPILOT", sp5_AUTOPILOT}, {"VECTORS", sp4_VECTORS}, {"RADAR", sp4_RADAR}, {"APPROACH", sp5_APPROACH},
   {"ALTITUDE", sp5_ALTITUDE}, {"HEADING", sp5_HEADING}, {"CONVERGING", sp5_CONVERGING}, {"TOUCHDOWN", sp5_TOUCHDOWN},
   {"STALL", sp5_STALL}, {"CLIMB", sp5_CLIMB}, {"INBOUND", sp5_INBOUND}, {"FREEDOM", sp5_FREEDOM}},
  {{"ALPHA", sp4_ALPHA}, {"BRAVO", sp4_BRAVO}, {"CHARLIE", sp4_CHARLIE}, {"DELTA", sp4_DELTA},
   {"ECHO", sp4_ECHO}, {"FOXTROT", sp4_FOXTROT}, {"HOTEL", sp4_HOTEL}, {"KILO", sp4_KILO},
   {"NOVEMBER", sp4_NOVEMBER}, {"SIERRA", sp4_SIERRA}, {"WHISKEY", sp4_WHISKEY}, {"ZULU", sp4_ZULU}},
  {{"THUNDERSTORM", sp3_THUNDERSTORM}, {"TURBULENCE", sp3_TURBULANCE}, {"CRYSTALS", sp5_CRYSTALS}, {"VISIBILITY", sp3_VISIBILITY},
   {"OBSCURED", sp3_OBSCURED}, {"UNLIMITED", sp3_UNLIMITED}, {"HAZE", sp3_HAZE}, {"MIST", sp3_MIST},
   {"FREEZING", sp3_FREEZING}, {"ALOFT", sp3_ALOFT}, {"DRIZZLE", sp3_DRIZZLE}, {"GREENWICH", sp3_GREENWICH}},
};
const char* BANK_NAMES[NBANKS] = {"CONTROL", "MATH", "FLIGHT", "ALPHA", "SKY"};

// drum kit. drive > 0 = through softclip after the envelope. W_FM: f0 carrier, f1 ratio, noise = index.
// W_METAL: f0 scales the six 808 cymbal squares.
enum { W_SINE, W_KICK, W_SNR, W_CLAP, W_METAL, W_FM, W_SLAM };
const PercDef PERC[12] = {
  // name     wave     f0     f1     ptime   dec    gain   noise  drive
  {"KICK",  W_KICK,  190,   52,    0.045f, 0.40f, 0.95f, 0.6f,  2.2f},
  {"SNARE", W_SNR,   210,   165,   0.025f, 0.20f, 0.62f, 0.9f,  2.0f},
  {"CLAP",  W_CLAP,  0,     0,     0.01f,  0.16f, 0.72f, 0,     1.6f},
  {"HAT",   W_METAL, 1.6f,  0,     0.01f,  0.035f,0.67f, 0,     0},
  {"OPEN",  W_METAL, 1.6f,  0,     0.01f,  0.28f, 0.60f, 0,     0},
  {"RIM",   W_SNR,   820,   640,   0.006f, 0.035f,0.60f, 0.35f, 1.6f},
  {"TOM-L", W_KICK,  180,   110,   0.07f,  0.38f, 0.75f, 0.3f,  1.4f},
  {"TOM-H", W_KICK,  300,   190,   0.06f,  0.30f, 0.70f, 0.3f,  1.4f},
  {"808",   W_KICK,  110,   50,    0.09f,  1.00f, 0.85f, 0.2f,  1.7f},
  {"COWBL", W_FM,    560,   1.48f, 0.05f,  0.22f, 0.40f, 0.5f,  1.3f},
  {"ZAP",   W_SINE,  2600,  70,    0.025f, 0.18f, 0.55f, 0,     0},
  {"CRASH", W_METAL, 1.0f,  0,     0.01f,  1.10f, 0.58f, 0,     0},
};
enum { D_KICK, D_SNARE, D_CLAP, D_HAT, D_OPEN, D_RIM, D_TOML, D_TOMH, D_808, D_COWBL, D_ZAP, D_CRASH };

// built-in grooves (the sequencer's own drum steps play on top)
const Groove GROOVES[] = {
  {"OFF",      {"................", "................", "................", "................"}, {D_KICK, D_SNARE, D_HAT, D_OPEN}},
  {"BOOM BAP", {"x.......x.x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "................"}, {D_KICK, D_SNARE, D_HAT, D_OPEN}},
  {"BREAK",    {"x.x.......xx....", "....x..x.x..x..x", "x.x.x.x.x.x.x.x.", "..............x."}, {D_KICK, D_SNARE, D_HAT, D_OPEN}},
  {"ELECTRO",  {"x..x..x...x.....", "....x.......x...", "x.xxx.xxx.xxx.xx", "x.........x....."}, {D_KICK, D_CLAP, D_HAT, D_808}},
  {"HALFTIME", {"x.........x.....", "........x.......", "x.x.x.x.x.x.x.x.", "..............x."}, {D_KICK, D_SNARE, D_HAT, D_OPEN}},
};
#define NGROOVES 5
uint16_t grooveMask[NGROOVES][16];

// vocoder chord progression (one chord per bar)
enum { Q_MAJ, Q_MIN, Q_MAJ7, Q_MIN7, Q_DOM7, Q_SUS4 };
const int8_t QI[6][4] = {{0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10}, {0, 5, 7, -1}};
const char* QN[6] = {"", "m", "maj7", "m7", "7", "sus4"};
const char* NOTE[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const Prog PROGS[] = {
  {"EPIC",  {9, 5, 0, 7},  {Q_MIN, Q_MAJ, Q_MAJ, Q_MAJ}},       // Am F C G
  {"POP",   {0, 7, 9, 5},  {Q_MAJ, Q_MAJ, Q_MIN, Q_MAJ}},       // C G Am F
  {"DREAM", {0, 9, 5, 7},  {Q_MAJ7, Q_MIN7, Q_MAJ7, Q_DOM7}},   // Cmaj7 Am7 Fmaj7 G7
  {"SPACE", {0, 10, 8, 7}, {Q_MAJ, Q_MAJ, Q_MAJ, Q_SUS4}},      // C Bb Ab Gsus4
};
#define NPROGS 4

// Serial log that never blocks: with USB plugged in but no serial monitor open, the USB-CDC TX buffer fills
// and a plain Serial.printf hangs the board. Lines that don't fit are dropped.
void logf(const char* fmt, ...) {
  char b[160];
  va_list ap; va_start(ap, fmt);
  int n = vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  n = min(n, (int)sizeof b - 1);
  if (Serial.availableForWrite() >= n) Serial.write((const uint8_t*)b, n);
}

U8G2_SH1107_PIMORONI_128X128_F_HW_I2C oled(U8G2_R1, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);   // R1 = 90 deg clockwise
I2SClass amp;

// Double buffering: a display task pushes the finished frame over I2C (~50 ms) while loop() draws the next one.
// Both run on core 1: the I2C wait is idle time (on core 0 the audio task slowed the transfer down).
#ifndef HOST_SIM
static uint8_t fbuf[2][128 * 16];
static int drawIdx = 0, sendIdx = 0;
static volatile bool sendBusy = false;
static TaskHandle_t dispHandle = nullptr;
void displayTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    u8x8_t* u = oled.getU8x8();
    for (int r = 0; r < 16; r++) u8x8_DrawTile(u, 0, r, 16, fbuf[sendIdx] + r * 128);
    sendBusy = false;
  }
}
void present() {
  while (sendBusy) vTaskDelay(1);
  sendIdx = drawIdx; sendBusy = true;
  xTaskNotifyGive(dispHandle);
  drawIdx ^= 1;
  oled.getU8g2()->tile_buf_ptr = fbuf[drawIdx];
}
void setupDisplayTask() {
  oled.getU8g2()->tile_buf_ptr = fbuf[0];
  xTaskCreatePinnedToCore(displayTask, "disp", 4096, nullptr, 2, &dispHandle, 1);
}
#else
void present() { oled.sendBuffer(); }
void setupDisplayTask() {}
#endif

void drawFrame();

// =====================================================================
// SAM -> sample pool
// =====================================================================
SamData* samdata;
int8_t* pool = nullptr;
uint32_t poolSize = 0, poolPos = 0;
volatile bool bankBusy = true;
volatile bool nameReady = false;                    // the name words render first
volatile int voiceIdx = 0;
int renderProgress = 0;

static void samByte(void*, unsigned char b) { if (poolPos < poolSize) pool[poolPos++] = (int8_t)((int)b - 128); }

bool sayToPool(const char* text, const Voice& v) {
  char input[256];
  int n = 0;
  for (; text[n] && n < 200; n++) input[n] = toupper((int)text[n]);
  input[n] = 0;
  strcat(input, "[");
  samdata = new SamData;
  if (!samdata) return false;
  EnableSingmode(0);
  SetSpeed(v.speed); SetPitch(v.pitch); SetThroat(v.throat); SetMouth(v.mouth);
  bool ok = TextToPhonemes(input);
  if (ok) { SetInput(input); SAMMain(samByte, nullptr); }
  delete samdata;
  return ok;
}

void renderBank(int vi, bool draw) {
  bankBusy = true; nameReady = false;
  delay(30);
  poolPos = 0;
  for (int step = 0; step < NBANKW; step++) {
    int i = step < 3 ? NAME_WORD + step : step - 3;  // the name first, then the pad words
    renderProgress = step * NWORDS / NBANKW;
    if (draw) drawFrame();
    uint32_t start = poolPos;
    bank[i].off = start; bank[i].len = 0;
    if (poolSize - poolPos < FS / 2) continue;       // pool full: skip the rest
    if (!sayToPool(bank[i].text, VOICES[vi])) { poolPos = start; continue; }
    uint32_t a = start, b = poolPos;                 // trim silence at both ends
    while (a < b && abs(pool[a]) < 4) a++;
    while (b > a && abs(pool[b - 1]) < 4) b--;
    bank[i].off = a; bank[i].len = b > a + 8 ? b - a : 0;
    if (i == NAME_B) nameReady = true;
  }
  renderProgress = NWORDS;
  logf("SAM voice %s: %u bytes\n", VOICES[vi].name, poolPos);
  bankBusy = false;
}

static inline float wordAt(int w, float p) {
  const Word& wd = bank[w];
  if (p < 0 || p >= (float)wd.len - 1) return 0;          // float compare: len 0 must not wrap to 4 billion
  int i = (int)p; float f = p - i;
  float a = pool[wd.off + i], b = pool[wd.off + i + 1];
  return (a + (b - a) * f) * (1.0f / 128.0f);
}

// =====================================================================
// Talkie LPC: words decoded to frames at boot, re-synthesized live (so they can be bent)
// Tables and lattice filter from Talkie (Peter Knight) via ESP8266Audio's AudioGeneratorTalkie.
// =====================================================================
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
static const uint8_t tmsEnergy[0x10] = {0x00,0x02,0x03,0x04,0x05,0x07,0x0a,0x0f,0x14,0x20,0x29,0x39,0x51,0x72,0xa1,0xff};
static const uint8_t tmsPeriod[0x40] = {0x00,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2D,0x2F,0x31,0x33,0x35,0x36,0x39,0x3B,0x3D,0x3F,0x42,0x45,0x47,0x49,0x4D,0x4F,0x51,0x55,0x57,0x5C,0x5F,0x63,0x66,0x6A,0x6E,0x73,0x77,0x7B,0x80,0x85,0x8A,0x8F,0x95,0x9A,0xA0};
static const int16_t tmsK1[0x20] = {0x82C0,0x8380,0x83C0,0x8440,0x84C0,0x8540,0x8600,0x8780,0x8880,0x8980,0x8AC0,0x8C00,0x8D40,0x8F00,0x90C0,0x92C0,0x9900,0xA140,0xAB80,0xB840,0xC740,0xD8C0,0xEBC0,0x0000,0x1440,0x2740,0x38C0,0x47C0,0x5480,0x5EC0,0x6700,0x6D40};
static const int16_t tmsK2[0x20] = {0xAE00,0xB480,0xBB80,0xC340,0xCB80,0xD440,0xDDC0,0xE780,0xF180,0xFBC0,0x0600,0x1040,0x1A40,0x2400,0x2D40,0x3600,0x3E40,0x45C0,0x4CC0,0x5300,0x5880,0x5DC0,0x6240,0x6640,0x69C0,0x6CC0,0x6F80,0x71C0,0x73C0,0x7580,0x7700,0x7E80};
static const int8_t tmsK3[0x10] = {0x92,0x9F,0xAD,0xBA,0xC8,0xD5,0xE3,0xF0,0xFE,0x0B,0x19,0x26,0x34,0x41,0x4F,0x5C};
static const int8_t tmsK4[0x10] = {0xAE,0xBC,0xCA,0xD8,0xE6,0xF4,0x01,0x0F,0x1D,0x2B,0x39,0x47,0x55,0x63,0x71,0x7E};
static const int8_t tmsK5[0x10] = {0xAE,0xBA,0xC5,0xD1,0xDD,0xE8,0xF4,0xFF,0x0B,0x17,0x22,0x2E,0x39,0x45,0x51,0x5C};
static const int8_t tmsK6[0x10] = {0xC0,0xCB,0xD6,0xE1,0xEC,0xF7,0x03,0x0E,0x19,0x24,0x2F,0x3A,0x45,0x50,0x5B,0x66};
static const int8_t tmsK7[0x10] = {0xB3,0xBF,0xCB,0xD7,0xE3,0xEF,0xFB,0x07,0x13,0x1F,0x2B,0x37,0x43,0x4F,0x5A,0x66};
static const int8_t tmsK8[0x08] = {0xC0,0xD8,0xF0,0x07,0x1F,0x37,0x4F,0x66};
static const int8_t tmsK9[0x08] = {0xC0,0xD4,0xE8,0xFC,0x10,0x25,0x39,0x4D};
static const int8_t tmsK10[0x08] = {0xCD,0xDF,0xF1,0x04,0x16,0x20,0x3B,0x4D};
static const int8_t chirp[] = {0x00,0x2a,0xd4,0x32,0xb2,0x12,0x25,0x14,0x02,0xe1,0xc5,0x02,0x5f,0x5a,0x05,0x0f,0x26,0xfc,0xa5,0xa5,0xd6,0xdd,0xdc,0xfc,0x25,0x2b,0x22,0x21,0x0f,0xff,0xf8,0xee,0xed,0xef,0xf7,0xf6,0xfa,0x00,0x03,0x02,0x01};
#pragma GCC diagnostic pop

LpcFrame* lpcFrames = nullptr;
uint32_t lpcCap = 0, lpcUsed = 0;
volatile int talkBank = 0;

struct BitReader {
  const uint8_t* p; uint8_t bit = 0;
  static uint8_t rev(uint8_t a) {
    a = (a >> 4) | (a << 4); a = ((a & 0xcc) >> 2) | ((a & 0x33) << 2); a = ((a & 0xaa) >> 1) | ((a & 0x55) << 1); return a;
  }
  uint8_t get(uint8_t bits) {
    uint16_t data = rev(p[0]) << 8;
    if (bit + bits > 8) data |= rev(p[1]);
    data <<= bit;
    uint8_t v = data >> (16 - bits);
    bit += bits;
    if (bit >= 8) { bit -= 8; p++; }
    return v;
  }
};

// decode one LPC word; out == nullptr just counts frames
uint32_t decodeLpc(const uint8_t* data, LpcFrame* out, uint32_t maxN) {
  BitReader br{data};
  LpcFrame cur = {};
  uint32_t n = 0;
  while (n < maxN) {
    uint8_t e = br.get(4);
    if (e == 0xF) break;
    if (e == 0) cur.energy = 0;
    else {
      cur.energy = tmsEnergy[e];
      uint8_t rep = br.get(1);
      cur.period = tmsPeriod[br.get(6)];
      if (!rep) {
        cur.k1 = tmsK1[br.get(5)]; cur.k2 = tmsK2[br.get(5)];
        cur.k[0] = tmsK3[br.get(4)]; cur.k[1] = tmsK4[br.get(4)];
        if (cur.period) {
          cur.k[2] = tmsK5[br.get(4)]; cur.k[3] = tmsK6[br.get(4)]; cur.k[4] = tmsK7[br.get(4)];
          cur.k[5] = tmsK8[br.get(3)]; cur.k[6] = tmsK9[br.get(3)]; cur.k[7] = tmsK10[br.get(3)];
        }
      }
    }
    if (out) out[n] = cur;
    n++;
  }
  return n;
}

void decodeTalk() {
  const uint32_t MAXW = 8000;
  uint32_t total = 0;
  for (auto& b : talk) for (auto& w : b) total += decodeLpc(w.data, nullptr, MAXW);
  lpcCap = total;
  lpcFrames = psramFound() ? (LpcFrame*)ps_malloc(total * sizeof(LpcFrame)) : nullptr;
  if (!lpcFrames) lpcFrames = (LpcFrame*)malloc(total * sizeof(LpcFrame));
  if (!lpcFrames) { logf("!! no memory for LPC frames\n"); return; }
  for (auto& b : talk) for (auto& w : b) {
    uint32_t n = decodeLpc(w.data, lpcFrames + lpcUsed, MAXW);
    uint32_t a = 0;
    while (a < n && lpcFrames[lpcUsed + a].energy == 0) a++;      // trim silent frames at both ends
    while (n > a && lpcFrames[lpcUsed + n - 1].energy == 0) n--;
    w.first = lpcUsed + a; w.n = n - a;
    lpcUsed += n;
  }
  logf("LPC: %u frames decoded (%u KB)\n", lpcUsed, lpcUsed * sizeof(LpcFrame) / 1024);
}

// live LPC voice with circuit bends
volatile bool lpWhisper = false, lpMono = false, lpFreeze = false, lpStretch = false, lpGlitch = false, lpRev = false;
volatile float lpPitch = 1, lpFormant = 1;
volatile float monoFreq = 130;

struct Lpc {
  int bank = 0, word = -1; float fpos = 0; bool on = false, scratch = false, rev = false;
  int loaded = -1;
  uint8_t period = 0; int32_t energy = 0, k1 = 0, k2 = 0; int32_t k[8] = {0};
  uint8_t pc = 0; int32_t x[10] = {0}; uint16_t rnd = 1;
  float ph = 0, y0 = 0, y1 = 0;
} lpc;
volatile int lpcVisFrame = -1;

void talkPlay(int w, bool rev) {
  if (!lpcFrames) return;
  const TalkWord& t = talk[talkBank][w];
  if (!t.n) return;
  trigWord = w; trigSrc = SRC_TALK; wordTrig++;
  lpc.bank = talkBank; lpc.word = w; lpc.rev = rev ^ lpRev; lpc.scratch = false;
  lpc.fpos = lpc.rev ? t.n - 0.01f : 0; lpc.loaded = -1; lpc.on = true;
}

void lpcLoad(int fi) {
  const TalkWord& t = talk[lpc.bank][lpc.word];
  const LpcFrame& f = lpcFrames[t.first + fi];
  lpc.loaded = fi;
  lpc.energy = f.energy;
  lpc.k1 = f.k1; lpc.k2 = f.k2;
  for (int i = 0; i < 8; i++) lpc.k[i] = f.k[i];
  if (lpGlitch) {                                   // circuit bend: corrupt some coefficients
    if (random(3) == 0) lpc.k1 = tmsK1[random(32)];
    if (random(3) == 0) lpc.k2 = tmsK2[random(32)];
    if (random(2) == 0) lpc.k[random(8)] = -lpc.k[random(8)];
  }
  int per = f.period;
  if (lpWhisper) per = 0;
  else if (per) {
    if (lpMono) per = (int)(8000.0f / monoFreq);
    else per = (int)(per / lpPitch);
    per = constrain(per, 10, 255);
  }
  lpc.period = per;
}

// one 8 kHz sample
static inline float lpcTick() {
  const TalkWord& t = talk[lpc.bank][lpc.word];
  if (!lpc.on || !t.n) return 0;
  int fi = (int)lpc.fpos;
  if (fi < 0 || fi >= (int)t.n) { if (!lpc.scratch) { lpc.on = false; return 0; } fi = constrain(fi, 0, (int)t.n - 1); }
  if (fi != lpc.loaded) lpcLoad(fi);
  if (!lpc.scratch && !lpFreeze) lpc.fpos += (lpc.rev ? -1.0f : 1.0f) * (lpStretch ? 0.4f : 1.0f) / 200.0f;

  int32_t u10;
  if (lpc.period) {
    if (lpc.pc < lpc.period) lpc.pc++; else lpc.pc = 0;
    u10 = lpc.pc < sizeof(chirp) ? (chirp[lpc.pc] * lpc.energy) >> 8 : 0;
  } else {
    lpc.rnd = (lpc.rnd >> 1) ^ ((lpc.rnd & 1) ? 0xB800 : 0);
    u10 = (lpc.rnd & 1) ? lpc.energy : -lpc.energy;
  }
  int32_t* x = lpc.x; int32_t* k = lpc.k;
  int32_t u9 = u10 - ((k[7] * x[9]) >> 7);
  int32_t u8 = u9 - ((k[6] * x[8]) >> 7);
  int32_t u7 = u8 - ((k[5] * x[7]) >> 7);
  int32_t u6 = u7 - ((k[4] * x[6]) >> 7);
  int32_t u5 = u6 - ((k[3] * x[5]) >> 7);
  int32_t u4 = u5 - ((k[2] * x[4]) >> 7);
  int32_t u3 = u4 - ((k[1] * x[3]) >> 7);
  int32_t u2 = u3 - ((k[0] * x[2]) >> 7);
  int32_t u1 = u2 - ((lpc.k2 * x[1]) >> 15);
  int32_t u0 = u1 - ((lpc.k1 * x[0]) >> 15);
  u0 = constrain(u0, -512, 511);
  x[9] = x[8] + ((k[6] * u8) >> 7);
  x[8] = x[7] + ((k[5] * u7) >> 7);
  x[7] = x[6] + ((k[4] * u6) >> 7);
  x[6] = x[5] + ((k[3] * u5) >> 7);
  x[5] = x[4] + ((k[2] * u4) >> 7);
  x[4] = x[3] + ((k[1] * u3) >> 7);
  x[3] = x[2] + ((k[0] * u2) >> 7);
  x[2] = x[1] + ((lpc.k2 * u1) >> 15);
  x[1] = x[0] + ((lpc.k1 * u0) >> 15);
  x[0] = u0;
  for (int i = 0; i < 10; i++) x[i] = constrain(x[i], -32768, 32767);
  return u0 * (1.0f / 512.0f);
}

// =====================================================================
// DSP helpers + drums
// =====================================================================
float sineTab[1025];
static inline float wrap1(float p) { return p - floorf(p); }
static inline float fsin(float ph) {
  float x = ph * 1024.0f; int i = (int)x; float f = x - i; i &= 1023;
  return sineTab[i] + (sineTab[i + 1] - sineTab[i]) * f;
}
static uint32_t nstate = 22222;
static inline float nz() { nstate ^= nstate << 13; nstate ^= nstate >> 17; nstate ^= nstate << 5; return (int32_t)nstate * (1.0f / 2147483648.0f); }
static inline float dcoef(float sec) { return expf(-1.0f / (sec * FS)); }
static inline float softclip(float x) { if (x > 3) return 1; if (x < -3) return -1; return x * (27 + x * x) / (27 + 9 * x * x); }
static inline float midiHz(float m) { return 440.0f * exp2f((m - 69) * (1.0f / 12.0f)); }

struct Perc {
  float env = 0, d = 0.99f, penv = 0, pd = 0.99f, ph = 0, ph2 = 0, prev = 0, lp = 0, bp = 0;
  float mph[6] = {0};
  uint32_t age = 0;
  void trig(const PercDef& p, float a) { env = a; d = dcoef(p.dec); penv = 1; pd = dcoef(p.ptime); ph = 0; age = 0; }
  inline float s(const PercDef& p, float rate) {
    if (env < 0.0001f) return 0;
    float f = (p.f1 + (p.f0 - p.f1) * penv) * rate, o = 0;
    penv *= pd;
    switch (p.wave) {
      case W_SINE: ph = wrap1(ph + f * INV_FS); o = fsin(ph) + nz() * p.noise * penv; break;
      case W_KICK: {                                   // sine sweep + a noise click on the front
        ph = wrap1(ph + f * INV_FS);
        o = fsin(ph) + (age < 110 ? nz() * p.noise * (1 - age / 110.0f) : 0);
        break;
      }
      case W_SNR: {                                    // body that thins out + bright noise that stays
        ph = wrap1(ph + f * INV_FS);
        float n = nz(), h = n - prev; prev = n;
        o = fsin(ph) * (0.2f + 0.8f * penv) + h * p.noise;
        break;
      }
      case W_CLAP: {                                   // three quick bursts, then the tail
        float n = nz(), q = 0.42f; lp += q * bp; bp += q * (n - lp - 0.6f * bp);
        float g = age < 660 ? 1.0f - (age % 220) / 260.0f : 1.0f;
        o = bp * 1.6f * g;
        break;
      }
      case W_METAL: {                                  // 808 cymbal: six detuned squares, high-passed
        static const float MF[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
        float sum = 0, sc = p.f0 * rate * INV_FS;
        for (int i = 0; i < 6; i++) { mph[i] = wrap1(mph[i] + MF[i] * sc); sum += mph[i] < 0.5f ? 1 : -1; }
        lp += 0.55f * (sum - lp);
        float h = sum - lp;
        bp += 0.55f * (h - bp);
        o = h - bp;
        break;
      }
      case W_FM: {                                     // inharmonic clang, index falls with penv
        float fc = p.f0 * rate;
        ph = wrap1(ph + fc * INV_FS); ph2 = wrap1(ph2 + fc * p.f1 * INV_FS);
        o = fsin(wrap1(ph + p.noise * (0.25f + penv) * fsin(ph2)));
        break;
      }
      case W_SLAM: {
        ph = wrap1(ph + f * INV_FS);
        lp += 0.12f * (nz() - lp);
        o = lp * 2.6f * (0.35f + 0.65f * penv) + fsin(ph) * penv;
        break;
      }
    }
    age++;
    o *= env; env *= d;
    if (p.drive > 0) o = softclip(o * p.drive) * (1.0f / 1.15f);
    return o * p.gain;
  }
};
Perc perc[12];
volatile uint16_t drumMute = 0;
volatile float drumTune[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
void hitDrums(uint16_t bits) {
  if (bits & (1 << D_HAT)) perc[D_OPEN].env *= 0.08f;          // closed hat chokes the open one
  for (int i = 0; i < 12; i++) if (bits & (1 << i)) perc[i].trig(PERC[i], 1);
}

#define DLY_N 16384
float* dlyBuf = nullptr;
int dlyW = 0;

// big reverb: 4-line feedback delay network, Hadamard mix, damped (from Nightlight / Blasteroid)
const int RL[4] = {3001, 3769, 4513, 5333};
float* rv[4]; int ri[4] = {0};
float rlp[4] = {0};

// =====================================================================
// Knob + instrument state
// =====================================================================
volatile float knobVal = 0.5f, space = 0.3f;
volatile float fxAmt[FX_COUNT] = {0.5f, 0.45f, 0.5f, 0.45f, 0.5f, 0.4f, 0.5f, 0.6f, 0.4f};
volatile bool knobScratch = false;                  // the knob is holding a word (SHIFT or a held pad)
volatile float deckTarget = 0, lpcTarget = 0;

Step seq[16];
volatile int mode = M_DJ, curStep = 0, curWord = 1, curTalk = 0, fx = FX_CLEAN, bpm = 96, page = 0;
volatile int scStyle = SC_BABY, grooveIdx = 1, progIdx = 1, progPos = 0;   // POP chords (C G Am F): bright and happy
volatile bool rec = false, playing = false, stutterOn = false, reverseOn = false, tapeOn = false, underOn = false;
volatile bool xtalk = false, revFreeze = false, freezeHeld = false, talkFreezeHeld = false, djBack = false;
volatile bool holdScratch = false;                  // a DJ pad is held: tempo-synced scratch
volatile int holdScratchPad = -1;
volatile bool eggReq = false, replayReq = false;    // MODE + SHIFT ~1 s = the film with credits; MODE menu REPLAY = without
volatile bool vjView = true, viewReq = false;       // SEQ + SHIFT ~0.6 s = VJ view <-> classic HUD
volatile int lastSrc = SRC_SAM;
volatile float tapeRate = 1, stepFrac = 0;
volatile uint16_t rollBits = 0;
char msg[28] = ""; volatile uint32_t msgAt = 0;
void flash(const char* m) { strncpy(msg, m, sizeof(msg) - 1); msgAt = millis(); }
// knob readouts don't pop up in the middle (a glitchy pot would keep covering the picture): a small label top-left
// and a level bar down the right edge instead
char knobLbl[28] = ""; volatile float knobLvl = 0; volatile uint32_t knobAt = 0;
void knobShow(const char* t, float lvl) { strncpy(knobLbl, t, sizeof knobLbl - 1); knobLvl = constrain(lvl, 0.0f, 1.0f); knobAt = millis(); }

// the turntable deck
struct Deck {
  int word = -1;
  float pos = 0, base = 0, freezePos = 0, spin = 0;
  bool on = false, rev = false;
  uint8_t how = DK_PLAY, style = SC_BABY;
  int autoSteps = 0;                                 // >0: sequencer-triggered scratch, counts down; 0 = while held
} deck;
volatile float deckVis = 0, outPeak = 0;
volatile float scopeBuf[128];
Grain grains[12];
volatile float grainVis[12];

bool wordOk(int w) { return w >= 0 && w < NBANKW && bank[w].len > 0 && (w >= NAME_WORD ? nameReady : !bankBusy); }
void deckPlay(int w, bool rev) {
  if (!wordOk(w)) return;
  deck.word = w; deck.rev = rev; deck.how = DK_PLAY;
  deck.pos = rev ? bank[w].len - 2 : 0;
  deck.on = true; lastSrc = SRC_SAM;
  trigWord = w; trigSrc = SRC_SAM; wordTrig++;
}
void deckAuto(int w, int steps, int style) {         // tempo-synced turntable scratch
  if (!wordOk(w)) return;
  deck.word = w; deck.how = DK_AUTO; deck.autoSteps = steps; deck.style = style; deck.base = 0; deck.pos = 0;
  deck.on = true; lastSrc = SRC_SAM;
  trigWord = w; trigSrc = SRC_SAM; wordTrig++;
}
void deckFreeze(int w, float at) {                   // hold a word: grains keep reading around one spot
  if (!wordOk(w)) return;
  deck.word = w; deck.how = DK_FREEZE; deck.on = true;
  deck.freezePos = constrain(at, 0.0f, (float)bank[w].len - 2); lastSrc = SRC_SAM;
}
void deckKnob(int w) {                               // the hand is on the record
  if (!wordOk(w)) return;
  deck.word = w; deck.how = DK_KNOB; deck.on = true; deck.pos = deckTarget; lastSrc = SRC_SAM;
}

// ---- the name cut routine: 2 bars of 16ths, one cue per hit; plays in the intro and from MODE pad 11 ----
enum { CA_PLAY, CA_REV, CA_SCR, CA_SPIN, CA_TALK };
struct Cue { uint8_t step, act; int8_t word; uint8_t arg; };
const Cue ROUTINE[] = {
  {0, CA_PLAY, NAME_WORD, 0},                                         // CYBERSKRATCH
  {4, CA_PLAY, NAME_A, 0}, {5, CA_PLAY, NAME_A, 0}, {6, CA_PLAY, NAME_WORD, 0},   // CY-CY-CYBERSKRATCH (CYBER restarts)
  {8, CA_SCR, NAME_A, SC_TRANS},                                      // transformer on CYBER
  {12, CA_SCR, NAME_B, SC_BABY},                                      // baby scratch on SKRATCH
  {16, CA_REV, NAME_WORD, 0},                                         // backwards
  {20, CA_SCR, NAME_B, SC_CHIRP},                                     // chirps
  {24, CA_SCR, NAME_A, SC_FLARE},                                     // flare
  {28, CA_SPIN, NAME_B, 0},                                           // backspin (the beat drops out)
  {32, CA_TALK, 0, 0},                                                // crash + Talkie "READY"
};
#define ROUTINE_LEN 33
uint16_t routineDrums(int r) {                       // boom bap, hats in bar 2, silence for the backspin, crash
  static const char* K = "x.......x.x.....x.......x.x.....x";
  static const char* S = "....x.......x.......x............";
  static const char* H = "................x.x.x.x.x.x......";
  uint16_t d = 0;
  if (K[r] == 'x') d |= 1 << D_KICK;
  if (S[r] == 'x') d |= 1 << D_SNARE;
  if (H[r] == 'x') d |= 1 << D_HAT;
  if (r == 32) d |= 1 << D_CRASH;
  return d;
}
volatile bool routineReq = false, routineOn = false;
volatile int routineStep = -1, routineStepVis = -1;
volatile float gateVis = 1;
volatile bool zapReq = false;                        // the intro's laser asks for a ZAP
void runCue(int r) {
  for (auto& c : ROUTINE) {
    if (c.step != r) continue;
    switch (c.act) {
      case CA_PLAY: deckPlay(c.word, false); break;
      case CA_REV: deckPlay(c.word, true); break;
      case CA_SCR: deckAuto(c.word, 4, c.arg); break;
      case CA_SPIN:
        if (wordOk(c.word)) { deck.word = c.word; deck.how = DK_SPIN; deck.pos = bank[c.word].len - 2; deck.spin = 3.0f; deck.on = true; }
        break;
      default:
        if (lpcFrames && talk[0][0].n) { lpc.bank = 0; lpc.word = 0; lpc.rev = false; lpc.scratch = false; lpc.fpos = 0; lpc.loaded = -1; lpc.on = true; }
        break;
    }
  }
}

int okWord(int w) { for (int k = 0; k < NWORDS; k++) { int c = (w + k) % NWORDS; if (bank[c].len) return c; } return w; }
int randomWord() { for (int k = 0; k < 40; k++) { int c = random(NWORDS); if (bank[c].len) return c; } return curWord; }

int quantStep() { return (curStep + (stepFrac > 0.5f ? 1 : 0)) & 15; }
void recVoice(uint8_t src, int w, uint8_t flags) {
  if (!rec || !playing) return;
  int q = quantStep();
  seq[q].word = w; seq[q].src = src; seq[q].flags = flags;
  if (q != curStep) seq[q].skip = true;              // we just played it live
}
void recDrum(int d) {
  if (!rec || !playing) return;
  int q = quantStep();
  seq[q].drum |= 1 << d;
  if (q != curStep) seq[q].skip = true;
}
void clearVoices() { for (auto& s : seq) { s.word = -1; s.flags = 0; } }
void clearDrums() { for (auto& s : seq) s.drum = 0; }
void clearAll() { for (auto& s : seq) s = {-1, 0, 0, 0, false}; }
void mutate() { for (auto& s : seq) if (s.word >= 0) { if (s.src == SRC_SAM) s.word = randomWord(); else s.word = random(12); } }
void autoPattern() {                                 // a random beat and a couple of words to cut up
  clearAll();
  static const uint8_t K[4][16] = {
    {1,0,0,0, 0,0,0,0, 1,0,1,0, 0,0,0,0}, {1,0,0,1, 0,0,1,0, 0,0,1,0, 0,0,0,0},
    {1,0,0,0, 0,0,0,1, 0,0,1,0, 0,1,0,0}, {1,0,1,0, 0,0,0,0, 1,0,0,0, 0,0,1,0}};
  int k = random(4);
  for (int i = 0; i < 16; i++) {
    if (K[k][i]) seq[i].drum |= 1 << D_KICK;
    if (i == 4 || i == 12) seq[i].drum |= 1 << (random(3) ? D_SNARE : D_CLAP);
    if ((i & 1) == 0 || random(5) == 0) seq[i].drum |= 1 << D_HAT;
  }
  if (random(3) == 0) seq[14].drum |= 1 << D_OPEN;
  bool talky = mode == M_TALK;
  int a = talky ? random(12) : randomWord(), b = talky ? random(12) : randomWord();
  int hits = 3 + random(3);
  for (int h = 0; h < hits; h++) {
    int st = random(16);
    seq[st].word = random(3) ? a : b;
    seq[st].src = talky ? SRC_TALK : SRC_SAM;
    seq[st].flags = (!talky && random(4) == 0) ? SF_SCR : (random(6) == 0 ? SF_REV : 0);
  }
  grooveIdx = 0;
  flash("AUTO");
}
void demoPattern() {                                 // what the handheld Robo DJ booted with
  clearAll();
  seq[0].word = 1; seq[6].word = 1; seq[6].flags = SF_SCR; seq[8].word = 4; seq[12].word = 2;
  grooveIdx = 1;
}

// =====================================================================
// Keypad (scanned inside the audio task every 5.8 ms: low latency) + knob
// =====================================================================
ModKey mods[4];
bool padDown[12], padLong[12];
int8_t padMenu[12];       // which modifier was held when this pad went down (-1 = none)
int8_t padMode[12];       // mode when the pad went down (holds release by the mode they started in)
bool padKnob[12];         // the knob was turned while this pad was held
uint32_t padT[12];
uint16_t keysStable = 0, keysRaw = 0;
volatile int heldModVis = -1;
volatile uint16_t keysVis = 0;

uint16_t scanKeys() {
  uint16_t bits = 0;
  for (int r = 0; r < 4; r++) {
    pinMode(ROW_PINS[r], OUTPUT);
    digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(20);
    for (int c = 0; c < 4; c++) if (!digitalRead(COL_PINS[c])) bits |= 1 << (r * 4 + c);
    pinMode(ROW_PINS[r], INPUT);
  }
  return bits;
}

int heldMod() {
  int best = -1; uint32_t t = 0;
  for (int m = 0; m < 4; m++) if (mods[m].down && (best < 0 || mods[m].tDown > t)) { best = m; t = mods[m].tDown; }
  return best;
}
void setMode(int m) { mode = m; flash(MODE_NAMES[m]); }
void vocChord(int i);
int chordRoot() { return PROGS[progIdx].root[progPos & 3]; }

void stopScratch() {                                 // let go of the record
  knobScratch = false;
  if (deck.how == DK_KNOB) deck.on = false;
  if (lpc.scratch) { lpc.scratch = false; lpc.on = false; }
}

void menuAction(int m, int p, bool down) {
  char t[28];
  // UNDERWATER and INFINITE live on SHIFT pads 10 / 11 in every mode
  if (m == K_SHIFT && p == 10) { underOn = down; if (down) flash("UNDERWATER"); return; }
  if (m == K_SHIFT && p == 11) { revFreeze = down; if (down) flash("INFINITE"); return; }
  switch (m) {
    case K_MODE:
      if (!down) return;
      if (p < 3) setMode(p);
      else if (p == 3) { page ^= 1; flash(page ? "WORDS PAGE B" : "WORDS PAGE A"); }
      else if (p == 4) { volIdx = max(0, volIdx - 1); snprintf(t, sizeof t, "VOLUME %d/6", volIdx + 1); flash(t); }
      else if (p == 5) { volIdx = min(5, volIdx + 1); snprintf(t, sizeof t, "VOLUME %d/6", volIdx + 1); flash(t); }
      else if (p == 6) { talkBank = (talkBank + 1) % NBANKS; snprintf(t, sizeof t, "BANK %s", BANK_NAMES[talkBank]); flash(t); }
      else if (p == 7) { progIdx = (progIdx + 1) % NPROGS; vocChord(progPos); snprintf(t, sizeof t, "PROG %s", PROGS[progIdx].name); flash(t); }
      else if (p == 8) voiceIdx = (voiceIdx + NVOICES - 1) % NVOICES;   // loop() sees the change and re-renders
      else if (p == 9) voiceIdx = (voiceIdx + 1) % NVOICES;
      else if (p == 10) replayReq = true;
      else { routineReq = true; flash("CYBERSKRATCH"); }
      break;
    case K_FX:
      if (p < FX_COUNT) { if (down) { fx = p; flash(FX_NAMES[p]); } }
      else if (p == 9) stutterOn = down;
      else if (p == 10) reverseOn = down;
      else tapeOn = down;
      break;
    case K_SEQ:
      if (!down) return;
      switch (p) {
        case 0: rec = !rec; flash(rec ? "REC" : "rec off"); if (rec && !playing) playing = true; break;
        case 1: playing = !playing; flash(playing ? "PLAY" : "STOP"); break;
        case 2: grooveIdx = (grooveIdx + 1) % NGROOVES; snprintf(t, sizeof t, "GROOVE %s", GROOVES[grooveIdx].name); flash(t); break;
        case 3: autoPattern(); if (!playing) playing = true; break;
        case 4: bpm = max(60, bpm - 4); snprintf(t, sizeof t, "%d BPM", bpm); flash(t); break;
        case 5: bpm = min(160, bpm + 4); snprintf(t, sizeof t, "%d BPM", bpm); flash(t); break;
        case 6: mutate(); flash("MUTATE"); break;
        case 7: xtalk = !xtalk; flash(xtalk ? "CROSSTALK" : "crosstalk off"); if (xtalk && !playing) playing = true; break;
        case 8: clearVoices(); flash("voices cleared"); break;
        case 9: clearDrums(); flash("drums cleared"); break;
        case 10: clearAll(); grooveIdx = 0; xtalk = false; flash("all cleared"); break;
        default: deck.on = false; lpc.on = false; for (auto& g : grains) g.on = false; rollBits = 0; playing = false; flash("SILENCE"); break;
      }
      break;
    case K_SHIFT:
      if (mode == M_DJ) {
        if (p == 6) {                                // FREEZE while held, where the word is right now
          freezeHeld = down;
          if (down) deckFreeze(curWord, deck.on && deck.word == curWord && deck.how == DK_PLAY ? deck.pos : bank[curWord].len * 0.4f);
          else if (deck.how == DK_FREEZE) deck.on = false;
          return;
        }
        if (!down) return;
        if (p < SC_COUNT) { scStyle = p; snprintf(t, sizeof t, "SCRATCH %s", SC_NAMES[p]); flash(t); deckAuto(curWord, 4, p); }
        else if (p == 7) { djBack = !djBack; flash(djBack ? "BACKWARDS" : "forwards"); }
        else if (p == 8) { curWord = randomWord(); deckPlay(curWord, djBack); recVoice(SRC_SAM, curWord, djBack ? SF_REV : 0); }
        else if (p == 9) { page ^= 1; flash(page ? "WORDS PAGE B" : "WORDS PAGE A"); }
      } else if (mode == M_TALK) {
        if (p == 2) { lpFreeze = down; return; }
        if (!down) return;
        switch (p) {
          case 0: lpWhisper = !lpWhisper; flash(lpWhisper ? "WHISPER" : "whisper off"); break;
          case 1: lpMono = !lpMono; monoFreq = midiHz(60 + chordRoot()); flash(lpMono ? "ROBOT MONO" : "mono off"); break;
          case 3: lpStretch = !lpStretch; flash(lpStretch ? "STRETCH" : "stretch off"); break;
          case 4: lpPitch = max(0.25f, lpPitch * 0.84f); flash("PITCH -"); break;
          case 5: lpPitch = min(4.0f, lpPitch * 1.19f); flash("PITCH +"); break;
          case 6: lpGlitch = !lpGlitch; flash(lpGlitch ? "GLITCH" : "glitch off"); break;
          case 7: lpRev = !lpRev; flash(lpRev ? "BACKWARDS" : "forwards"); break;
          case 8: lpFormant = lpFormant < 0.9f ? 1.0f : lpFormant < 1.1f ? 1.4f : 0.7f;
                  snprintf(t, sizeof t, "FORMANT %s", lpFormant < 0.9f ? "LOW" : lpFormant < 1.1f ? "NORMAL" : "HIGH"); flash(t); break;
          default: lpWhisper = false; lpMono = false; lpStretch = false; lpGlitch = false; lpRev = false; lpPitch = 1; lpFormant = 1; flash("BENDS RESET"); break;
        }
      } else {
        if (!down) return;
        drumMute ^= 1 << p;
        snprintf(t, sizeof t, "%s %s", PERC[p].name, drumMute & (1 << p) ? "MUTED" : "on"); flash(t);
      }
      break;
  }
}

void tapAction(int m) {
  if (m == K_MODE) setMode((mode + 1) % M_COUNT);
  else if (m == K_FX) { fx = (fx + 1) % FX_COUNT; flash(FX_NAMES[fx]); }
  else if (m == K_SEQ) { rec = !rec; flash(rec ? "REC" : "rec off"); if (rec && !playing) playing = true; }
  else { playing = !playing; flash(playing ? "PLAY" : "STOP"); }
}

void padPress(int p, uint32_t now) {
  padDown[p] = true; padLong[p] = false; padKnob[p] = false; padT[p] = now;
  int m = heldMod();
  padMenu[p] = m;
  if (m >= 0) { mods[m].used = true; menuAction(m, p, true); return; }
  padMode[p] = mode;
  switch (mode) {
    case M_DJ: {
      int w = page * 12 + p;
      curWord = w;
      bool rev = reverseOn ^ djBack;
      deckPlay(w, rev); recVoice(SRC_SAM, w, rev ? SF_REV : 0);
      break;
    }
    case M_TALK: curTalk = p; talkPlay(p, reverseOn); recVoice(SRC_TALK, p, reverseOn ? SF_REV : 0); lastSrc = SRC_TALK; break;
    default:
      if (!(drumMute & (1 << p))) hitDrums(1 << p);
      recDrum(p);
      break;
  }
}

void padHold(int p) {
  padLong[p] = true;
  if (padMenu[p] >= 0 || padKnob[p]) return;
  switch (padMode[p]) {
    case M_DJ:                                       // tempo-synced turntable scratch while held
      holdScratch = true; holdScratchPad = p;
      deckAuto(page * 12 + p, 0, scStyle);
      break;
    case M_TALK:                                     // freeze the mouth, humming the chord root
      talkFreezeHeld = true; lpFreeze = true; lpMono = true; monoFreq = midiHz(60 + chordRoot());
      if (!(lpc.on && lpc.word == p)) talkPlay(p, false);
      break;
    case M_BEAT: rollBits |= 1 << p; break;
    default: break;
  }
}

void padRelease(int p, uint32_t now) {
  padDown[p] = false;
  if (padMenu[p] >= 0) { menuAction(padMenu[p], p, false); return; }
  if (padKnob[p]) { if (knobScratch && !mods[K_SHIFT].down) stopScratch(); padKnob[p] = false; }
  if (!padLong[p]) return;
  switch (padMode[p]) {
    case M_DJ: if (holdScratch && holdScratchPad == p) { holdScratch = false; holdScratchPad = -1; if (deck.how == DK_AUTO) deck.on = false; } break;
    case M_TALK: if (talkFreezeHeld) { talkFreezeHeld = false; lpFreeze = false; lpMono = false; lpc.on = false; } break;
    case M_BEAT: rollBits &= ~(1 << p); break;
    default: break;
  }
}

// a held, plain pad (no modifier) - the knob acts on it
int heldPad() {
  int best = -1; uint32_t t = 0;
  for (int p = 0; p < 12; p++) if (padDown[p] && padMenu[p] < 0 && (best < 0 || padT[p] > t)) { best = p; t = padT[p]; }
  return best;
}

// grab a word with the knob: SAM word w, or Talkie word w (current bank)
void knobGrab(bool talkie, int w, float pv) {
  if (talkie) {
    const TalkWord& tw = talk[talkBank][w];
    lpcTarget = pv * (tw.n > 1 ? tw.n - 1 : 0);
    if (!knobScratch || !lpc.scratch || lpc.word != w) {
      if (deck.how == DK_KNOB) deck.on = false;
      lpc.bank = talkBank; lpc.word = w; lpc.scratch = true; lpc.fpos = lpcTarget; lpc.loaded = -1; lpc.on = tw.n > 0; lastSrc = SRC_TALK;
    }
  } else {
    deckTarget = pv * max(0.0f, (float)bank[w].len - 2);
    if (!knobScratch || deck.how != DK_KNOB || deck.word != w || !deck.on) {
      if (lpc.scratch) { lpc.scratch = false; lpc.on = false; }
      holdScratch = false;
      deckKnob(w);
    }
  }
  knobScratch = true;
}

// the knob: alone = FX amount, SHIFT / held pad = scratch, FX = space, SEQ = tempo, MODE = volume
void knobTick(uint32_t now) {
  static float potS = -1, potMax = 2700, ref = -1, volAcc = 0;
  int raw = 0;                                       // 16 reads averaged: knocks the ADC noise down ~4x
  for (int i = 0; i < 16; i++) raw += analogRead(PIN_POT);
  raw >>= 4;
  if (potS < 0) potS = raw;
  potS += (raw - potS) * 0.1f;
  if (potS > potMax) potMax = min(4095.0f, potS);
  float pv = constrain(potS / potMax, 0.0f, 1.0f);
  knobVal = pv;
  if (ref < 0) { ref = pv; return; }
  bool scratching = knobScratch;
  // the knob locks when it rests: only a real turn (4.5%) wakes it, then it follows finely (also pulling back a bit)
  // until it has been still for ~0.45 s and locks again. A hand on the record always follows finer moves.
  static bool live = false;
  static uint32_t lastMove = 0;
  float thr = scratching ? 0.01f : live ? 0.006f : 0.045f;
  if (fabsf(pv - ref) < thr) { if (live && now - lastMove > 450) live = false; return; }
  live = true; lastMove = now;
  float delta = pv - ref;
  ref = pv;
  char t[28];
  int hp = heldPad();
  if (hp >= 0 && padMode[hp] == mode && mode != M_BEAT) {         // hand on the held pad's word
    padKnob[hp] = true;
    if (holdScratch) { holdScratch = false; holdScratchPad = -1; }
    if (mode == M_DJ) knobGrab(false, page * 12 + hp, pv);
    else {
      if (talkFreezeHeld) { talkFreezeHeld = false; lpFreeze = false; lpMono = false; }
      knobGrab(true, hp, pv);
    }
  } else if (hp >= 0 && mode == M_BEAT) {                          // tune the held drum
    padKnob[hp] = true;
    drumTune[hp] = 0.5f * exp2f(2 * pv);
    snprintf(t, sizeof t, "%s TUNE x%.2f", PERC[hp].name, (float)drumTune[hp]); knobShow(t, pv);
  } else if (mods[K_SHIFT].down) {
    mods[K_SHIFT].used = true;
    if (lastSrc == SRC_TALK || mode == M_TALK) knobGrab(true, curTalk, pv);
    else knobGrab(false, curWord, pv);
  } else if (mods[K_FX].down) {
    mods[K_FX].used = true;
    space = pv; snprintf(t, sizeof t, "SPACE %d%%", (int)(pv * 100)); knobShow(t, pv);
  } else if (mods[K_SEQ].down) {
    mods[K_SEQ].used = true;
    bpm = constrain(bpm + (int)roundf(delta * 120), 60, 160); snprintf(t, sizeof t, "%d BPM", bpm); knobShow(t, (bpm - 60) / 100.0f);
  } else if (mods[K_MODE].down) {
    mods[K_MODE].used = true;
    volAcc += delta;
    if (fabsf(volAcc) > 0.1f) { volIdx = constrain(volIdx + (volAcc > 0 ? 1 : -1), 0, 5); volAcc = 0; }
    snprintf(t, sizeof t, "VOLUME %d/6", volIdx + 1); knobShow(t, volIdx / 5.0f);
  } else if (!scratching) {
    fxAmt[fx] = pv; snprintf(t, sizeof t, "%s %s %d%%", FX_NAMES[fx], FX_KNOB[fx], (int)(pv * 100)); knobShow(t, pv);
  }
}

void inputTick() {
  uint32_t now = millis();
  uint16_t raw = scanKeys();
  // asymmetric debounce: down after 2 scans (~12 ms), up only after 8 scans (~46 ms) - rides over flaky contacts
  static uint8_t upCnt[16], dnCnt[16];
  uint16_t stable = keysStable;
  for (int k = 0; k < 16; k++) {
    bool r = raw & (1 << k);
    if (r) { upCnt[k] = 0; if (dnCnt[k] < 255) dnCnt[k]++; if (dnCnt[k] >= 2) stable |= 1 << k; }
    else { dnCnt[k] = 0; if (upCnt[k] < 255) upCnt[k]++; if (upCnt[k] >= 8) stable &= ~(1 << k); }
  }
  keysRaw = raw;
  uint16_t pressed = stable & ~keysStable, released = keysStable & ~stable;
  keysStable = stable;
  keysVis = stable;
  for (int r = 0; r < 4; r++) {
    int bit = 1 << (r * 4);
    if (pressed & bit) { mods[r].down = true; mods[r].used = false; mods[r].tDown = now; }
    if (released & bit) {
      mods[r].down = false;
      if (r == K_SHIFT && knobScratch && heldPad() < 0) stopScratch();
      if (!mods[r].used && now - mods[r].tDown < 500) tapAction(r);
    }
    for (int c = 1; c < 4; c++) {
      int b = 1 << (r * 4 + c), p = r * 3 + c - 1;
      if (pressed & b) padPress(p, now);
      if (released & b) padRelease(p, now);
    }
  }
  for (int p = 0; p < 12; p++) if (padDown[p] && !padLong[p] && now - padT[p] > 300) padHold(p);
  // REC while a DJ pad scratches: every step it covers becomes a scratch step
  if (holdScratch && rec && playing && holdScratchPad >= 0) {
    int q = quantStep(); seq[q].word = page * 12 + holdScratchPad; seq[q].src = SRC_SAM; seq[q].flags = SF_SCR;
  }
  static bool viewFired = false;                     // SEQ + SHIFT held ~0.6 s = VJ view <-> classic HUD
  if (mods[K_SEQ].down && mods[K_SHIFT].down) {
    if (!viewFired && now - max(mods[K_SEQ].tDown, mods[K_SHIFT].tDown) > 600) {
      viewFired = true; viewReq = true; mods[K_SEQ].used = mods[K_SHIFT].used = true;
    }
  } else viewFired = false;
  static bool eggFired = false;
  if (mods[K_MODE].down && mods[K_SHIFT].down) {
    if (!eggFired && now - max(mods[K_MODE].tDown, mods[K_SHIFT].tDown) > 900) {
      eggFired = true; eggReq = true; mods[K_MODE].used = mods[K_SHIFT].used = true;
    }
  } else eggFired = false;
  int m = heldMod();
  heldModVis = (m >= 0 && now - mods[m].tDown > 180 && !knobScratch) ? m : -1;
  knobTick(now);
}

// =====================================================================
// Audio
// =====================================================================
// scratch hand position (0..1) and crossfader gate for phase 0..1 of one stroke cycle
static inline void scratchShape(int style, float ph, float& x, float& g) {
  switch (style) {
    case SC_CHIRP: x = 0.5f - 0.5f * cosf(2 * PI * ph); g = ph < 0.45f ? 1 : 0; break;
    case SC_TRANS: { float p2 = ph * 0.5f; x = 0.5f - 0.5f * cosf(2 * PI * p2); g = ((int)(ph * 8)) & 1 ? 0 : 1; break; }
    case SC_TEAR:
      if (ph < 0.3f) x = ph / 0.3f;
      else if (ph < 0.55f) x = 1 - (ph - 0.3f) / 0.25f * 0.5f;
      else if (ph < 0.65f) x = 0.5f;
      else x = 0.5f - (ph - 0.65f) / 0.35f * 0.5f;
      g = 1; break;
    case SC_FLARE: x = 0.5f - 0.5f * cosf(2 * PI * ph); g = (fabsf(ph - 0.25f) < 0.04f || fabsf(ph - 0.75f) < 0.04f) ? 0 : 1; break;
    case SC_CRAB: x = 0.5f - 0.5f * cosf(2 * PI * ph); g = ph < 0.5f ? (((int)(ph * 16)) & 1 ? 0 : 1) : 0.0f; break;   // four finger taps going out
    default: x = 0.5f - 0.5f * cosf(2 * PI * ph); g = 1; break;
  }
}

// ---- vocoder (from Robo Choir): the voice shapes the chord across 16 bands ----
#define NB 16
struct VOsc { float ph = 0, ph2 = 0, f = 110, target = 110; bool on = false; };
VOsc vosc[5];
float coefM[NB][5], coefC[NB][5], wM[NB][2], wC[NB][2], bandEnv[NB];
float vBuf[BLOCK], dBuf[BLOCK], carBuf[BLOCK], noiseBuf[BLOCK], tmpM[BLOCK], tmpC[BLOCK], tmpC2[BLOCK], vocOut[BLOCK], keepBuf[BLOCK];
float vocAgc = 6;

void setupVocoder() {
  for (int b = 0; b < NB; b++) {
    float f = 150.0f * powf(5500.0f / 150.0f, b / (float)(NB - 1));
    dsps_biquad_gen_bpf_f32(coefM[b], f / FS, 5.0f);
    dsps_biquad_gen_bpf_f32(coefC[b], f / FS, 5.0f);
  }
}

void vocChord(int i) {
  const Prog& p = PROGS[progIdx];
  int root = p.root[i & 3], q = p.q[i & 3];
  for (int k = 0; k < 4; k++) {
    int iv = QI[q][k];
    if (iv < 0) { vosc[k].on = false; continue; }
    int n = 48 + root + iv;
    while (n < 52) n += 12;
    while (n > 67) n -= 12;
    vosc[k].target = midiHz(n);
    if (!vosc[k].on) vosc[k].f = vosc[k].target;
    vosc[k].on = true;
  }
  int b = 36 + root; if (b > 43) b -= 12;
  vosc[4].target = midiHz(b);
  if (!vosc[4].on) vosc[4].f = vosc[4].target;
  vosc[4].on = true;
}

void chordLabel(char* out, size_t n) {
  const Prog& p = PROGS[progIdx];
  snprintf(out, n, "%s%s", NOTE[p.root[progPos & 3]], QN[p.q[progPos & 3]]);
}

static inline float blep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1; }
  if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
  return 0;
}

// replaces vBuf with the vocoded voice
void vocodeBlock() {
  for (int n = 0; n < BLOCK; n++) {
    float c = 0;
    for (int k = 0; k < 5; k++) {
      VOsc& o = vosc[k];
      if (!o.on) continue;
      o.f += (o.target - o.f) * 0.0015f;
      float dt = o.f * INV_FS, dt2 = dt * 1.007f;
      o.ph += dt; if (o.ph >= 1) o.ph -= 1;
      o.ph2 += dt2; if (o.ph2 >= 1) o.ph2 -= 1;
      c += (2 * o.ph - 1 - blep(o.ph, dt) + 2 * o.ph2 - 1 - blep(o.ph2, dt2)) * (k == 4 ? 0.8f : 0.5f);
    }
    carBuf[n] = softclip(c * 0.7f) * 1.6f;            // driven carrier: grittier robot
    noiseBuf[n] = nz();
  }
  memset(vocOut, 0, sizeof vocOut);
  for (int b = 0; b < NB; b++) {
    dsps_biquad_f32(vBuf, tmpM, BLOCK, coefM[b], wM[b]);
    if (b >= NB - 3) for (int n = 0; n < BLOCK; n++) tmpC[n] = carBuf[n] * 0.4f + noiseBuf[n] * 1.2f;
    else memcpy(tmpC, carBuf, sizeof tmpC);
    dsps_biquad_f32(tmpC, tmpC2, BLOCK, coefC[b], wC[b]);
    float env = bandEnv[b];
    for (int n = 0; n < BLOCK; n++) {
      float a = fabsf(tmpM[n]);
      env += (a - env) * (a > env ? 0.02f : 0.0025f);
      vocOut[n] += tmpC2[n] * env;
    }
    bandEnv[b] = env;
  }
  float pk = 0.0001f;
  for (int n = 0; n < BLOCK; n++) pk = max(pk, fabsf(vocOut[n]));
  float want = min(40.0f, 0.55f / pk);
  if (want < vocAgc) vocAgc += (want - vocAgc) * 0.3f;
  else if (pk > 0.002f) vocAgc += (want - vocAgc) * 0.01f;
  for (int n = 0; n < BLOCK; n++) vBuf[n] = vocOut[n] * vocAgc;
}

// ---- grains: 0 = GRAIN (mixed pitches), 1 = FREEZE (tight, near unison), 2 = SWARM (detuned robot choir) ----
void spawnGrain(int word, float at, int type) {
  if (word < 0 || bank[word].len == 0) return;
  for (auto& g : grains) if (!g.on) {
    static const float GP[6] = {1.0f, 1.0f, 0.5f, 1.5f, 2.0f, 0.75f};
    float r = random(1000) / 1000.0f;
    g.word = word;
    if (type == 2) {
      g.len = FS * (0.03f + 0.02f * r);
      g.pos = at + (random(200) - 100) / 100.0f * FS * 0.012f;
      g.inc = 1.0f + (random(200) - 100) / 100.0f * (0.03f + 0.25f * fxAmt[FX_SWARM]);
      g.amp = 0.55f;
    } else {
      g.len = FS * (0.06f + 0.06f * r);
      g.pos = at + (random(200) - 100) / 100.0f * FS * (type == 1 ? 0.02f : 0.03f);
      g.inc = type == 0 ? GP[random(6)] : 1.0f + (random(200) - 100) / 100.0f * 0.006f;
      g.amp = type == 0 ? 0.75f : 0.55f;
    }
    if (deck.rev && type != 1) g.inc = -g.inc;
    g.pos = constrain(g.pos, 0.0f, (float)bank[word].len - 2);
    g.age = 0; g.on = true;
    return;
  }
}
static inline float grainSum() {
  float v = 0;
  for (auto& g : grains) {
    if (!g.on) continue;
    float w = fsin(g.age / g.len * 0.5f);              // sine window, 0..1..0
    v += wordAt(g.word, g.pos) * w * w * g.amp;
    g.pos += g.inc * tapeRate;
    if (++g.age >= g.len) g.on = false;
  }
  return v;
}

static inline float svfBP(SVF& s, float x, float f, float q) { s.lo += f * s.bp; float hi = x - s.lo - q * s.bp; s.bp += f * hi; return s.bp; }

void setupAmp() {
  amp.setPins(AMP_BCLK, AMP_LRC, AMP_DIN);
  bool ok = amp.begin(I2S_MODE_STD, FS, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  logf("I2S %s\n", ok ? "OK" : "FAILED");
}
volatile uint32_t audioBlocks = 0;
TaskHandle_t audioHandle = nullptr;
volatile float masterFade = 0;

void audioTask(void*) {
  static int16_t out[BLOCK * 2];
  float stepPos = 0, underLP = 0, under = 0, deckLP = 0, gateSm = 1, compEnv = 0, subLP = 0, scrGate = 0, lastPos = 0;
  float deepLP = 0, crushHold = 0, xtTurn = 0;
  SVF radS;
  float stutterBase = -1, lpcStutter = -1;
  int grainTimer = 0, crushCnt = 0;
  for (;;) {
    inputTick();
    if (zapReq) { zapReq = false; hitDrums(1 << D_ZAP); }
    if (routineReq && !routineOn && nameReady) { routineStep = -1; routineOn = true; routineReq = false; }
    tapeRate += ((tapeOn ? 0.0f : 1.0f) - tapeRate) * (tapeOn ? 0.03f : 0.08f);
    under += ((underOn ? 1.0f : 0.0f) - under) * 0.05f;
    masterFade = min(1.0f, masterFade + BLOCK * INV_FS / 1.5f);
    float stepLen = FS * 60.0f / bpm / 4.0f;
    float master = VOLS[volIdx] * masterFade;
    bool busy = bankBusy;
    if (busy && !(deck.word >= NAME_WORD && nameReady)) { deck.on = false; for (auto& g : grains) g.on = false; }
    if (!stutterOn) { stutterBase = -1; lpcStutter = -1; }
    int f = fx;
    float pitchK = f == FX_CLEAN ? 0.5f + fxAmt[FX_CLEAN] : f == FX_DEEP ? 0.75f - 0.4f * fxAmt[FX_DEEP] : 1.0f;
    float lpcInc = 8000.0f * lpFormant * tapeRate * INV_FS * (f == FX_DEEP ? pitchK : 1.0f);
    int crushN = 1 + (int)(fxAmt[FX_CRUSH] * 7);
    float crushLv = 32 - 28 * fxAmt[FX_CRUSH];
    float sp = space;
    float revFb = revFreeze ? 0.999f : 0.78f + 0.19f * sp, revDamp = 0.55f - 0.3f * sp, revIn = revFreeze ? 0.0f : 1.0f;
    float sendVox = 0.15f + 0.65f * sp, sendDrum = 0.08f + 0.35f * sp;
    float wet = 0.3f + 0.5f * sp, dry = 1.0f - 0.3f * sp;
    float echoFb = 0.25f + 0.65f * fxAmt[FX_ECHO];
    float radCtr = 2 * sinf(PI * (300 + 3200 * fxAmt[FX_RADIO]) * INV_FS);
    uint16_t mute = drumMute;
    float spinDec = expf(logf(0.0067f) / (stepLen * 4));   // a backspin dies out over one beat
    float tune[12];
    for (int i = 0; i < 12; i++) tune[i] = drumTune[i];

    for (int n = 0; n < BLOCK; n++) {
      // ---- 16th-note clock (always runs; the sequencer only fires while playing) ----
      stepPos += tapeRate;
      if (stepPos >= stepLen) {
        stepPos -= stepLen;
        int s = (curStep + 1) & 15;
        curStep = s;
        Step& st = seq[s];
        uint16_t d = 0;
        if (s == 0) {
          progPos = (progPos + 1) & 3; vocChord(progPos);
          if (talkFreezeHeld || lpMono) monoFreq = midiHz(60 + chordRoot());
          if (playing && xtalk && random(10) < 7 && !busy) {      // the two mouths take turns
            xtTurn = 1 - xtTurn;
            if (xtTurn > 0.5f) { int w = randomWord(); deckPlay(w, false); curWord = w; }
            else { int w = random(12); talkPlay(w, false); curTalk = w; lastSrc = SRC_TALK; }
          }
        }
        if (routineOn && (routineStep >= 0 || !playing || s == 0)) {   // with the sequencer running it waits for the bar
          int r = ++routineStep;
          runCue(r);
          if (!playing) d |= routineDrums(r);
          if (r >= ROUTINE_LEN - 1) routineOn = false;
        } else if (!routineOn && routineStep >= ROUTINE_LEN - 1 && ++routineStep > ROUTINE_LEN + 1) routineStep = -1;   // crash flash: 2 steps
        routineStepVis = routineStep;
        if (playing) {
          d |= (st.skip ? 0 : st.drum) | grooveMask[grooveIdx][s];
          bool handsOff = !holdScratch && !knobScratch && !freezeHeld && !routineOn;
          if (!st.skip && !busy && st.word >= 0) {
            if (st.src == SRC_SAM && handsOff) {
              if (st.flags & SF_SCR) deckAuto(st.word, 2, scStyle);
              else deckPlay(st.word, st.flags & SF_REV);
            } else if (st.src == SRC_TALK && !talkFreezeHeld && !knobScratch) { talkPlay(st.word, st.flags & SF_REV); lastSrc = SRC_TALK; }
          }
        }
        d |= rollBits;
        if (rollBits && rec && playing) seq[s].drum |= rollBits;
        hitDrums(d & ~mute);
        st.skip = false;
        if (deck.how == DK_AUTO && deck.autoSteps > 0 && --deck.autoSteps == 0) deck.on = false;
        if (stutterOn && !busy) {                    // retrigger every 16th from where we were
          if (deck.word >= 0 && deck.how == DK_PLAY) {
            if (stutterBase < 0) stutterBase = deck.on ? deck.pos : 0;
            deck.pos = stutterBase; deck.on = true;
          }
          if (lpc.word >= 0 && !lpc.scratch) {
            if (lpcStutter < 0) lpcStutter = lpc.on ? lpc.fpos : 0;
            lpc.fpos = lpcStutter; lpc.loaded = -1; lpc.on = true;
          }
        }
      }
      stepFrac = stepPos / stepLen;

      // ---- the turntable deck (SAM) ----
      float v = 0, gTarget = 1;
      bool grainFx = f == FX_GRAIN || f == FX_SWARM;
      if (deck.on && deck.word >= 0) {
        const Word& w = bank[deck.word];
        switch (deck.how) {
          case DK_FREEZE:
            if (--grainTimer <= 0) { spawnGrain(deck.word, deck.freezePos, 1); grainTimer = FS / 70; }
            break;
          case DK_AUTO: {                            // one stroke per 8th note, the hand has a little mass
            float ph = wrap1(((curStep & 1) + stepPos / stepLen) * 0.5f);
            float x, g;
            scratchShape(deck.style, ph, x, g);
            float depth = min((float)w.len - 2, FS * 0.22f);
            deck.pos += (deck.base + x * depth - deck.pos) * 0.35f;
            gTarget = g;
            v = wordAt(deck.word, deck.pos) * 1.3f;
            break;
          }
          case DK_SPIN: {                            // backspin: shoved backwards, slowing down
            deck.pos -= deck.spin * tapeRate;
            deck.spin *= spinDec;
            gTarget = min(1.0f, deck.spin * 1.5f);
            v = wordAt(deck.word, deck.pos) * 1.3f;
            if (deck.pos < 0 || deck.spin < 0.02f) deck.on = false;
            break;
          }
          case DK_KNOB: {                            // the knob is the hand; silent when the record stands still
            lastPos = deck.pos;
            deck.pos += (deckTarget - deck.pos) * 0.004f;
            float vel = fabsf(deck.pos - lastPos);
            scrGate += (min(1.0f, vel * 3) - scrGate) * 0.01f;
            gTarget = scrGate;
            v = wordAt(deck.word, deck.pos) * 1.3f;
            break;
          }
          default:
            if (grainFx) {                           // the read pointer crawls, grains do the talking
              bool sw = f == FX_SWARM;
              deck.pos += (deck.rev ? -1 : 1) * (sw ? 0.8f : 0.33f) * tapeRate;
              if (--grainTimer <= 0) {
                spawnGrain(deck.word, deck.pos, sw ? 2 : 0);
                grainTimer = sw ? FS / 160 : (int)(FS / (20 + 120 * fxAmt[FX_GRAIN]));
              }
            } else {
              deck.pos += (deck.rev ? -1 : 1) * pitchK * tapeRate;
              if (stutterOn && stutterBase >= 0 && deck.pos > stutterBase + stepLen * 0.5f) gTarget = 0;
              v = wordAt(deck.word, deck.pos) * 1.3f;
            }
            if (deck.pos < 0 || deck.pos >= w.len - 1) deck.on = false;
            break;
        }
      }
      v += grainSum();                               // grain tails ring on
      gateSm += (gTarget - gateSm) * 0.25f;
      v *= gateSm;
      deckLP += 0.6f * (v - deckLP);                 // take the 8-bit fizz off
      v = deckLP;

      // ---- Talkie LPC (8 kHz engine, interpolated up) ----
      if (lpc.on) {
        float g = 1;
        if (lpc.scratch) {
          float prev = lpc.fpos;
          lpc.fpos += (lpcTarget - lpc.fpos) * 0.003f;
          scrGate += (min(1.0f, fabsf(lpc.fpos - prev) * 600) - scrGate) * 0.01f;
          g = scrGate;
        }
        lpc.ph += lpcInc;
        while (lpc.ph >= 1) { lpc.ph -= 1; lpc.y0 = lpc.y1; lpc.y1 = lpcTick(); }
        v += (lpc.y0 + (lpc.y1 - lpc.y0) * lpc.ph) * g * 2.5f;
      }

      // ---- voice FX ----
      if (f == FX_CRUSH) {
        if (++crushCnt >= crushN) { crushCnt = 0; crushHold = roundf(v * crushLv) / crushLv; }
        v = crushHold;
      } else if (f == FX_RADIO) {                    // bandpass on the knob, driven, with crackle
        float b = svfBP(radS, v + nz() * 0.03f, radCtr, 0.5f);
        v = softclip(b * 3.0f) * 0.6f;
      } else if (f == FX_DEEP) {
        deepLP += 0.25f * (v - deepLP); v = deepLP * 1.3f;
      }
      vBuf[n] = v;

      float dr = 0;
      for (int i = 0; i < 12; i++) dr += perc[i].s(PERC[i], tapeRate * tune[i]);
      dBuf[n] = dr * 0.7f;
    }

    if (f == FX_VOX || f == FX_CHOIR) {              // VOX = vocoder + clear voice (knob = wet), CHOIR = pure + breath
      memcpy(keepBuf, vBuf, sizeof keepBuf);
      vocodeBlock();
      float wetV = f == FX_VOX ? fxAmt[FX_VOX] : 1.0f;
      for (int n = 0; n < BLOCK; n++) {
        float vv = vBuf[n] * wetV + keepBuf[n] * (f == FX_VOX ? (1 - wetV) * 1.4f + 0.2f : 0);
        if (f == FX_CHOIR) vv += nz() * fxAmt[FX_CHOIR] * 0.3f * fabsf(vBuf[n]);
        vBuf[n] = vv;
      }
    }

    float blockPeak = 0;
    for (int n = 0; n < BLOCK; n++) {
      float v = vBuf[n], dr = dBuf[n];
      float echo = 0;
      if (dlyBuf) {
        int dl = (int)(stepLen * 3);
        if (dl >= DLY_N) dl = DLY_N - 1;
        int r = dlyW - dl; if (r < 0) r += DLY_N;
        echo = dlyBuf[r];
        dlyBuf[dlyW] = (f == FX_ECHO ? v : (f == FX_CHOIR ? v * 0.3f : 0)) + echo * echoFb;
        if (++dlyW >= DLY_N) dlyW = 0;
      }
      float vox = v * 0.9f + echo * 0.6f;
      // reverb (SPACE)
      float in = (vox * sendVox + dr * sendDrum) * revIn;
      for (int k = 0; k < 4; k++) rlp[k] += revDamp * (rv[k][ri[k]] - rlp[k]);
      float h0 = (rlp[0] + rlp[1] + rlp[2] + rlp[3]) * 0.5f, h1 = (rlp[0] - rlp[1] + rlp[2] - rlp[3]) * 0.5f;
      float h2 = (rlp[0] + rlp[1] - rlp[2] - rlp[3]) * 0.5f, h3 = (rlp[0] - rlp[1] - rlp[2] + rlp[3]) * 0.5f;
      rv[0][ri[0]] = in + revFb * h0; rv[1][ri[1]] = in + revFb * h1; rv[2][ri[2]] = in + revFb * h2; rv[3][ri[3]] = in + revFb * h3;
      for (int k = 0; k < 4; k++) if (++ri[k] >= RL[k]) ri[k] = 0;
      float rev = (rlp[0] + rlp[1] + rlp[2] + rlp[3]) * 0.25f;
      float x = (vox + dr) * dry + rev * wet * 1.4f;
      subLP += 0.0113f * (x - subLP);                // ~40 Hz highpass: sub the speaker can't play
      x -= subLP;
      underLP += (1.0f - under * 0.95f) * (x - underLP);
      float a = fabsf(underLP);                      // glue compressor
      compEnv += (a - compEnv) * (a > compEnv ? 0.015f : 0.00025f);
      float gr = compEnv > 0.5f ? powf(0.5f / compEnv, 0.667f) : 1.0f;
      float y = softclip(underLP * gr * 1.35f);
      scopeBuf[n] = y;
      if (fabsf(y) > blockPeak) blockPeak = fabsf(y);
      int16_t s = (int16_t)(y * master * 32000);
      out[2 * n] = s; out[2 * n + 1] = s;
    }
    outPeak = blockPeak;
    gateVis = gateSm;
    deckVis = deck.on ? (deck.how == DK_FREEZE ? deck.freezePos : deck.pos) : -1;
    for (int i = 0; i < 12; i++) grainVis[i] = grains[i].on && grains[i].word == deck.word ? grains[i].pos : -1;
    lpcVisFrame = lpc.on ? lpc.loaded : -1;
    amp.write((uint8_t*)out, sizeof(out));
    audioBlocks++;
  }
}

// =====================================================================
// Visuals: sci-fi HUD - SIG scope, PAD grid, mode strip, readout, SEQ lanes, status chips
// =====================================================================
void brackets(int x, int y, int w, int h) {
  const int l = 5;
  oled.drawHLine(x, y, l);             oled.drawVLine(x, y, l);
  oled.drawHLine(x + w - l, y, l);     oled.drawVLine(x + w - 1, y, l);
  oled.drawHLine(x, y + h - 1, l);     oled.drawVLine(x, y + h - l, l);
  oled.drawHLine(x + w - l, y + h - 1, l); oled.drawVLine(x + w - 1, y + h - l, l);
}
int tab(int x, int y, const char* t) {
  oled.setFont(u8g2_font_4x6_tf);
  int w = oled.getStrWidth(t) + 4;
  oled.drawBox(x, y, w, 7);
  oled.setDrawColor(0); oled.drawStr(x + 2, y + 6, t); oled.setDrawColor(1);
  return w;
}
void dottedH(int x, int y, int w) { for (int i = 0; i < w; i += 2) oled.drawPixel(x + i, y); }
const char* padName(int m, int p) {
  switch (m) {
    case M_DJ: return bank[page * 12 + p].text;
    case M_TALK: return talk[talkBank][p].name;
    default: return PERC[p].name;
  }
}
void shortName(const char* s, char* out, int n) {
  int j = 0;
  for (int i = 0; s[i] && j < n; i++) if (s[i] != ' ' && s[i] != '\'' && s[i] != '-' && s[i] != '.') out[j++] = s[i];
  out[j] = 0;
}

void drawTopBar() {
  int w = tab(0, 0, "CYBERSKRATCH");
  oled.setFont(u8g2_font_4x6_tf);
  char t[12]; snprintf(t, sizeof t, "%d", bpm);
  oled.drawStr(w + 3, 6, t);
  int bx = w + 5 + oled.getStrWidth(t);
  int beat = curStep / 4;
  for (int i = 0; i < 4; i++) { if (playing && i == beat) oled.drawBox(bx + i * 5, 1, 4, 4); else oled.drawFrame(bx + i * 5, 1, 4, 4); }
  const char* mn = MODE_NAMES[mode];
  int mw = oled.getStrWidth(mn) + 4;
  const char* vn = VOICES[voiceIdx].name;
  if (bx + 23 + oled.getStrWidth(vn) < 126 - mw) oled.drawStr(bx + 23, 6, vn);
  oled.drawFrame(128 - mw, 0, mw, 7);
  oled.drawStr(128 - mw + 2, 6, mn);
  dottedH(0, 8, 128);
}

void drawSigPane() {
  const int x = 0, y = 11, w = 54, h = 40, mid = y + 22;
  brackets(x, y, w, h);
  tab(x + 2, y + 2, "SIG");
  int py = mid;
  for (int i = 0; i < 44; i++) {
    float v = scopeBuf[i * 128 / 44];
    int yy = constrain(mid - (int)(v * 14), y + 9, y + h - 4);
    if (i) oled.drawLine(x + 3 + i, py, x + 4 + i, yy);
    py = yy;
  }
  static float lvl = 0;
  lvl = max((float)outPeak, lvl * 0.9f);
  int segs = (int)(lvl * 8 + 0.5f);
  for (int i = 0; i < 8; i++) {
    int sy = y + h - 5 - i * 4;
    if (i < segs) oled.drawBox(x + w - 5, sy, 3, 3); else oled.drawPixel(x + w - 4, sy + 1);
  }
}
void drawPadPane() {                                 // the 12 pads as they sit on the keypad
  const int x0 = 56, y0 = 11, cw = 24, ch = 10;
  uint16_t k = keysVis;
  oled.setFont(u8g2_font_4x6_tf);
  for (int p = 0; p < 12; p++) {
    int r = p / 3, c = p % 3, x = x0 + c * cw, y = y0 + r * ch;
    bool down = k & (1 << (r * 4 + c + 1));
    bool active = (mode == M_BEAT && perc[p].env > 0.05f) ||
                  (mode == M_DJ && deck.on && deck.word == page * 12 + p) || (mode == M_TALK && lpc.on && lpc.word == p && lpc.bank == talkBank);
    char nm[8]; shortName(padName(mode, p), nm, 5);
    if (down || active) { oled.drawBox(x, y, cw - 1, ch - 1); oled.setDrawColor(0); }
    else oled.drawFrame(x, y, cw - 1, ch - 1);
    if (mode == M_BEAT && (drumMute & (1 << p))) oled.drawLine(x + 1, y + ch - 3, x + cw - 3, y + 1);
    oled.drawStr(x + 2, y + 7, nm);
    oled.setDrawColor(1);
  }
}

float envStrip[120];
int envWord = -2;
void buildEnvStrip(int w) {
  envWord = w;
  for (int k = 0; k < 120; k++) envStrip[k] = 0;
  if (w < 0 || bank[w].len == 0) return;
  uint32_t len = bank[w].len;
  for (int k = 0; k < 120; k++) {
    uint32_t a = len * k / 120, b = len * (k + 1) / 120;
    int pk = 0;
    for (uint32_t i = a; i < b; i += 4) pk = max(pk, abs((int)pool[bank[w].off + i]));
    envStrip[k] = pk / 128.0f;
  }
}
void drawVizStrip() {                                // y 53..72: mode-specific view
  const int y = 53, h = 20, mid = y + 12;
  char lab[24];
  oled.setFont(u8g2_font_4x6_tf);
  if (mode == M_DJ) {                                // the record: word waveform, playhead, grains
    int wd = deck.on ? deck.word : curWord;
    if (wd != envWord) buildEnvStrip(wd);
    for (int k = 0; k < 120; k++) { int a = (int)(envStrip[k] * 7); if (a) oled.drawVLine(4 + k, mid - a, a * 2 + 1); else oled.drawPixel(4 + k, mid); }
    float pos = deckVis;
    if (pos >= 0 && wd >= 0 && bank[wd].len) {
      int px = 4 + (int)(pos / bank[wd].len * 119);
      oled.setDrawColor(2); oled.drawVLine(px, y + 2, h - 2); oled.setDrawColor(1);
      oled.drawTriangle(px - 2, y + h, px + 2, y + h, px, y + h - 3);
    }
    if (wd >= 0 && bank[wd].len) for (int i = 0; i < 12; i++) {
      float gp = grainVis[i];
      if (gp >= 0) oled.drawPixel(4 + (int)(gp / bank[wd].len * 119), y + 3 + (i % 3) * 2);
    }
    const char* how = !deck.on ? SC_SHORT[scStyle] : deck.how == DK_KNOB ? "HAND" : deck.how == DK_AUTO ? SC_SHORT[deck.style] :
                      deck.how == DK_FREEZE ? "FREEZE" : djBack ? "BACK" : "PLAY";
    snprintf(lab, sizeof lab, "%s %c", how, page ? 'B' : 'A');
  } else if (mode == M_TALK) {
    int fi = lpcVisFrame;
    if (fi >= 0) {
      int32_t kk[10] = {lpc.k1 >> 8, lpc.k2 >> 8, lpc.k[0], lpc.k[1], lpc.k[2], lpc.k[3], lpc.k[4], lpc.k[5], lpc.k[6], lpc.k[7]};
      for (int i = 0; i < 10; i++) {
        int a = constrain((int)(kk[i] * 8 / 128), -8, 8), bx = 30 + i * 8;
        if (a > 0) oled.drawBox(bx, mid - a, 6, a); else if (a < 0) oled.drawBox(bx, mid, 6, -a); else oled.drawHLine(bx, mid, 6);
      }
      int e = constrain((int)(lpc.energy * 18 / 255), 0, 18);
      oled.drawFrame(116, y + 1, 8, 19); oled.drawBox(118, y + 19 - e, 4, e);
      const TalkWord& t = talk[lpc.bank][lpc.word];
      if (t.n) oled.drawHLine(30, y + 19, (int)(fi * 78 / t.n) + 1);
    } else for (int i = 0; i < 10; i++) oled.drawHLine(30 + i * 8, mid, 6);
    oled.drawStr(2, mid + 3, lpc.scratch ? "HAND" : lpFreeze ? "FRZ" : lpMono ? "MONO" : (lpWhisper ? "WHSP" : "LPC"));
    snprintf(lab, sizeof lab, "%s", BANK_NAMES[talkBank]);
  } else {
    for (int i = 0; i < 12; i++) {
      int e = constrain((int)(perc[i].env * 16), 0, 16);
      oled.drawFrame(4 + i * 10, y + 2, 8, 18);
      if (e) oled.drawBox(5 + i * 10, y + 19 - e, 6, e);
      if (drumMute & (1 << i)) oled.drawLine(4 + i * 10, y + 19, 11 + i * 10, y + 2);
    }
    snprintf(lab, sizeof lab, "%s", GROOVES[grooveIdx].name);
  }
  if (lab[0]) tab(128 - oled.getStrWidth(lab) - 4, y, lab);
}

void drawReadout() {
  char wq[28], ch[12] = "--";
  if (mode == M_DJ) snprintf(wq, sizeof wq, "[ %s ]", bank[curWord].text);
  else if (mode == M_TALK) snprintf(wq, sizeof wq, "[ %s ]", talk[talkBank][curTalk].name);
  else snprintf(wq, sizeof wq, "[ %s ]", GROOVES[grooveIdx].name);
  oled.setFont(u8g2_font_5x8_tf);
  int ww = oled.getStrWidth(wq), wx = 64 - ww / 2;
  oled.drawStr(wx, 81, wq);
  if ((millis() / 400) & 1) oled.drawBox(wx + ww + 2, 74, 3, 7);   // blinking cursor
  oled.setFont(u8g2_font_4x6_tf);
  if (fx == FX_VOX || fx == FX_CHOIR) chordLabel(ch, sizeof ch);
  char info[44];
  if (fx == FX_VOX || fx == FX_CHOIR) snprintf(info, sizeof info, "FX:%s %s %d%% CH:%s", FX_NAMES[fx], FX_KNOB[fx], (int)(fxAmt[fx] * 100), ch);
  else snprintf(info, sizeof info, "FX:%s %s %d%%", FX_NAMES[fx], FX_KNOB[fx], (int)(fxAmt[fx] * 100));
  oled.drawStr(64 - oled.getStrWidth(info) / 2, 88, info);
}

void drawSeqPane() {                                 // 16 steps: VOX / DRM lanes + knob meters
  const int y = 90, h = 29;
  brackets(0, y, 128, h);
  for (int s = 0; s < 16; s++) {
    int x = 4 + s * 7 + (s / 4);
    const Step& st = seq[s];
    int lv = y + 3, ld = y + 11;
    if (st.word >= 0) {
      if (st.flags & SF_SCR) { oled.drawLine(x, lv + 5, x + 2, lv); oled.drawLine(x + 2, lv, x + 5, lv + 5); }
      else if (st.src == SRC_TALK || (st.flags & SF_REV)) oled.drawFrame(x, lv, 6, 6);
      else oled.drawBox(x, lv, 6, 6);
    } else oled.drawPixel(x + 2, lv + 3);
    uint16_t d = (st.drum | grooveMask[grooveIdx][s]) & ~drumMute;
    if (d & ((1 << D_KICK) | (1 << D_808))) oled.drawBox(x, ld, 6, 6);
    else if (d & ((1 << D_SNARE) | (1 << D_CLAP) | (1 << D_RIM))) oled.drawFrame(x, ld, 6, 6);
    else if (d) oled.drawHLine(x, ld + 3, 6);
    else oled.drawPixel(x + 2, ld + 3);
    if (s == curStep && playing) { oled.setDrawColor(2); oled.drawBox(x - 1, y + 2, 8, 16); oled.setDrawColor(1); }
  }
  oled.setFont(u8g2_font_4x6_tf);                    // knob meters: FX amount and SPACE
  oled.drawStr(3, y + 25, "FX"); oled.drawFrame(13, y + 20, 48, 5); oled.drawBox(14, y + 21, (int)(46 * fxAmt[fx]), 3);
  oled.drawStr(66, y + 25, "SP"); oled.drawFrame(76, y + 20, 48, 5); oled.drawBox(77, y + 21, (int)(46 * space), 3);
}

void drawChips() {
  struct { const char* t; bool on; } chips[6] = {
    {"REC", rec}, {"PLAY", playing}, {"GRV", grooveIdx > 0}, {"XTLK", xtalk}, {"FRZ", freezeHeld || lpFreeze}, {"INF", revFreeze}};
  oled.setFont(u8g2_font_4x6_tf);
  for (int i = 0; i < 6; i++) {
    int x = i * 21 + 1, w = 20;
    if (chips[i].on && !(i == 0 && ((millis() / 300) & 1))) {   // REC blinks
      oled.drawBox(x, 120, w, 8);
      oled.setDrawColor(0); oled.drawStr(x + (w - oled.getStrWidth(chips[i].t)) / 2, 126, chips[i].t); oled.setDrawColor(1);
    } else {
      dottedH(x, 120, w); dottedH(x, 127, w);
      oled.drawStr(x + (w - oled.getStrWidth(chips[i].t)) / 2, 126, chips[i].t);
    }
  }
}

void menuLabel(int m, int p, char* nm, bool& on) {
  static const char* MODE_L[12] = {"DJ", "TALK", "BEAT", "PAGE", "VOL-", "VOL+", "BANK", "PROG", "VOICE-", "VOICE+", "REPLAY", "CUTS"};
  static const char* FX_L[12] = {"CLEAN", "CRUSH", "ECHO", "RADIO", "GRAIN", "SWARM", "DEEP", "VOX", "CHOIR", "STUTTR", "REVRS", "TAPE"};
  static const char* SEQ_L[12] = {"REC", "PLAY", "GROOVE", "AUTO", "BPM-", "BPM+", "MUTATE", "XTALK", "CLR VOX", "CLR DRM", "CLR ALL", "SILENCE"};
  static const char* DJ_L[12] = {"BABY", "CHIRP", "TRANS", "TEAR", "FLARE", "CRAB", "FREEZE", "BACKWD", "RANDOM", "PAGE", "UNDER", "INFINIT"};
  static const char* TALK_L[12] = {"WHISPER", "MONO", "FREEZE", "STRETCH", "PITCH-", "PITCH+", "GLITCH", "BACKWD", "FORMANT", "RESET", "UNDER", "INFINIT"};
  const char* l = "";
  on = false;
  switch (m) {
    case K_MODE: l = MODE_L[p]; on = (p < 3 && p == mode) || (p == 3 && page); break;
    case K_FX: l = FX_L[p]; on = (p < FX_COUNT && p == fx) || (p == 9 && stutterOn) || (p == 10 && reverseOn) || (p == 11 && tapeOn); break;
    case K_SEQ: l = SEQ_L[p]; on = (p == 0 && rec) || (p == 1 && playing) || (p == 2 && grooveIdx) || (p == 7 && xtalk); break;
    default:
      if (p == 10) { l = "UNDER"; on = underOn; }
      else if (p == 11) { l = "INFINIT"; on = revFreeze; }
      else if (mode == M_DJ) { l = DJ_L[p]; on = (p < SC_COUNT && p == scStyle) || (p == 6 && freezeHeld) || (p == 7 && djBack) || (p == 9 && page); }
      else if (mode == M_TALK) { l = TALK_L[p]; on = (p == 0 && lpWhisper) || (p == 1 && lpMono) || (p == 2 && lpFreeze) || (p == 3 && lpStretch) || (p == 6 && lpGlitch) || (p == 7 && lpRev) || (p == 8 && lpFormant != 1.0f); }
      else { l = PERC[p].name; on = drumMute & (1 << p); }
      break;
  }
  strncpy(nm, l, 8); nm[8] = 0;
}

void drawMenu(int m) {
  char title[32];
  snprintf(title, sizeof title, "%s", MOD_NAMES[m]);
  if (m == K_SHIFT) snprintf(title, sizeof title, "SHIFT: %s  knob=SCRATCH", mode == M_DJ ? "TURNTABLE" : mode == M_TALK ? "BENDS" : "MUTE");
  if (m == K_MODE) snprintf(title, sizeof title, "MODE v%d %s %s %s", volIdx + 1, VOICES[voiceIdx].name, BANK_NAMES[talkBank], PROGS[progIdx].name);
  if (m == K_SEQ) snprintf(title, sizeof title, "SEQ  %d BPM  knob=TEMPO", bpm);
  if (m == K_FX) snprintf(title, sizeof title, "FX  knob=SPACE %d%%", (int)(space * 100));
  tab(0, 0, title);
  oled.setFont(u8g2_font_5x8_tf);
  const int cw = 42, ch = 28, y0 = 10;
  for (int p = 0; p < 12; p++) {
    int r = p / 3, c = p % 3, x = c * cw + 1, y = y0 + r * ch;
    char nm[10]; bool on;
    menuLabel(m, p, nm, on);
    if (on) { oled.drawBox(x, y, cw - 2, ch - 2); oled.setDrawColor(0); } else oled.drawFrame(x, y, cw - 2, ch - 2);
    oled.drawStr(x + (cw - 2 - oled.getStrWidth(nm)) / 2, y + 16, nm);
    oled.setDrawColor(1);
  }
}

void drawAlert(const char* m) {
  oled.setFont(u8g2_font_6x10_tf);
  char t[32]; snprintf(t, sizeof t, ">> %s", m);
  int w = oled.getStrWidth(t) + 10, x = max(2, 64 - w / 2);
  oled.setDrawColor(0); oled.drawBox(x - 2, 40, w + 4, 20); oled.setDrawColor(1);
  oled.drawFrame(x - 2, 40, w + 4, 20); oled.drawFrame(x, 42, w, 16);
  oled.drawStr(x + 5, 53, t);
}

// ---- intro film "CYBERSKRATCH": particles fly together into the cyclops (from Blasteroid), his eye charges, a
// turntable slides in below (in perspective, the hibiscus as its label), the eye LASER burns the grooves into the record,
// then the laser is the stylus for the name cut routine (ROUTINE[]): it fires while the sound plays, rides the groove
// with the deck position, and the record turns with the audio. After the crash the laser burns the name in.
// Painted in grey, then Bayer-dithered with film grain. Any key skips. splash_preview.py mirrors the drawing.
volatile bool splashOn = true;
uint32_t splashT0 = 0, splashEnd = 0, splashLast = 0;
bool splashCuts = false, showCredit = false, splashKeysClear = false;
float camS = 1, camX = 64, camY = 64, sqY = 1, sqC = 64;   // sqY squashes y around sqC (the platter in perspective)
int shakeX = 0, shakeY = 0;

static uint8_t gbuf[128 * 128];
float pB = 1;
void paint(float b) { pB = b; }
static inline float camTX(float x) { return 64 + (x + 0.5f - camX) * camS + shakeX; }
static inline float camTY(float y) { return 64 + (sqC + (y - sqC) * sqY + 0.5f - camY) * camS + shakeY; }
static inline uint8_t g8(float g) { return (uint8_t)constrain((int)(g * 255), 0, 255); }
static inline float smooth01(float x) { x = constrain(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }

void hspan(float xa, float xb, int y) {              // paint pixels whose centres lie in [xa, xb]
  if (y < 0 || y > 127) return;
  if (xa > xb) { float t = xa; xa = xb; xb = t; }
  int x0 = (int)ceilf(xa - 0.5f), x1 = (int)floorf(xb - 0.5f);
  if (x1 < x0) x1 = x0 = (int)floorf((xa + xb) * 0.5f);   // thinner than a pixel: still paint one
  if (x1 < 0 || x0 > 127) return;
  x0 = max(x0, 0); x1 = min(x1, 127);
  uint8_t v = g8(pB), *row = gbuf + y * 128;
  for (int x = x0; x <= x1; x++) row[x] = v;
}
void rTri(float ax, float ay, float bx, float by, float cx, float cy) {
  float t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
  int y0 = max(0, (int)ceilf(ay - 0.5f)), y1 = min(127, (int)floorf(cy - 0.5f));
  for (int y = y0; y <= y1; y++) {
    float yc = y + 0.5f;
    float xa = ax + (cx - ax) * (yc - ay) / max(cy - ay, 1e-4f);
    float xb = yc < by ? ax + (bx - ax) * (yc - ay) / max(by - ay, 1e-4f) : bx + (cx - bx) * (yc - by) / max(cy - by, 1e-4f);
    hspan(xa, xb, y);
  }
}
void rDisc(float cx, float cy, float r) {
  float R = r + 0.5f;
  int y0 = max(0, (int)floorf(cy - R)), y1 = min(127, (int)ceilf(cy + R));
  for (int y = y0; y <= y1; y++) {
    float dy = y + 0.5f - cy;
    if (fabsf(dy) > R) continue;
    float dx = sqrtf(R * R - dy * dy);
    hspan(cx - dx, cx + dx, y);
  }
}
void rPoly(const float* P, int n) {                  // scanline fill, even-odd; P = x0,y0,x1,y1... screen coords
  float ymin = 1e9f, ymax = -1e9f;
  for (int i = 0; i < n; i++) { ymin = min(ymin, P[2 * i + 1]); ymax = max(ymax, P[2 * i + 1]); }
  int y0 = max(0, (int)ceilf(ymin - 0.5f)), y1 = min(127, (int)floorf(ymax - 0.5f));
  float xs[24];
  for (int y = y0; y <= y1; y++) {
    float yc = y + 0.5f; int m = 0;
    for (int i = 0; i < n && m < 24; i++) {
      float ax = P[2 * i], ay = P[2 * i + 1], bx = P[2 * ((i + 1) % n)], by = P[2 * ((i + 1) % n) + 1];
      if ((ay <= yc && yc < by) || (by <= yc && yc < ay)) xs[m++] = ax + (bx - ax) * (yc - ay) / (by - ay);
    }
    for (int i = 1; i < m; i++) { float v = xs[i]; int j = i - 1; while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; } xs[j + 1] = v; }
    for (int k = 0; k + 1 < m; k += 2) hspan(xs[k], xs[k + 1], y);
  }
}
void cLine(float x0, float y0, float x1, float y1, float t = 1) {
  float a = camTX(x0), b = camTY(y0), c = camTX(x1), d = camTY(y1), w = max(t * camS, 1.0f);
  float len = sqrtf((c - a) * (c - a) + (d - b) * (d - b)) + 1e-4f, nx = -(d - b) / len * w * 0.5f, ny = (c - a) / len * w * 0.5f;
  rTri(a + nx, b + ny, a - nx, b - ny, c + nx, d + ny);
  rTri(c + nx, d + ny, c - nx, d - ny, a - nx, b - ny);
  if (w >= 2) { rDisc(a, b, w * 0.5f - 0.5f); rDisc(c, d, w * 0.5f - 0.5f); }
}
void cDisc(float x, float y, float r) { rDisc(camTX(x), camTY(y), r * camS); }
void cPoly(const float* P, int n) {
  float S[48];
  n = min(n, 24);
  for (int i = 0; i < n; i++) { S[2 * i] = camTX(P[2 * i]); S[2 * i + 1] = camTY(P[2 * i + 1]); }
  rPoly(S, n);
}
// hard contrast + Bayer 4x4 + grain; true black stays black
void ditherOut(uint32_t t) {
  static const uint8_t B4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  uint32_t r = t * 2654435761u | 1;
  for (int y = 0; y < 128; y++) {
    const uint8_t* row = gbuf + y * 128;
    for (int x = 0; x < 128; x++) {
      int g = ((int)row[x] - 115) * 3 / 2 + 115;
      r ^= r << 13; r ^= r >> 17; r ^= r << 5;
      int thr = B4[y & 3][x & 3] * 16 + 8 + (int)(r % 37) - 18;
      if (g > max(thr, 8)) oled.drawPixel(x, y);
    }
  }
}

// ---- the hibiscus (record label): ruffled petals nudged sideways so they overlap like a pinwheel, dark eye ----
int petalPts(float* P, float cx, float cy, float th, float L, float W, float sc, float ruf) {
  float sx[10], sy[10];
  for (int k = 0; k < 10; k++) {
    float s = k / 9.0f;
    sx[k] = L * s;
    sy[k] = W * powf(s, 0.55f) * powf(max(0.0f, 1 - powf(s, 3.2f)), 0.45f) * (1 + 0.07f * s * s * sinf(s * 19 + ruf));
  }
  float c = cosf(th), sn = sinf(th), off = W * 0.22f;
  for (int k = 0; k < 20; k++) {
    int j = k < 10 ? k : 19 - k;
    float x = sx[j], y = (k < 10 ? sy[j] : -sy[j]) + off;
    P[2 * k] = cx + (x * c - y * sn) * sc; P[2 * k + 1] = cy + (x * sn + y * c) * sc;
  }
  return 20;
}
void drawHibiscus(float cx, float cy, float sc, float rot, float fade) {
  const float L = 48, W = 28;
  float P[40];
  for (int i = 0; i < 5; i++) {
    float th = rot - PI / 2 + (i - 2) * 2 * PI / 5, ruf = i * 1.7f;
    petalPts(P, cx, cy, th, L, W, sc, ruf);
    float e0x = P[0], e0y = P[1], e1x = P[24], e1y = P[25];      // the leading edge (base -> point 12)
    paint(0.84f * fade); cPoly(P, 20);
    petalPts(P, cx, cy, th, L * 0.6f, W * 0.62f, sc, ruf); paint(0.62f * fade); cPoly(P, 20);
    petalPts(P, cx, cy, th, L * 0.32f, W * 0.42f, sc, ruf); paint(0.1f * fade); cPoly(P, 20);   // the dark eye
    paint(0.3f * fade); cLine(e0x, e0y, e1x, e1y, 0.5f);   // where this petal tucks under the next
  }
}

// ---- the cyclops (from blasteroid_megasynth): head coordinates, drawn HS x around (64, HCY) -> screen y HSY ----
const float HEAD[24] = {44, 11, 52, 5, 76, 5, 84, 11, 87, 22, 86, 36, 80, 46, 72, 51, 56, 51, 48, 46, 42, 36, 41, 22};
const float SHADES[20] = {35, 22, 46, 18.5f, 64, 17.5f, 82, 18.5f, 93, 22, 91, 30, 80, 33.5f, 64, 32, 48, 33.5f, 37, 30};
const float EYE_X = 64, EYE_Y = 25.5f, HS = 0.95f, HCY = 27, HSY = 26;
static inline float headSX(float x) { return 64 + (x - 64) * HS; }
static inline float headSY(float y) { return HSY + (y - HCY) * HS; }

void drawHead(float fade, float eye, float t) {
  float cs = camS, cx = camX, cy = camY;
  camS = HS; camX = 64; camY = HCY + (64 - HSY) / HS;
  float tmp[24];
  static const float SH0[8] = {46, 49, 82, 49, 100, 58, 28, 58}, SH1[8] = {48, 50, 80, 50, 97, 57.5f, 31, 57.5f};
  paint(0); cPoly(SH0, 4);                           // shoulders / collar: dark, rim-lit
  paint(0.3f * fade); cPoly(SH1, 4);
  paint(0.9f * fade); cLine(80, 50, 97, 57.5f, 0.7f); cLine(31, 57.5f, 48, 50, 0.5f);
  for (int i = 0; i < 12; i++) { tmp[2 * i] = HEAD[2 * i] + (HEAD[2 * i] - 64) * 0.07f; tmp[2 * i + 1] = HEAD[2 * i + 1] + (HEAD[2 * i + 1] - 28) * 0.07f; }
  paint(0); cPoly(tmp, 12);
  paint(0.95f * fade); cPoly(HEAD, 12);              // head: bold and bright, a shadow side
  static const float SHADOW[12] = {42, 36, 41, 22, 46.5f, 22, 48, 37, 52, 48, 48, 46};
  paint(0.55f * fade); cPoly(SHADOW, 6);
  paint(0); cLine(52, 10.5f, 76, 10.5f, 0.5f);                                // forehead seam
  cLine(64.5f, 33.5f, 62, 38.8f, 0.7f); cLine(62, 38.8f, 65.5f, 39.2f, 0.7f); // nose
  cLine(56, 43, 72, 43, 1.6f); cLine(56, 43, 54, 44.8f, 1.2f); cLine(72, 43, 74, 44.8f, 1.2f);   // grim mouth
  cLine(61, 48.6f, 67, 48.6f, 0.7f);                                          // chin
  for (int i = 0; i < 10; i++) { tmp[2 * i] = SHADES[2 * i] + (SHADES[2 * i] - 64) * 0.05f; tmp[2 * i + 1] = SHADES[2 * i + 1] + (SHADES[2 * i + 1] > 25 ? 1.4f : -1.4f); }
  paint(0); cPoly(tmp, 10);                          // wraparound shades: black, white rim, reflection, glint
  paint(0.85f * fade);
  for (int i = 0; i < 10; i++) { int q = (i + 1) % 10; cLine(tmp[2 * i], tmp[2 * i + 1], tmp[2 * q], tmp[2 * q + 1], 0.55f); }
  paint(fade); cLine(38, 28.5f, 90, 28.5f, 0.6f);
  float g = fmodf(t * 0.6f, 1.6f);
  if (g < 1) { paint(1); cLine(40 + g * 46, 19.5f, 37 + g * 46, 30, 0.9f); }
  if (eye > 0) {                                     // the single eye blazing through the lens
    paint(0.3f + 0.35f * min(1.0f, eye)); cDisc(EYE_X, EYE_Y, 2 + 3.5f * eye);
    paint(1); cDisc(EYE_X, EYE_Y, 0.8f + 1.4f * eye);
  }
  camS = cs; camX = cx; camY = cy;
}

// ---- the turntable, in perspective ----
const float RCX = 64, RCY = 88, RRX = 54, SQ = 0.36f;
void drawDeck(float fade, float burnR, float ang, float slide) {
  float cy = RCY + slide, P[48];
  for (int i = 0; i < 24; i++) { float a = i * 2 * PI / 24; P[2 * i] = RCX + RRX * cosf(a); P[2 * i + 1] = cy + RRX * sinf(a) * SQ; }
  paint(0.35f * fade);                               // platter edge: the near half, 4 px thick
  for (int i = 0; i <= 12; i++) { int q = (i + 1) % 24; cLine(P[2 * i], P[2 * i + 1] + 2, P[2 * q], P[2 * q + 1] + 2, 4); }
  paint(0.08f * fade); cPoly(P, 24);                 // vinyl
  paint(0.55f * fade);
  for (int i = 0; i < 24; i++) { int q = (i + 1) % 24; cLine(P[2 * i], P[2 * i + 1], P[2 * q], P[2 * q + 1], 0.6f); }
  paint(0.38f * fade);                               // grooves, only where the laser has burned them
  for (int k = 0; k < 6; k++) {
    float r = 50 - k * 6;
    if (r < burnR) continue;
    float px = RCX + r, py = cy;
    for (int i = 1; i <= 32; i++) { float a = i * 2 * PI / 32, x = RCX + r * cosf(a), y = cy + r * sinf(a) * SQ; cLine(px, py, x, y, 0.45f); px = x; py = y; }
  }
  sqY = SQ; sqC = cy;                                // the hibiscus label, squashed onto the platter, turning
  drawHibiscus(RCX, cy, 0.33f, ang, fade);
  paint(0); cDisc(RCX, cy, 1.4f);
  sqY = 1;
}
void drawLaser(float x0, float y0, float x1, float y1, float power) {
  paint(0.55f); cLine(x0, y0, x1, y1, 2.6f * power);
  paint(1); cLine(x0, y0, x1, y1, 1.1f);
  paint(0.6f); cDisc(x1, y1, 3.5f * power); paint(1); cDisc(x1, y1, 1.4f);
  paint(0.9f);                                       // sparks
  for (int j = 0; j < 5; j++) {
    float a = (esp_random() % 628) / 100.0f, rr = 2 + (esp_random() % 600) / 100.0f;
    cDisc(x1 + cosf(a) * rr, y1 + sinf(a) * rr * 0.5f - (esp_random() % 400) / 100.0f, 0.5f);
  }
}
static inline void laserSpot(float r, float slide, float& x, float& y) { x = RCX + r * 0.55f; y = RCY + slide + r * SQ * 0.75f; }

struct Film { float P[150][2], face[150][2]; } film;
float recAng = 0, lastDeckVis = -1;
int lastDeckWord = -1;
uint32_t routineEndAt = 0, lastZap = 0;

void filmInit() {
  for (auto& p : film.P) { p[0] = (esp_random() % 12800) / 100.0f; p[1] = (esp_random() % 12800) / 100.0f; }
  int k = 0;                                         // targets along the head outline (60 %) and the shades (40 %)
  for (int pass = 0; pass < 2; pass++) {
    const float* P = pass ? SHADES : HEAD; int n = pass ? 10 : 12, m = pass ? 60 : 90;
    float L[12], tot = 0;
    for (int e = 0; e < n; e++) { int q = (e + 1) % n; L[e] = sqrtf(sq(P[2 * q] - P[2 * e]) + sq(P[2 * q + 1] - P[2 * e + 1])); tot += L[e]; }
    for (int c = 0; c < m && k < 150; c++) {
      float d = tot * c / m; int e = 0;
      while (e < n - 1 && d > L[e]) { d -= L[e]; e++; }
      int q = (e + 1) % n; float u = d / L[e];
      film.face[k][0] = headSX(P[2 * e] + (P[2 * q] - P[2 * e]) * u); film.face[k][1] = headSY(P[2 * e + 1] + (P[2 * q + 1] - P[2 * e + 1]) * u); k++;
    }
  }
  recAng = 0; lastDeckVis = -1; lastDeckWord = -1; routineEndAt = 0; lastZap = 0;
  memset(gbuf, 0, sizeof gbuf);
}

void drawSplash() {
  uint32_t now = millis();
  if (!splashT0) { splashT0 = now; splashLast = now; splashEnd = now + 17000; filmInit(); }
  float dt = min(0.1f, (now - splashLast) * 0.001f);
  splashLast = now;
  float u = (now - splashT0) * 0.001f;
  camS = 1; camX = 64; camY = 64; shakeX = shakeY = 0; sqY = 1;
  memset(gbuf, 0, sizeof gbuf);
  float ex = headSX(EYE_X), ey = headSY(EYE_Y);
  float burnT = routineEndAt ? (now - routineEndAt) * 0.001f : -1;   // the name burn, after the routine
  if (u < 1.8f) {                                    // particles fly together into the cyclops
    float e = 1 - powf(1 - smooth01((u - 0.4f) / 1.4f), 3);
    if (u > 1.3f) drawHead((u - 1.3f) / 0.5f * 0.7f, 0, u);
    paint(1);
    for (int i = 0; i < 150; i++) cDisc(film.P[i][0] + (film.face[i][0] - film.P[i][0]) * e, film.P[i][1] + (film.face[i][1] - film.P[i][1]) * e, 0.7f);
  } else {
    float slide = 40 * (1 - smooth01((u - 2.2f) / 0.9f));
    float burn = u > 3.2f ? 50 - 36 * smooth01((u - 3.2f) / 1.3f) : 99;
    bool cutting = u > 3.2f && u < 4.6f;
    // the record turns: by itself while cutting, with the deck during the routine
    if (routineOn && deck.on && deck.word >= NAME_WORD) {
      float p = deckVis;
      if (deck.word == lastDeckWord && lastDeckVis >= 0 && p >= 0 && fabsf(p - lastDeckVis) < FS * 0.15f) recAng += (p - lastDeckVis) / FS * 2 * PI * 0.55f;
      lastDeckVis = p; lastDeckWord = deck.word;
    } else { recAng += dt * 2 * PI * 0.55f; lastDeckVis = -1; }   // 33 rpm
    float eye = min(1.0f, (u - 1.8f) / 0.6f);
    if (routineOn) eye = 0.7f + 0.6f * outPeak;
    if (u > 2.2f) drawDeck(min(1.0f, (u - 2.2f) / 0.5f), burn, recAng, slide);
    if (routineOn && gateVis > 0.5f) shakeX = (int)(outPeak * 2.0f);
    drawHead(1, eye, u);
    shakeX = 0;
    float lx, ly;
    if (cutting) {                                   // the laser cuts the grooves, rim to centre
      laserSpot(burn, slide, lx, ly); drawLaser(ex, ey, lx, ly, 1);
      if (now - lastZap > 260) { lastZap = now; zapReq = true; }
    } else if (routineOn && deck.on && deck.word >= NAME_WORD && gateVis > 0.4f && bank[deck.word].len) {
      float pos = constrain((float)deckVis / bank[deck.word].len, 0.0f, 1.0f);   // the laser is the stylus
      laserSpot(48 - 32 * pos, 0, lx, ly); drawLaser(ex, ey, lx, ly, 0.6f + 0.6f * gateVis);
    } else if (burnT >= 0 && burnT < 0.9f) {         // ...and burns the name in
      oled.setFont(u8g2_font_helvB10_tr);
      int w = oled.getStrWidth("CYBERSKRATCH");
      drawLaser(ex, ey, 64 - w / 2 + w * burnT / 0.9f, 120, 1);
      if (now - lastZap > 200) { lastZap = now; zapReq = true; }
    }
  }
  if (u >= 4.8f && !splashCuts) { splashCuts = true; routineReq = true; }   // the name cut routine
  if (splashCuts && !routineOn && !routineReq && !routineEndAt) { routineEndAt = now; splashEnd = now + 3200; }
  int rs = routineStepVis;
  bool crash = rs >= ROUTINE_LEN - 1;
  if (crash) memset(gbuf, 255, sizeof gbuf);         // white-out on the crash
  else if (burnT >= 0) {                             // scorch glow along the name while it burns
    oled.setFont(u8g2_font_helvB10_tr);
    int w = oled.getStrWidth("CYBERSKRATCH"), lit = 64 - w / 2 + (int)(w * min(1.0f, burnT / 0.9f));
    for (int y = 108; y < 128; y++) {
      uint8_t v = g8(0.16f * expf(-sq((y - 120) / 6.0f)));
      for (int x = 0; x < lit && x < 128; x++) if (v > gbuf[y * 128 + x]) gbuf[y * 128 + x] = v;
    }
  }
  ditherOut(now);

  if (burnT >= 0 && !crash) {                        // CYBERSKRATCH, burned in up to where the laser is
    oled.setFont(u8g2_font_helvB10_tr);
    const char* T = "CYBERSKRATCH";
    int w = oled.getStrWidth(T), x0 = 64 - w / 2, lit = x0 + (int)(w * min(1.0f, burnT / 0.9f));
    oled.setDrawColor(0);
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) oled.drawStr(x0 + dx, 125 + dy, T);
    oled.setDrawColor(1);
    oled.drawStr(x0, 125, T);
    oled.setDrawColor(0); if (lit < 128) oled.drawBox(lit, 112, 128 - lit, 16); oled.setDrawColor(1);
    oled.setFont(u8g2_font_4x6_tf);
    if (burnT > 1.6f && ((now / 500) & 1)) {
      const char* pr = "PRESS ANY KEY";
      int pw = oled.getStrWidth(pr);
      oled.setDrawColor(0); oled.drawBox(64 - pw / 2 - 2, 58, pw + 4, 8); oled.setDrawColor(1);
      oled.drawStr(64 - pw / 2, 64, pr);
    }
  }
  if (showCredit && u > 5.0f && !crash) {
    oled.setFont(u8g2_font_4x6_tf);
    const char* cr = "MARK HELLAR + CLAUDE";
    int cw = oled.getStrWidth(cr);
    oled.setDrawColor(0); oled.drawBox(64 - cw / 2 - 2, 0, cw + 4, 7); oled.setDrawColor(1);
    oled.drawStr(64 - cw / 2, 6, cr);
  }
  if (bankBusy) oled.drawHLine(34, 127, 60 * renderProgress / NWORDS);   // voices still loading
}

// =====================================================================
// VJ view: abstract 3D glitch visuals that play along. Polygon engine (flat-shaded solids + wireframes, painter's
// sort), drawn grey into gbuf, glitched, Bayer-dithered straight into u8g2's buffer.
//   DJ   = a solid that changes shape with every word, spun by the record (scratch it and it whips), grid floor,
//          the output scope as a halo, grains as shards
//   TALK = the Speak & Spell mouth as a wireframe landscape rolling at you (LPC coefficients = the terrain)
//   BEAT = a tunnel: every drum fires its own ring
//   kick = pulse, snare/clap = sliced rows, crash = white flash, a new word = a double image for a moment
//   FX:  ECHO trails, RADIO scanlines + tuning tear, CRUSH big pixels, CHOIR particles, UNDERWATER wobble,
//        GRAIN pixel scatter, SWARM clones, DEEP dark + vignette, VOX kaleidoscope, STUTTER freeze-frame,
//        REVERSE negative, TAPE smear, INFINITE zoom feedback
// =====================================================================
static Mesh meshes[10];
enum { MS_ICO, MS_CUBE, MS_OCTA, MS_TORUS, MS_TETRA, MS_HEAD, MS_VISOR, MS_BODY, MS_PLATTER, MS_SPHERE };
static PTri ptris[420];
static int nPT = 0;
static Ring rings[28];
static Part parts[70];
static uint8_t vtmp[128 * 128];                      // feedback / freeze frame
static float terr[20][12];                           // TALK landscape rows
static int terrHead = 0;
static float vjT = 0, kickPulse = 0, sliceT = 0, flashV = 0, splitT = 0, labelT = -9, deckAng = 0, rollV = 0, lastDeck = -1;
static char vjLabel[40] = "";
static uint32_t seenTrig = 0;
static int lastStepVJ = -1;
static float lastEnv[5];
static bool frozen = false;

void addTri(Mesh& m, int a, int b, int c) { if (m.nt < 128) m.t[m.nt++] = {(uint8_t)a, (uint8_t)b, (uint8_t)c}; }
void buildMeshes() {
  const float t = 1.618034f;
  Mesh& ico = meshes[MS_ICO];
  const float IV[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
                           {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  const uint8_t IF[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6},
                             {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
  float k = 1 / sqrtf(1 + t * t);
  for (int i = 0; i < 12; i++) ico.v[i] = {IV[i][0] * k, IV[i][1] * k, IV[i][2] * k};
  ico.nv = 12; for (auto& f : IF) addTri(ico, f[0], f[1], f[2]);
  Mesh& cu = meshes[MS_CUBE];
  for (int i = 0; i < 8; i++) cu.v[i] = {(i & 1) ? 0.62f : -0.62f, (i & 2) ? 0.62f : -0.62f, (i & 4) ? 0.62f : -0.62f};
  cu.nv = 8;
  const uint8_t CF[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
  for (auto& f : CF) { addTri(cu, f[0], f[1], f[2]); addTri(cu, f[0], f[2], f[3]); }
  Mesh& oc = meshes[MS_OCTA];
  const float OV[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (int i = 0; i < 6; i++) oc.v[i] = {OV[i][0], OV[i][1], OV[i][2]};
  oc.nv = 6;
  for (int a = 0; a < 2; a++) for (int b = 2; b < 4; b++) for (int c = 4; c < 6; c++) addTri(oc, a, b, c);
  Mesh& to = meshes[MS_TORUS];                       // 8 around x 6 through the tube
  to.nv = 48;
  for (int i = 0; i < 8; i++) for (int j = 0; j < 6; j++) {
    float u = i * 2 * PI / 8, v = j * 2 * PI / 6, r = 0.68f + 0.26f * cosf(v);
    to.v[i * 6 + j] = {r * cosf(u), 0.26f * sinf(v), r * sinf(u)};
  }
  for (int i = 0; i < 8; i++) for (int j = 0; j < 6; j++) {
    int a = i * 6 + j, b = ((i + 1) % 8) * 6 + j, c = ((i + 1) % 8) * 6 + (j + 1) % 6, d = i * 6 + (j + 1) % 6;
    addTri(to, a, b, c); addTri(to, a, c, d);
  }
  Mesh& te = meshes[MS_TETRA];
  te.v[0] = {0, 0.7f, 0}; te.v[1] = {-0.6f, -0.35f, 0.35f}; te.v[2] = {0.6f, -0.35f, 0.35f}; te.v[3] = {0, -0.35f, -0.7f}; te.nv = 4;
  addTri(te, 0, 1, 2); addTri(te, 0, 2, 3); addTri(te, 0, 3, 1); addTri(te, 1, 3, 2);
  for (auto& p : parts) { p.x = random(-300, 300) / 100.0f; p.y = random(-300, 300) / 100.0f; p.z = random(100, 1200) / 100.0f; }
  // ---- the cyclops for the 3D intro: a low-poly head (front = -z), wraparound visor, shoulders, a platter ----
  Mesh& h = meshes[MS_HEAD];
  const int N = 9, R = 6;
  h.v[h.nv++] = {0, 0.78f, 0};
  for (int r = 0; r < R; r++) {
    float phi = (r + 1) * PI / (R + 1);
    float cy = cosf(phi), sy = sinf(phi);
    float ry = (cy < 0 ? -1 : 1) * powf(fabsf(cy), 0.75f) * 0.8f, rr = powf(sy, 0.6f);   // rounder, flatter crown
    for (int i = 0; i < N; i++) {
      float th = -PI / 2 + i * 2 * PI / N, sx = 0.6f, sz = 0.6f;
      if (phi > 1.9f) { sx *= 0.82f; sz *= 0.9f; }       // a narrower jaw
      h.v[h.nv++] = {rr * cosf(th) * sx, ry, rr * sinf(th) * sz};
    }
  }
  h.v[h.nv++] = {0, -0.86f, -0.1f};                     // the chin juts out
  for (int i = 0; i < N; i++) addTri(h, 0, 1 + i, 1 + (i + 1) % N);
  for (int r = 0; r < R - 1; r++) for (int i = 0; i < N; i++) {
    int a = 1 + r * N + i, b = 1 + r * N + (i + 1) % N, c = 1 + (r + 1) * N + (i + 1) % N, d = 1 + (r + 1) * N + i;
    addTri(h, a, b, c); addTri(h, a, c, d);
  }
  for (int i = 0; i < N; i++) addTri(h, h.nv - 1, 1 + (R - 1) * N + (i + 1) % N, 1 + (R - 1) * N + i);
  Mesh& vz = meshes[MS_VISOR];
  for (int i = 0; i <= 8; i++) {
    float a = -1.05f + i * 2.1f / 8, x = sinf(a) * 0.64f, z = -cosf(a) * 0.64f;
    vz.v[vz.nv++] = {x, 0.2f, z}; vz.v[vz.nv++] = {x * 1.02f, -0.06f, z * 1.02f};
  }
  for (int i = 0; i < 8; i++) { int a = 2 * i, b = 2 * i + 1, c = 2 * i + 3, d = 2 * i + 2; addTri(vz, a, b, c); addTri(vz, a, c, d); }
  Mesh& bo = meshes[MS_BODY];
  const float BV[8][3] = {{-0.45f, -0.8f, -0.3f}, {0.45f, -0.8f, -0.3f}, {-1.0f, -1.3f, -0.4f}, {1.0f, -1.3f, -0.4f},
                          {-0.45f, -0.8f, 0.3f}, {0.45f, -0.8f, 0.3f}, {-1.0f, -1.3f, 0.4f}, {1.0f, -1.3f, 0.4f}};
  for (int i = 0; i < 8; i++) bo.v[bo.nv++] = {BV[i][0], BV[i][1], BV[i][2]};
  const uint8_t BF[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
  for (auto& f : BF) { addTri(bo, f[0], f[1], f[2]); addTri(bo, f[0], f[2], f[3]); }
  Mesh& gs = meshes[MS_SPHERE];                         // the glitch sphere (the intro's "cyclops", featureless)
  gs.v[gs.nv++] = {0, 0.72f, 0};
  for (int r = 0; r < 5; r++) {
    float phi = (r + 1) * PI / 6;
    for (int i = 0; i < 10; i++) { float th = -PI / 2 + i * 2 * PI / 10; gs.v[gs.nv++] = {sinf(phi) * cosf(th) * 0.72f, cosf(phi) * 0.72f, sinf(phi) * sinf(th) * 0.72f}; }
  }
  gs.v[gs.nv++] = {0, -0.72f, 0};
  for (int i = 0; i < 10; i++) addTri(gs, 0, 1 + i, 1 + (i + 1) % 10);
  for (int r = 0; r < 4; r++) for (int i = 0; i < 10; i++) {
    int a = 1 + r * 10 + i, b = 1 + r * 10 + (i + 1) % 10, c = 1 + (r + 1) * 10 + (i + 1) % 10, d = 1 + (r + 1) * 10 + i;
    addTri(gs, a, b, c); addTri(gs, a, c, d);
  }
  for (int i = 0; i < 10; i++) addTri(gs, gs.nv - 1, 41 + (i + 1) % 10, 41 + i);
  Mesh& pl = meshes[MS_PLATTER];
  pl.v[pl.nv++] = {0, 0.07f, 0};
  for (int i = 0; i < 16; i++) { float a = i * 2 * PI / 16; pl.v[pl.nv++] = {cosf(a) * 1.9f, 0.07f, sinf(a) * 1.9f}; }
  for (int i = 0; i < 16; i++) { float a = i * 2 * PI / 16; pl.v[pl.nv++] = {cosf(a) * 1.9f, -0.09f, sinf(a) * 1.9f}; }
  for (int i = 0; i < 16; i++) {
    int j = (i + 1) % 16;
    addTri(pl, 0, 1 + i, 1 + j);
    addTri(pl, 1 + i, 1 + j, 17 + j); addTri(pl, 1 + i, 17 + j, 17 + i);
  }
}

// orbit camera for the 3D intro: the world turns round a pivot in front of the lens (the VJ view keeps it at rest)
static bool camOn = false;
static float cYc = 1, cYs = 0, cPc = 1, cPs = 0, cDist = 0;
static const float CPIVZ = 4.4f;
void setCam(float yaw, float pitch, float dist) {
  camOn = yaw != 0 || pitch != 0 || dist != 0;
  cYc = cosf(yaw); cYs = sinf(yaw); cPc = cosf(pitch); cPs = sinf(pitch); cDist = dist;
}
static inline void w2c(float& x, float& y, float& z) {
  if (!camOn) return;
  float zz = z - CPIVZ;
  float x2 = x * cYc + zz * cYs; zz = -x * cYs + zz * cYc; x = x2;
  float y2 = y * cPc - zz * cPs; zz = y * cPs + zz * cPc; y = y2;
  z = zz + CPIVZ + cDist;
}

// camera: looks down +z, rolls around the view axis
static float camRoll = 0, camCR = 1, camSR = 0;
static const float FOC = 92;
static inline bool proj(float x, float y, float z, int& sx, int& sy) {
  if (z < 0.25f) return false;
  float rx = x * camCR - y * camSR, ry = x * camSR + y * camCR;
  sx = 64 + (int)(FOC * rx / z); sy = 64 - (int)(FOC * ry / z);
  return true;
}
static inline void gPix(int x, int y, uint8_t v) { if ((unsigned)x < 128 && (unsigned)y < 128 && gbuf[y * 128 + x] < v) gbuf[y * 128 + x] = v; }
void gLine(int x0, int y0, int x1, int y1, uint8_t v, bool over) {
  if (abs(x1 - x0) > 600 || abs(y1 - y0) > 600) return;
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
  for (;;) {
    if ((unsigned)x0 < 128 && (unsigned)y0 < 128) { uint8_t& p = gbuf[y0 * 128 + x0]; if (over || p < v) p = v; }
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * e;
    if (e2 >= dy) { e += dy; x0 += sx; }
    if (e2 <= dx) { e += dx; y0 += sy; }
  }
}
void line3(float ax, float ay, float az, float bx, float by, float bz, uint8_t v) {
  w2c(ax, ay, az); w2c(bx, by, bz);
  if (az < 0.3f && bz < 0.3f) return;
  if (az < 0.3f) { float t = (0.3f - az) / (bz - az); ax += (bx - ax) * t; ay += (by - ay) * t; az = 0.3f; }
  if (bz < 0.3f) { float t = (0.3f - bz) / (az - bz); bx += (ax - bx) * t; by += (ay - by) * t; bz = 0.3f; }
  int x0, y0, x1, y1;
  if (proj(ax, ay, az, x0, y0) && proj(bx, by, bz, x1, y1)) gLine(x0, y0, x1, y1, v, false);
}
void gTri(float ax, float ay, float bx, float by, float cx, float cy, uint8_t v) {
  if (ay > by) { std::swap(ax, bx); std::swap(ay, by); }
  if (ay > cy) { std::swap(ax, cx); std::swap(ay, cy); }
  if (by > cy) { std::swap(bx, cx); std::swap(by, cy); }
  int y0 = max(0, (int)ceilf(ay)), y1 = min(127, (int)floorf(cy));
  for (int y = y0; y <= y1; y++) {
    float xa = cy != ay ? ax + (cx - ax) * (y - ay) / (cy - ay) : ax, xb;
    if (y < by) xb = by != ay ? ax + (bx - ax) * (y - ay) / (by - ay) : bx;
    else xb = cy != by ? bx + (cx - bx) * (y - by) / (cy - by) : bx;
    if (xa > xb) std::swap(xa, xb);
    int l = max(0, (int)ceilf(xa)), r = min(127, (int)floorf(xb));
    uint8_t* row = gbuf + y * 128;
    for (int x = l; x <= r; x++) row[x] = v;
  }
}
// queue a mesh: style 0 = solid, 1 = wire, 2 = solid with bright edges
void queueMesh(const Mesh& m, float px, float py, float pz, float yaw, float pitch, float roll, float sc, int style, float bright) {
  float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
  static V3 w[64]; static int sx[64], sy2[64]; static bool ok[64];
  for (int i = 0; i < m.nv; i++) {
    V3 v = m.v[i];
    float x = v.x * cy + v.z * sy, z = -v.x * sy + v.z * cy, y = v.y;          // yaw
    float y2 = y * cp - z * sp; z = y * sp + z * cp; y = y2;                  // pitch
    float x2 = x * cr - y * sr; y = x * sr + y * cr; x = x2;                  // roll
    w[i] = {x * sc + px, y * sc + py, z * sc + pz};
    w2c(w[i].x, w[i].y, w[i].z);
    ok[i] = proj(w[i].x, w[i].y, w[i].z, sx[i], sy2[i]);
  }
  const float LX = -0.45f, LY = 0.6f, LZ = -0.66f;
  for (int i = 0; i < m.nt && nPT < 420; i++) {
    const Tri3& t = m.t[i];
    if (!ok[t.a] || !ok[t.b] || !ok[t.c]) continue;
    V3 a = w[t.a], b = w[t.b], c = w[t.c];
    float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx, nl = sqrtf(nx * nx + ny * ny + nz * nz) + 1e-6f;
    float mz = (a.z + b.z + c.z) / 3;
    float l = fabsf((nx * LX + ny * LY + nz * LZ) / nl);                       // two-sided light
    PTri& p = ptris[nPT++];
    p.z = mz;
    p.x[0] = sx[t.a]; p.y[0] = sy2[t.a]; p.x[1] = sx[t.b]; p.y[1] = sy2[t.b]; p.x[2] = sx[t.c]; p.y[2] = sy2[t.c];
    p.shade = (uint8_t)constrain((30 + 200 * l) * bright, 0.0f, 250.0f);
    p.wire = style;
  }
}
void flushTris() {                                   // far to near (painter's)
  for (int i = 1; i < nPT; i++) { PTri k = ptris[i]; int j = i - 1; while (j >= 0 && ptris[j].z < k.z) { ptris[j + 1] = ptris[j]; j--; } ptris[j + 1] = k; }
  for (int i = 0; i < nPT; i++) {
    PTri& p = ptris[i];
    if (p.wire != 1) gTri(p.x[0], p.y[0], p.x[1], p.y[1], p.x[2], p.y[2], p.shade);
    if (p.wire >= 1) for (int k = 0; k < 3; k++) gLine(p.x[k], p.y[k], p.x[(k + 1) % 3], p.y[(k + 1) % 3], 255, p.wire == 2);
  }
  nPT = 0;
}

float beatPos() { return (curStep + stepFrac) / 4.0f; }      // in beats, 0..4 over the bar

void sceneDJ(float dt) {
  // grid floor rushing at you, two lines per beat
  float bp = beatPos();
  for (int i = 0; i < 12; i++) {
    float z = 1.2f + i - fmodf(bp * 2, 1.0f);
    line3(-7, -1.7f, z, 7, -1.7f, z, (uint8_t)constrain(200 - z * 16, 20.0f, 200.0f));
  }
  for (int i = -6; i <= 6; i++) line3(i * 1.1f, -1.7f, 1.0f, i * 1.1f, -1.7f, 13, 90);
  // the object: one shape per word, spun by the record
  static const uint8_t CYC[4] = {MS_ICO, MS_CUBE, MS_OCTA, MS_TORUS};
  int mi = CYC[wordTrig & 3];
  float s = 1.15f * (1 + 0.28f * kickPulse) * (0.85f + 0.3f * outPeak);
  float yaw = vjT * 0.55f + deckAng, pitch = vjT * 0.31f, roll = vjT * 0.17f;
  queueMesh(meshes[mi], 0, 0.15f, 4.2f, yaw, pitch, roll, s, 2, 1.0f);
  if (splitT > 0) queueMesh(meshes[mi], (random(2) ? 0.5f : -0.5f), 0.15f, 4.2f, yaw + 0.3f, pitch, roll, s * 1.1f, 1, 1);
  if (fx == FX_SWARM) for (int k = 0; k < 6; k++) {   // clones in orbit
    float a = vjT * (0.9f + fxAmt[FX_SWARM]) + k * PI / 3;
    queueMesh(meshes[CYC[(wordTrig + k) & 3]], cosf(a) * 2.5f, 0.15f + sinf(a * 2) * 0.6f, 4.2f + sinf(a) * 1.8f, yaw * 2 + k, pitch, 0, 0.35f, 2, 0.9f);
  }
  // grains = shards flying off the record
  int dw = deck.word;
  if (dw >= 0 && bank[dw].len) for (int i = 0; i < 12; i++) {
    float gp = grainVis[i];
    if (gp < 0) continue;
    float a = gp / bank[dw].len * 2 * PI * 3 + i;
    queueMesh(meshes[MS_TETRA], cosf(a) * 1.9f, sinf(a * 1.3f) * 1.1f, 4.2f + sinf(a) * 1.5f, vjT * 3 + i, vjT * 2, 0, 0.22f, 2, 1);
  }
  flushTris();
  // the output scope as a halo round it
  int px = 0, py = 0;
  for (int i = 0; i <= 64; i++) {
    float a = i * 2 * PI / 64, r = 1.75f + scopeBuf[(i * 2) & 127] * 0.9f;
    int x, y;
    if (proj(cosf(a) * r, 0.15f + sinf(a) * r, 4.2f, x, y)) { if (i) gLine(px, py, x, y, 170, false); px = x; py = y; }
  }
}

void sceneTalk(float dt) {
  // a new landscape row ~12 times a second; speech raises mountains, silence flattens them
  static float acc = 0;
  acc += dt * 12;
  while (acc >= 1) {
    acc -= 1;
    terrHead = (terrHead + 19) % 20;
    float* row = terr[terrHead];
    const float* prev = terr[(terrHead + 1) % 20];
    if (lpcVisFrame >= 0) {
      int32_t kk[10] = {lpc.k1 >> 8, lpc.k2 >> 8, lpc.k[0], lpc.k[1], lpc.k[2], lpc.k[3], lpc.k[4], lpc.k[5], lpc.k[6], lpc.k[7]};
      float e = min(1.0f, lpc.energy / 120.0f);
      for (int c = 0; c < 12; c++) {
        float f = c * 9 / 11.0f; int i = (int)f; float u = f - i; int j = min(9, i + 1);
        row[c] = ((kk[i] * (1 - u) + kk[j] * u) / 128.0f) * (0.4f + 1.2f * e);
      }
    } else for (int c = 0; c < 12; c++) row[c] = prev[c] * 0.6f + random(-10, 10) / 200.0f;
  }
  // the moon: a wire octahedron, turned by the record
  queueMesh(meshes[MS_OCTA], 0, 2.4f, 11, vjT * 0.3f + deckAng, vjT * 0.2f, 0, 1.3f * (1 + 0.2f * kickPulse), 1, 1);
  flushTris();
  for (int r = 0; r < 20; r++) {
    float z = 1.4f + (r + acc) * 0.62f;
    const float* row = terr[(terrHead + r) % 20];
    const float* nxt = terr[(terrHead + r + 1) % 20];
    uint8_t v = (uint8_t)constrain(255 - z * 18, 30.0f, 255.0f);
    for (int c = 0; c < 12; c++) {
      float x = -3.6f + c * 0.65f, y = -1.3f + row[c] * 1.3f;
      if (c < 11) line3(x, y, z, x + 0.65f, -1.3f + row[c + 1] * 1.3f, z, v);
      if (r < 19) line3(x, y, z, x, -1.3f + nxt[c] * 1.3f, z + 0.62f, (uint8_t)(v * 0.6f));
    }
  }
}

void spawnRing(uint8_t kind) {
  for (auto& r : rings) if (!r.on) { r = {14, vjT + random(100) / 50.0f, kind == 1 ? 2.8f : kind == 3 ? 2.0f : kind == 0 ? 3.2f : 2.4f, kind, true}; return; }
}
void sceneBeat(float dt) {
  float spd = bpm / 60.0f * 3.2f;
  for (auto& r : rings) if (r.on) { r.z -= spd * dt * (0.5f + 0.5f * tapeRate); if (r.z < 0.35f) r.on = false; }
  int order[28], n = 0;                              // far to near
  for (int i = 0; i < 28; i++) if (rings[i].on) order[n++] = i;
  for (int i = 1; i < n; i++) { int k = order[i], j = i - 1; while (j >= 0 && rings[order[j]].z < rings[k].z) { order[j + 1] = order[j]; j--; } order[j + 1] = k; }
  // the core at the end of the tunnel
  queueMesh(meshes[MS_OCTA], 0, 0, 12, vjT, vjT * 0.7f, 0, 0.9f + outPeak * 1.5f + kickPulse, 2, 1);
  flushTris();
  for (int o = 0; o < n; o++) {
    Ring& r = rings[order[o]];
    int sides = r.kind == 1 ? 6 : r.kind == 2 ? 4 : r.kind == 4 ? 3 : 8;
    uint8_t v = (uint8_t)constrain(280 - r.z * 18, 40.0f, 255.0f);
    float rr = r.size, rot = r.rot;
    for (int k = 0; k < sides; k++) {
      float a0 = rot + k * 2 * PI / sides, a1 = rot + (k + 1) * 2 * PI / sides;
      if (r.kind == 1) {                             // kick: a solid band
        int x0, y0, x1, y1, x2, y2, x3, y3;
        if (proj(cosf(a0) * rr, sinf(a0) * rr, r.z, x0, y0) && proj(cosf(a1) * rr, sinf(a1) * rr, r.z, x1, y1) &&
            proj(cosf(a1) * rr * 0.8f, sinf(a1) * rr * 0.8f, r.z, x2, y2) && proj(cosf(a0) * rr * 0.8f, sinf(a0) * rr * 0.8f, r.z, x3, y3)) {
          uint8_t sh = (uint8_t)(v * (0.55f + 0.45f * fabsf(cosf(a0 * 2))));
          gTri(x0, y0, x1, y1, x2, y2, sh); gTri(x0, y0, x2, y2, x3, y3, sh);
        }
      } else if (r.kind == 3) {                      // hats: dots
        int x, y;
        if (proj(cosf(a0) * rr, sinf(a0) * rr, r.z, x, y)) { gPix(x, y, v); gPix(x + 1, y, v); gPix(x, y + 1, v); gPix(x + 1, y + 1, v); }
      } else {
        line3(cosf(a0) * rr, sinf(a0) * rr, r.z, cosf(a1) * rr, sinf(a1) * rr, r.z, v);
        if (r.kind == 2) line3(cosf(a0) * rr * 0.6f, sinf(a0) * rr * 0.6f, r.z, cosf(a1) * rr * 0.6f, sinf(a1) * rr * 0.6f, r.z, v);
      }
    }
  }
}

// ---- post effects on the grey frame ----
static void shiftRow(int y, int sh) {
  uint8_t* row = gbuf + y * 128; uint8_t tmp[128];
  for (int x = 0; x < 128; x++) tmp[x] = row[(x - sh + 256) & 127];
  memcpy(row, tmp, 128);
}
void postFx() {
  uint32_t now = millis();
  int f = fx;
  float amt = fxAmt[f];
  if (sliceT > 0) for (int b = 0; b < 4; b++) {      // snare glitch: bands of rows torn sideways
    int y0 = random(0, 120), h = random(2, 10), sh = random(-22, 23);
    for (int y = y0; y < min(128, y0 + h); y++) shiftRow(y, sh);
  }
  if (underOn) for (int y = 0; y < 128; y++) shiftRow(y, (int)(4 * sinf(y * 0.14f + vjT * 3.1f)));   // UNDERWATER sways
  switch (f) {
    case FX_RADIO: {                                 // scanlines + the tuning tears the picture
      for (int y = 1; y < 128; y += 2) { uint8_t* row = gbuf + y * 128; for (int x = 0; x < 128; x++) row[x] >>= 2; }
      int tear = (int)(fabsf(sinf(vjT * 1.7f)) * 10 * (1 - amt) + 1), y0 = (int)(now / 7) % 128;
      for (int y = y0; y < min(128, y0 + 12); y++) shiftRow(y, tear);
      break;
    }
    case FX_CRUSH: {                                 // big pixels, few greys
      int b = 2 + (int)(amt * 6);
      for (int y = 0; y < 128; y += b) for (int x = 0; x < 128; x += b) {
        int v = (gbuf[y * 128 + x] >> 6) * 85;
        for (int yy = y; yy < min(128, y + b); yy++) memset(gbuf + yy * 128 + x, v, min(b, 128 - x));
      }
      break;
    }
    case FX_GRAIN:                                   // pixels scatter
      for (int i = 0; i < 900 + (int)(amt * 2500); i++) {
        int x = random(128), y = random(128), x2 = constrain(x + (int)random(-4, 5), 0, 127), y2 = constrain(y + (int)random(-4, 5), 0, 127);
        std::swap(gbuf[y * 128 + x], gbuf[y2 * 128 + x2]);
      }
      break;
    case FX_DEEP:                                    // dark, with a vignette
      for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) {
        float d = (sq(x - 64) + sq(y - 64)) / 8192.0f;
        gbuf[y * 128 + x] = (uint8_t)(gbuf[y * 128 + x] * max(0.0f, 0.75f - d * 0.7f));
      }
      break;
    case FX_VOX:                                     // kaleidoscope: four-way mirror
      for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        uint8_t v = gbuf[y * 128 + x];
        gbuf[y * 128 + 127 - x] = v; gbuf[(127 - y) * 128 + x] = v; gbuf[(127 - y) * 128 + 127 - x] = v;
      }
      break;
    case FX_CHOIR:                                   // particles drifting up through it
      for (auto& p : parts) {
        p.y += 0.02f + amt * 0.04f; p.z -= 0.03f;
        if (p.y > 3 || p.z < 0.6f) { p.x = random(-300, 300) / 100.0f; p.y = -3; p.z = random(100, 1200) / 100.0f; }
        int x, y;
        if (proj(p.x, p.y, p.z, x, y)) {
          uint8_t v = (uint8_t)constrain(300 - p.z * 20, 60.0f, 255.0f);
          gPix(x, y, v); if (p.z < 4) { gPix(x + 1, y, v); gPix(x, y + 1, v); }
        }
      }
      break;
  }
  if (tapeOn || tapeRate < 0.97f)                    // TAPE: the picture sags and smears down
    for (int y = 127; y > 0; y--) { uint8_t* row = gbuf + y * 128; const uint8_t* up = row - 128;
      for (int x = 0; x < 128; x++) row[x] = max(row[x], (uint8_t)(up[x] * 0.8f)); }
  if (flashV > 0) { int a = (int)(flashV * 255); for (int i = 0; i < 128 * 128; i++) gbuf[i] = min(255, gbuf[i] + a); }
  if (reverseOn) for (int i = 0; i < 128 * 128; i++) gbuf[i] = 255 - gbuf[i];
}

void vjDither() {                                    // Bayer 8x8 straight into u8g2's buffer (R1: (x,y) = bit x&7 of buf[(x>>3)*128 + 127-y])
  static const uint8_t B8[64] = {0, 32, 8, 40, 2, 34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26, 12, 44, 4, 36, 14, 46, 6, 38, 60, 28, 52, 20, 62, 30, 54, 22,
                                 3, 35, 11, 43, 1, 33, 9, 41, 51, 19, 59, 27, 49, 17, 57, 25, 15, 47, 7, 39, 13, 45, 5, 37, 63, 31, 55, 23, 61, 29, 53, 21};
  uint8_t* buf = oled.getBufferPtr();
  for (int y = 0; y < 128; y++) {
    const uint8_t* row = gbuf + y * 128;
    const uint8_t* th = B8 + (y & 7) * 8;
    uint8_t* col = buf + 127 - y;
    for (int k = 0; k < 16; k++) {
      uint8_t b = 0;
      const uint8_t* r8 = row + k * 8;
      for (int j = 0; j < 8; j++) if (r8[j] > th[j] * 4 + 2) b |= 1 << j;
      col[k * 128] = b;
    }
  }
}

// =====================================================================
// The intro film in 3D: same clock and the same sound cues as the flat version (drawSplash, kept for reference):
// particles stream in from all round while the camera swings in -> the low-poly cyclops (visor, shoulders) ->
// his eye charges -> a turntable rises and spins -> the eye LASER cuts the grooves (sparks, ZAPs) -> the laser is the
// stylus for the name cut routine (record turns with the audio, camera whips with the scratches, kicks punch in,
// snares tear the picture, every cut doubles the head) -> crash white-out -> the laser burns CYBERSKRATCH in while
// the camera drifts away. Motion-blur trails all the way.
// =====================================================================
// ---- the 2D cyclops as a standing card in the 3D scene: drawHead() is painted into a texture every frame
// (so the eye charges and the glint slides), then the card is drawn with a perspective-correct plane mapping ----
static uint8_t headTex[72 * 56];                     // 1 = see-through
const int TX0 = 28, TY0 = 3, TW = 72, TH = 56;
// he sits behind the decks: big (x1.95), set back so the platter covers his shoulders
const float CARD_W = 3.36f, CARD_H = 2.62f, CARD_Y = 0.94f, CARD_Z = 5.4f, CARD_SC = 1.95f;
void paintHeadTex(float fade, float eye, float t) {
  memcpy(vtmp, gbuf, sizeof vtmp);
  memset(gbuf, 1, sizeof gbuf);
  float cs = camS, cx = camX, cy = camY, sqy = sqY; int shx = shakeX, shy = shakeY;
  camS = 1; camX = 64; camY = 64; shakeX = shakeY = 0; sqY = 1;
  drawHead(fade, eye, t);
  for (int y = 0; y < TH; y++) memcpy(headTex + y * TW, gbuf + (TY0 + y) * 128 + TX0, TW);
  camS = cs; camX = cx; camY = cy; sqY = sqy; shakeX = shx; shakeY = shy;
  memcpy(gbuf, vtmp, sizeof vtmp);
}
// the card's top-left corner and its right / down edges in the world
static inline void cardFrame(float cx, float yaw, float sc, V3& C, V3& U, V3& V) {
  float W = CARD_W * sc * CARD_SC, H = CARD_H * sc * CARD_SC, ux = cosf(yaw), uz = sinf(yaw);
  C = {cx - ux * W / 2, CARD_Y + H / 2, CARD_Z - uz * W / 2};
  U = {ux * W, 0, uz * W}; V = {0, -H, 0};
}
static inline void cardPoint(float cx, float yaw, float sc, float u, float v, float& x, float& y, float& z) {
  V3 C, U, V; cardFrame(cx, yaw, sc, C, U, V);
  x = C.x + U.x * u + V.x * v; y = C.y + U.y * u + V.y * v; z = C.z + U.z * u + V.z * v;
}
void drawCard(float cx, float yaw, float sc, float bright) {
  V3 C, U, V; cardFrame(cx, yaw, sc, C, U, V);
  V3 A = C, B = {C.x + U.x, C.y + U.y, C.z + U.z}, D = {C.x + V.x, C.y + V.y, C.z + V.z};
  w2c(A.x, A.y, A.z); w2c(B.x, B.y, B.z); w2c(D.x, D.y, D.z);                 // into camera space
  V3 Uc = {B.x - A.x, B.y - A.y, B.z - A.z}, Vc = {D.x - A.x, D.y - A.y, D.z - A.z};
  float nx = Uc.y * Vc.z - Uc.z * Vc.y, ny = Uc.z * Vc.x - Uc.x * Vc.z, nz = Uc.x * Vc.y - Uc.y * Vc.x;
  float nC = nx * A.x + ny * A.y + nz * A.z, uu = Uc.x * Uc.x + Uc.y * Uc.y + Uc.z * Uc.z, vv = Vc.x * Vc.x + Vc.y * Vc.y + Vc.z * Vc.z;
  int x0 = 128, y0 = 128, x1 = -1, y1 = -1;
  for (int k = 0; k < 4; k++) {                      // screen bounds of the four corners
    float px = A.x + ((k & 1) ? Uc.x : 0) + ((k & 2) ? Vc.x : 0), py = A.y + ((k & 1) ? Uc.y : 0) + ((k & 2) ? Vc.y : 0),
          pz = A.z + ((k & 1) ? Uc.z : 0) + ((k & 2) ? Vc.z : 0);
    if (pz < 0.3f) return;
    int sx = 64 + (int)(FOC * px / pz), sy = 64 - (int)(FOC * py / pz);
    x0 = min(x0, sx); x1 = max(x1, sx); y0 = min(y0, sy); y1 = max(y1, sy);
  }
  x0 = max(0, x0); y0 = max(0, y0); x1 = min(127, x1); y1 = min(127, y1);
  for (int y = y0; y <= y1; y++) {
    float dy = -(y - 64) / FOC;
    for (int x = x0; x <= x1; x++) {
      float dx = (x - 64) / FOC, den = nx * dx + ny * dy + nz;
      if (fabsf(den) < 1e-6f) continue;
      float t = nC / den, rx = t * dx - A.x, ry = t * dy - A.y, rz = t - A.z;
      float u = (rx * Uc.x + ry * Uc.y + rz * Uc.z) / uu, v = (rx * Vc.x + ry * Vc.y + rz * Vc.z) / vv;
      if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
      uint8_t tv = headTex[(int)(v * TH) * TW + (int)(u * TW)];
      if (tv == 1) continue;
      gbuf[y * 128 + x] = (uint8_t)min(255.0f, tv * bright);
    }
  }
}

struct Spark { float x, y, z, vx, vy, vz, life; };
static Spark sparks[48];
static float starP[60][3], partStart[150][3], partEnd[150][3];
static float headYaw = 0, headPitch = 0;
static const float HX = 0, HY = 0.95f, HZ = 4.3f, HSC = 1.35f, PY = -1.75f, PZ = 4.6f;
static inline void headPt(float x, float y, float z, float& ox, float& oy, float& oz) {   // head space -> world
  float c = cosf(headYaw), s = sinf(headYaw);
  float x2 = x * c + z * s, z2 = -x * s + z * c;
  float cp = cosf(headPitch), sp = sinf(headPitch);
  float y2 = y * cp - z2 * sp; z2 = y * sp + z2 * cp;
  ox = HX + x2 * HSC; oy = HY + y2 * HSC; oz = HZ + z2 * HSC;
}
static inline bool projW(float x, float y, float z, int& sx, int& sy) { w2c(x, y, z); return proj(x, y, z, sx, sy); }
void glow2(int cx, int cy, float r, float inten) {
  int R = (int)r + 1;
  for (int y = max(0, cy - R); y <= min(127, cy + R); y++) for (int x = max(0, cx - R); x <= min(127, cx + R); x++) {
    float d = sqrtf((float)sq(x - cx) + sq(y - cy)) / r;
    if (d < 1) { int v = gbuf[y * 128 + x] + (int)(255 * inten * (1 - d) * (1 - d)); gbuf[y * 128 + x] = min(255, v); }
  }
}
void eyeFlare(int x, int y, float eye) {             // the eye: a hot core and an anamorphic streak across the shades
  glow2(x, y, 3 + 4 * eye, 1.6f);
  int L = (int)(10 + 26 * eye);
  for (int dx = -L; dx <= L; dx++) {
    float f = 1 - fabsf((float)dx) / L;
    uint8_t v = (uint8_t)(255 * f * f);
    gPix(x + dx, y, v);
    if (fabsf((float)dx) < L * 0.4f) { gPix(x + dx, y - 1, v / 2); gPix(x + dx, y + 1, v / 2); }
  }
  for (int dy = -4; dy <= 4; dy++) gPix(x, y + dy, (uint8_t)(255 - abs(dy) * 50));
}
void thickLine(int x0, int y0, int x1, int y1, uint8_t core, uint8_t halo) {
  for (int o = -2; o <= 2; o++) if (o) { gLine(x0 + o, y0, x1 + o, y1, halo, false); gLine(x0, y0 + o, x1, y1 + o, halo, false); }
  gLine(x0, y0, x1, y1, core, true); gLine(x0 + 1, y0, x1 + 1, y1, core, true);
}
void spawnSparks(float x, float y, float z, int n) {
  for (int k = 0; k < n; k++) for (auto& s : sparks) if (s.life <= 0) {
    s = {x, y, z, random(-100, 101) / 60.0f, random(40, 160) / 60.0f, random(-100, 101) / 60.0f, random(25, 60) / 100.0f};
    break;
  }
}
void splash3DInit() {
  for (auto& s : starP) { s[0] = random(-400, 401) / 100.0f; s[1] = random(-400, 401) / 100.0f; s[2] = random(50, 1400) / 100.0f; }
  headYaw = 0; headPitch = 0;
  for (int k = 0; k < 150; k++) {                    // targets: the cyclops' outline (head + shades) on the card
    float u = (film.face[k][0] - TX0) / TW, v = (film.face[k][1] - TY0) / TH;
    cardPoint(0, 0, 1, u, v, partEnd[k][0], partEnd[k][1], partEnd[k][2]);
    float th = random(628) / 100.0f, ph = random(-120, 121) / 100.0f, r = random(500, 900) / 100.0f;
    partStart[k][0] = cosf(th) * cosf(ph) * r; partStart[k][1] = sinf(ph) * r; partStart[k][2] = CPIVZ + sinf(th) * cosf(ph) * r;
  }
  for (auto& s : sparks) s.life = 0;
  memset(gbuf, 0, sizeof gbuf);
}

void drawSplash3D() {
  uint32_t now = millis();
  if (!splashT0) { splashT0 = now; splashLast = now; splashEnd = now + 17000; filmInit(); splash3DInit(); }
  float dt = min(0.1f, (now - splashLast) * 0.001f);
  splashLast = now;
  float u = (now - splashT0) * 0.001f;
  vjT += dt;
  float burnT = routineEndAt ? (now - routineEndAt) * 0.001f : -1;
  int rs = routineStepVis;
  bool crash = rs >= ROUTINE_LEN - 1;

  // what the drums did (the routine plays its own boom bap)
  float kE = max(perc[D_KICK].env, perc[D_808].env), sE = max(perc[D_SNARE].env, perc[D_CLAP].env);
  if (kE > 0.45f && kE > lastEnv[0] * 1.3f + 0.05f) kickPulse = 1;
  if (sE > 0.35f && sE > lastEnv[1] * 1.3f + 0.05f) sliceT = 0.1f;
  lastEnv[0] = kE; lastEnv[1] = sE;
  kickPulse *= expf(-dt * 7); sliceT -= dt; splitT -= dt;
  if (wordTrig != seenTrig) { seenTrig = wordTrig; if (routineOn) splitT = 0.2f; }

  // the record turns: by itself while cutting, with the deck during the routine
  static float whip = 0;
  if (routineOn && deck.on && deck.word >= NAME_WORD) {
    float p = deckVis;
    if (deck.word == lastDeckWord && lastDeckVis >= 0 && p >= 0 && fabsf(p - lastDeckVis) < FS * 0.15f) {
      float d = (p - lastDeckVis) / FS * 2 * PI * 0.55f;
      recAng += d; whip += d * 0.35f;
    }
    lastDeckVis = p; lastDeckWord = deck.word;
  } else { recAng += dt * 2 * PI * 0.55f; lastDeckVis = -1; }
  whip *= expf(-dt * 4);

  // ---- the camera ----
  float e = 1 - powf(1 - smooth01((u - 0.2f) / 1.6f), 3);   // particles home in
  float yaw, pitch, dist;
  if (u < 1.8f) { yaw = 1.5f * sq(1 - e); pitch = 0.4f * (1 - e); dist = 4.5f * (1 - e); }
  else if (burnT < 0) {
    float k = min(1.0f, u - 1.8f);
    yaw = (0.22f * sinf(u * 0.7f)) * k + whip; pitch = 0.06f * sinf(u * 0.5f) - 0.12f * kickPulse; dist = -0.55f * kickPulse;
  } else { yaw = 0.22f * sinf(u * 0.7f); pitch = 0.06f * sinf(u * 0.5f); dist = 0; }   // no pull-out: he just skews
  setCam(yaw, pitch, dist);
  camRoll = 0; camCR = 1; camSR = 0;                 // (the card mapping assumes no roll)
  headYaw = routineOn ? 0.18f * sinf(u * 2.3f) : 0.08f * sinf(u * 0.9f);
  headPitch = routineOn ? 0.1f * kickPulse : 0;

  // ---- the frame (trails: the last frame fades instead of clearing) ----
  for (int i = 0; i < 128 * 128; i++) gbuf[i] = (uint8_t)(gbuf[i] * 0.45f);
  if (u < 2.6f) for (auto& s : starP) {              // warp stars, fading as the scene settles
    float z0 = s[2], z1 = s[2] + 0.35f;
    s[2] -= dt * 9;
    if (s[2] < 0.4f) { s[0] = random(-400, 401) / 100.0f; s[1] = random(-400, 401) / 100.0f; s[2] = 14; }
    line3(s[0], s[1], z0, s[0], s[1], z1, (uint8_t)(220 * max(0.0f, 1 - u / 2.6f)));
  }
  float headFade = u < 1.3f ? 0 : min(1.0f, (u - 1.3f) / 0.5f);
  float slide = (1 - smooth01((u - 2.2f) / 0.9f)) * -3.0f;
  float deckFade = u > 2.2f ? min(1.0f, (u - 2.2f) / 0.5f) : 0;
  float py = PY + slide;
  if (deckFade > 0) queueMesh(meshes[MS_PLATTER], 0, py, PZ, recAng, 0, 0, 1, 0, 0.35f * deckFade);
  float cardYaw = 0.22f * sinf(u * 0.8f) + whip * 1.5f + (burnT >= 0 ? min(0.55f, burnT * 0.35f) : 0), cardSc = 1 + 0.08f * kickPulse;
  float eyeNow = routineOn ? 0.7f + 0.6f * outPeak : min(1.0f, max(0.0f, (u - 1.8f) / 0.6f));
  if (false) {                                       // (the glitch sphere: retired, the 2D cyclops card is back)
    static Mesh gm;
    const Mesh& base = meshes[MS_SPHERE];
    gm = base;
    float charge = min(1.0f, max(0.0f, (u - 1.8f) / 0.6f));
    float amt = 0.05f + 0.06f * charge + 0.35f * kickPulse + (sliceT > 0 ? 0.12f : 0);
    int band = random(-3, 3);                        // one band of latitude slips sideways
    float slip = (random(3) == 0) ? random(-30, 31) / 100.0f : 0;
    for (int i = 0; i < gm.nv; i++) {
      float n = 1 + amt * (random(-100, 101) / 100.0f) + 0.06f * sinf(u * 5 + i * 1.7f);
      gm.v[i].x *= n; gm.v[i].y *= n; gm.v[i].z *= n;
      if ((int)floorf(gm.v[i].y * 4) == band) gm.v[i].x += slip;
    }
    float hb = headFade * (0.75f + 0.35f * charge + (routineOn ? 0.3f * outPeak : 0));
    bool wire = sliceT > 0 || random(14) == 0;      // snares (and now and then) flip it to wireframe
    float bob = 0.06f * sinf(u * 1.6f);
    queueMesh(gm, HX, HY + bob, HZ, headYaw + u * 0.4f, headPitch + u * 0.23f, 0, HSC, wire ? 1 : 0, hb);
    if (splitT > 0) queueMesh(gm, HX + (random(2) ? 0.4f : -0.4f), HY + bob, HZ, headYaw + u * 0.4f + 0.3f, headPitch, 0, HSC * 1.06f, 1, 1);
  }
  if (headFade > 0) {                                // the cyclops card behind the decks (doubled for a moment on every cut)
    paintHeadTex(headFade, eyeNow, u);
    if (splitT > 0) drawCard(random(2) ? 0.45f : -0.45f, cardYaw + 0.2f, cardSc * 1.04f, 0.55f);
    drawCard(0, cardYaw, cardSc, 1);
  }
  flushTris();                                       // the platter in front of him
  if (false) {                                       // (the 3D cyclops' shades and mouth: retired)
    uint8_t rv = (uint8_t)(235 * headFade);
    for (int i = 0; i < 8; i++) {
      float a0 = -1.05f + i * 2.1f / 8, a1 = -1.05f + (i + 1) * 2.1f / 8, x0, y0, z0, x1, y1, z1;
      headPt(sinf(a0) * 0.65f, 0.2f, -cosf(a0) * 0.65f, x0, y0, z0); headPt(sinf(a1) * 0.65f, 0.2f, -cosf(a1) * 0.65f, x1, y1, z1);
      line3(x0, y0, z0, x1, y1, z1, rv);
      headPt(sinf(a0) * 0.66f, -0.06f, -cosf(a0) * 0.66f, x0, y0, z0); headPt(sinf(a1) * 0.66f, -0.06f, -cosf(a1) * 0.66f, x1, y1, z1);
      line3(x0, y0, z0, x1, y1, z1, rv);
    }
    float g = fmodf(u * 0.6f, 1.6f);
    if (g < 1) {
      float ag = -0.9f + g * 1.8f, x0, y0, z0, x1, y1, z1;
      headPt(sinf(ag) * 0.66f, 0.18f, -cosf(ag) * 0.66f, x0, y0, z0); headPt(sinf(ag - 0.15f) * 0.67f, -0.04f, -cosf(ag - 0.15f) * 0.67f, x1, y1, z1);
      line3(x0, y0, z0, x1, y1, z1, 255);
    }
    float mx0, my0, mz0, mx1, my1, mz1; int a, b, c, d;
    headPt(-0.18f, -0.42f, -0.55f, mx0, my0, mz0); headPt(0.18f, -0.42f, -0.55f, mx1, my1, mz1);
    if (projW(mx0, my0, mz0, a, b) && projW(mx1, my1, mz1, c, d)) { gLine(a, b, c, d, 0, true); gLine(a, b + 1, c, d + 1, 0, true); }
  }
  if (deckFade > 0) {                                // rim, grooves (where the laser has burned them), the label
    uint8_t rimV = (uint8_t)(200 * deckFade);
    float pyT = py + 0.08f;
    for (int i = 0; i < 24; i++) {
      float a0 = i * 2 * PI / 24, a1 = (i + 1) * 2 * PI / 24;
      line3(cosf(a0) * 1.9f, pyT, PZ + sinf(a0) * 1.9f, cosf(a1) * 1.9f, pyT, PZ + sinf(a1) * 1.9f, rimV);
    }
    float burn = u > 3.2f ? 1.8f - 1.25f * smooth01((u - 3.2f) / 1.3f) : 99;
    for (int k = 0; k < 6; k++) {
      float r = 1.75f - k * 0.2f;
      if (r < burn) continue;
      for (int i = 0; i < 24; i++) {
        float a0 = i * 2 * PI / 24, a1 = (i + 1) * 2 * PI / 24;
        line3(cosf(a0) * r, pyT, PZ + sinf(a0) * r, cosf(a1) * r, pyT, PZ + sinf(a1) * r, (uint8_t)(170 * deckFade));
      }
    }
    for (int k = 0; k < 10; k++) {                   // the label: a five-point flower turning with the record
      float a0 = recAng + k * PI / 5, a1 = recAng + (k + 1) * PI / 5, r0 = (k & 1) ? 0.18f : 0.5f, r1 = (k & 1) ? 0.5f : 0.18f;
      line3(cosf(a0) * r0, pyT, PZ + sinf(a0) * r0, cosf(a1) * r1, pyT, PZ + sinf(a1) * r1, (uint8_t)(230 * deckFade));
    }
    // ---- the laser ----
    float ex, ey, ez;
    cardPoint(0, cardYaw, cardSc, (64.0f - TX0) / TW, (headSY(EYE_Y) - TY0) / TH, ex, ey, ez);   // his eye, on the card
    int esx, esy;
    bool eyeOn = projW(ex, ey, ez, esx, esy);
    bool cutting = u > 3.2f && u < 4.6f;
    float lr = -1, power = 1;
    if (cutting) { lr = burn; if (now - lastZap > 260) { lastZap = now; zapReq = true; } }
    else if (routineOn && deck.on && deck.word >= NAME_WORD && gateVis > 0.4f && bank[deck.word].len) {
      float pos = constrain((float)deckVis / bank[deck.word].len, 0.0f, 1.0f);
      lr = 1.7f - 1.0f * pos; power = 0.6f + 0.6f * gateVis;
    }
    if (lr > 0 && eyeOn) {
      const float A0 = -1.05f;                       // the spot sits front-right on the record
      float sx = cosf(A0) * lr, sz = PZ + sinf(A0) * lr;
      int tx, ty;
      if (projW(sx, pyT, sz, tx, ty)) {
        thickLine(esx, esy, tx, ty, 255, (uint8_t)(110 * power));
        glow2(tx, ty, 7 * power, 0.9f);
        spawnSparks(sx, pyT, sz, 3);
        if (random(4) == 0) sliceT = max(sliceT, 0.03f);
      }
    }
    if (burnT >= 0 && burnT < 0.9f && eyeOn) {       // ...and burns the name in
      oled.setFont(u8g2_font_helvB10_tr);
      int w = oled.getStrWidth("CYBERSKRATCH");
      thickLine(esx, esy, 64 - w / 2 + (int)(w * burnT / 0.9f), 120, 255, 110);
      if (now - lastZap > 200) { lastZap = now; zapReq = true; }
    }
    // the eye itself
    if (eyeOn) {
      float eye = min(1.0f, max(0.0f, (u - 1.8f) / 0.6f));
      if (routineOn) eye = 0.7f + 0.6f * outPeak;
      if (eye > 0 && lr > 0) glow2(esx, esy, 3 + 4 * eye, 1.6f);   // where the beam leaves the sphere
    }
  } else if (u > 1.8f) {                             // eye charging before the deck arrives
    float ex, ey, ez; int esx, esy;
    headPt(0, 0.07f, -0.67f, ex, ey, ez);
    float eye = min(1.0f, (u - 1.8f) / 0.6f);
    (void)eye; (void)esx; (void)esy;
  }
  // sparks
  for (auto& s : sparks) if (s.life > 0) {
    s.life -= dt; s.vy -= dt * 6; s.x += s.vx * dt; s.y += s.vy * dt; s.z += s.vz * dt;
    int x, y;
    if (projW(s.x, s.y, s.z, x, y)) { gPix(x, y, 255); gPix(x + 1, y, 200); }
  }
  // particles homing in on the head
  if (u < 1.9f) for (int k = 0; k < 150; k++) {
    float x = partStart[k][0] + (partEnd[k][0] - partStart[k][0]) * e, y = partStart[k][1] + (partEnd[k][1] - partStart[k][1]) * e,
          z = partStart[k][2] + (partEnd[k][2] - partStart[k][2]) * e;
    int sx, sy;
    if (projW(x, y, z, sx, sy)) { gPix(sx, sy, 255); gPix(sx + 1, sy, 255); gPix(sx, sy + 1, 200); }
  }

  if (u >= 4.8f && !splashCuts) { splashCuts = true; routineReq = true; }   // the name cut routine
  if (splashCuts && !routineOn && !routineReq && !routineEndAt) { routineEndAt = now; splashEnd = now + 3200; }
  if (crash) memset(gbuf, 255, sizeof gbuf);         // white-out on the crash
  else if (burnT >= 0) {                             // scorch glow along the name while it burns
    oled.setFont(u8g2_font_helvB10_tr);
    int w = oled.getStrWidth("CYBERSKRATCH"), lit = 64 - w / 2 + (int)(w * min(1.0f, burnT / 0.9f));
    for (int y = 108; y < 128; y++) {
      uint8_t v = g8(0.16f * expf(-sq((y - 120) / 6.0f)));
      for (int x = 0; x < lit && x < 128; x++) if (v > gbuf[y * 128 + x]) gbuf[y * 128 + x] = v;
    }
  }
  if (sliceT > 0) for (int b = 0; b < 3; b++) {      // glitch: torn rows
    int y0 = random(0, 120), h = random(2, 8), sh = random(-18, 19);
    for (int y = y0; y < min(128, y0 + h); y++) shiftRow(y, sh);
  }
  vjDither();

  if (burnT >= 0 && !crash) {                        // CYBERSKRATCH, burned in up to where the laser is
    oled.setFont(u8g2_font_helvB10_tr);
    const char* T = "CYBERSKRATCH";
    int w = oled.getStrWidth(T), x0 = 64 - w / 2, lit = x0 + (int)(w * min(1.0f, burnT / 0.9f));
    oled.setDrawColor(0);
    for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) oled.drawStr(x0 + dx, 125 + dy, T);
    oled.setDrawColor(1);
    oled.drawStr(x0, 125, T);
    oled.setDrawColor(0); if (lit < 128) oled.drawBox(lit, 112, 128 - lit, 16); oled.setDrawColor(1);
    oled.setFont(u8g2_font_4x6_tf);
    if (burnT > 1.6f && ((now / 500) & 1)) {
      const char* pr = "PRESS ANY KEY";
      int pw = oled.getStrWidth(pr);
      oled.setDrawColor(0); oled.drawBox(64 - pw / 2 - 2, 58, pw + 4, 8); oled.setDrawColor(1);
      oled.drawStr(64 - pw / 2, 64, pr);
    }
  }
  if (showCredit && u > 5.0f && !crash) {
    oled.setFont(u8g2_font_4x6_tf);
    const char* cr = "MARK HELLAR + CLAUDE";
    int cw = oled.getStrWidth(cr);
    oled.setDrawColor(0); oled.drawBox(64 - cw / 2 - 2, 0, cw + 4, 7); oled.setDrawColor(1);
    oled.drawStr(64 - cw / 2, 6, cr);
  }
  if (bankBusy) oled.drawHLine(34, 127, 60 * renderProgress / NWORDS);   // voices still loading
}

void drawVJ() {
  setCam(0, 0, 0);
  static uint32_t last = millis();
  uint32_t now = millis();
  float dt = min(0.1f, (now - last) * 0.001f); last = now;
  vjT += dt * (0.4f + 0.6f * tapeRate);
  // ---- what the music did since the last frame ----
  float kE = max(perc[D_KICK].env, perc[D_808].env), sE = max(perc[D_SNARE].env, max(perc[D_CLAP].env, perc[D_RIM].env));
  float hE = max(perc[D_HAT].env, perc[D_OPEN].env), cE = perc[D_CRASH].env;
  float oE = max(max(perc[D_TOML].env, perc[D_TOMH].env), max(perc[D_COWBL].env, perc[D_ZAP].env));
  bool kick = kE > 0.45f && kE > lastEnv[0] * 1.3f + 0.05f, snare = sE > 0.35f && sE > lastEnv[1] * 1.3f + 0.05f;
  bool hat = hE > 0.3f && hE > lastEnv[2] * 1.3f + 0.05f, other = oE > 0.35f && oE > lastEnv[3] * 1.3f + 0.05f;
  bool crash = cE > 0.5f && cE > lastEnv[4] * 1.3f + 0.05f;
  lastEnv[0] = kE; lastEnv[1] = sE; lastEnv[2] = hE; lastEnv[3] = oE; lastEnv[4] = cE;
  if (kick) { kickPulse = 1; if (mode == M_BEAT) spawnRing(1); }
  if (snare) { sliceT = 0.12f; if (mode == M_BEAT) spawnRing(2); }
  if (hat && mode == M_BEAT) spawnRing(3);
  if (other && mode == M_BEAT) spawnRing(4);
  if (crash || routineStepVis >= ROUTINE_LEN - 1) flashV = 1;
  if (mode == M_BEAT && curStep != lastStepVJ) { lastStepVJ = curStep; if ((curStep & 1) == 0) spawnRing(0); }
  kickPulse *= expf(-dt * 7); sliceT -= dt; flashV = max(0.0f, flashV - dt * 4); splitT -= dt;
  if (wordTrig != seenTrig) {                        // a new word: double image + its name
    seenTrig = wordTrig; splitT = 0.22f; labelT = vjT;
    int tw = trigWord;
    const char* s = trigSrc == SRC_TALK ? talk[talkBank][constrain(tw, 0, 11)].name : (tw >= 0 && tw < NBANKW ? bank[tw].text : "");
    if (trigSrc == SRC_SAM && tw >= NAME_WORD) s = "CYBERSKRATCH";
    if (!strncmp(s, "SIGHBER", 7)) snprintf(vjLabel, sizeof vjLabel, "CYBER%s", s + 7);   // SAM's spelling -> the real word
    else { strncpy(vjLabel, s, sizeof vjLabel - 1); vjLabel[sizeof vjLabel - 1] = 0; }
  }
  // the record turns the world: deck position = angle; scratching whips it, a backspin spins it
  float dv = deckVis;
  int dw = deck.word;
  if (dv >= 0 && dw >= 0 && bank[dw].len) {
    float a = dv / bank[dw].len * 2 * PI;
    if (lastDeck >= 0) { float d = a - lastDeck; if (fabsf(d) < 3) { deckAng += d; rollV += d * 0.5f; } }
    lastDeck = a;
  } else lastDeck = -1;
  rollV *= expf(-dt * 3);
  camRoll = 0.12f * sinf(vjT * 0.4f) + rollV + (mode == M_BEAT ? vjT * 0.2f + deckAng * 0.5f : 0);
  camCR = cosf(camRoll); camSR = sinf(camRoll);

  // ---- the frame: STUTTER freezes it, INFINITE zooms the last one, ECHO leaves trails ----
  if (stutterOn) {
    if (!frozen) { memcpy(vtmp, gbuf, sizeof vtmp); frozen = true; }
    memcpy(gbuf, vtmp, sizeof vtmp);
    if ((curStep & 1) == 0) sliceT = 0.05f;
  } else {
    frozen = false;
    if (revFreeze) {                                 // feedback tunnel
      memcpy(vtmp, gbuf, sizeof vtmp);
      for (int y = 0; y < 128; y++) {
        int sy = 64 + (int)((y - 64) * 0.93f);
        for (int x = 0; x < 128; x++) { int sx = 64 + (int)((x - 64) * 0.93f); gbuf[y * 128 + x] = (uint8_t)(vtmp[sy * 128 + sx] * 0.86f); }
      }
    } else if (fx == FX_ECHO) {
      float k = 0.55f + 0.4f * fxAmt[FX_ECHO];
      for (int i = 0; i < 128 * 128; i++) gbuf[i] = (uint8_t)(gbuf[i] * k);
    } else memset(gbuf, 0, 128 * 128);
    if (mode == M_DJ) sceneDJ(dt); else if (mode == M_TALK) sceneTalk(dt); else sceneBeat(dt);
  }
  postFx();
  vjDither();
  // ---- a little text on top ----
  oled.setFont(u8g2_font_4x6_tf);
  char t[24]; snprintf(t, sizeof t, "%s %s %d", MODE_NAMES[mode], FX_NAMES[fx], (int)bpm);
  int w = oled.getStrWidth(t);
  oled.setDrawColor(0); oled.drawBox(0, 0, w + 3, 7); oled.setDrawColor(1); oled.drawStr(1, 6, t);
  for (int i = 0; i < 4; i++) { int bx = 108 + i * 5; if (playing && i == curStep / 4) oled.drawBox(bx, 1, 4, 4); else oled.drawFrame(bx, 1, 4, 4); }
  if (rec && ((now / 300) & 1)) oled.drawDisc(102, 3, 2);
  float age = vjT - labelT;
  if (age < 1.1f && vjLabel[0]) {
    oled.setFont(u8g2_font_helvB10_tr);
    if (oled.getStrWidth(vjLabel) > 122) oled.setFont(u8g2_font_6x10_tf);
    if (oled.getStrWidth(vjLabel) > 122) oled.setFont(u8g2_font_4x6_tf);
    int lw = oled.getStrWidth(vjLabel), lx = 64 - lw / 2 + (age < 0.25f ? (int)random(-4, 5) : 0);
    oled.setDrawColor(0); oled.drawBox(lx - 3, 110, lw + 6, 16); oled.setDrawColor(1);
    oled.drawStr(lx, 123, vjLabel);
    if (age < 0.25f) { oled.setDrawColor(2); oled.drawBox(lx - 3, 110 + random(0, 10), lw + 6, random(2, 5)); oled.setDrawColor(1); }
  }
}

void drawKnobInd() {
  if (!knobAt || millis() - knobAt > 1500) return;
  oled.setFont(u8g2_font_4x6_tf);
  int w = oled.getStrWidth(knobLbl);
  oled.setDrawColor(0); oled.drawBox(0, 0, w + 3, 7); oled.setDrawColor(1);
  oled.drawStr(1, 6, knobLbl);
  int h = (int)(94 * knobLvl);
  oled.setDrawColor(0); oled.drawBox(121, 9, 7, 100); oled.setDrawColor(1);
  oled.drawFrame(122, 10, 5, 98);
  oled.drawBox(123, 106 - h, 3, h + 1);
}

// SAM renders its words on a background task during the film (the splash keeps animating)
void bankTask(void*) { renderBank(voiceIdx, false); vTaskDelete(nullptr); }

void drawFrame() {
  oled.clearBuffer();
  if (eggReq || replayReq) {                         // replay the film (MODE+SHIFT = with credits)
    showCredit = eggReq; eggReq = false; replayReq = false;
    splashOn = true; splashT0 = splashEnd = 0; splashKeysClear = false; splashCuts = false;
  }
  if (splashOn) {
    if (!keysVis) splashKeysClear = true;
    if (splashEnd && (millis() > splashEnd || (keysVis && splashKeysClear))) splashOn = false;
    else { drawSplash3D(); present(); return; }
  }
  if (bankBusy) {
    tab(0, 0, "CYBERSKRATCH");
    oled.setFont(u8g2_font_4x6_tf);
    oled.drawStr(0, 30, "> VOICE CORE");
    char v[24]; snprintf(v, sizeof v, "> LOADING %s", VOICES[voiceIdx].name);
    oled.drawStr(0, 40, v);
    char wtxt[32]; snprintf(wtxt, sizeof wtxt, "> %s", bank[min(renderProgress, NWORDS - 1)].text);
    oled.drawStr(0, 50, wtxt);
    brackets(10, 60, 108, 14);
    int segs = 20 * renderProgress / NWORDS;
    for (int i = 0; i < 20; i++) { if (i < segs) oled.drawBox(13 + i * 5, 63, 4, 8); else oled.drawPixel(15 + i * 5, 67); }
    present();
    return;
  }
  int hm = heldModVis;
  if (hm >= 0) { drawMenu(hm); if (millis() - msgAt < 700) drawAlert(msg); present(); return; }
  if (viewReq) { viewReq = false; vjView = !vjView; flash(vjView ? "VJ VIEW" : "HUD VIEW"); }
  if (vjView) { drawVJ(); drawKnobInd(); if (millis() - msgAt < 900) drawAlert(msg); present(); return; }
  drawTopBar();
  drawSigPane();
  drawPadPane();
  drawVizStrip();
  drawReadout();
  drawSeqPane();
  drawChips();
  if (millis() - msgAt < 900) drawAlert(msg);
  drawKnobInd();
  present();
}

// =====================================================================
void setup() {
  Serial.setTxTimeoutMs(0);
  Serial.begin(115200);
  delay(300);
  for (int c = 0; c < 4; c++) pinMode(COL_PINS[c], INPUT_PULLUP);
  for (int r = 0; r < 4; r++) pinMode(ROW_PINS[r], INPUT);
  pinMode(PIN_POT_HI, OUTPUT); digitalWrite(PIN_POT_HI, HIGH);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_POT, ADC_11db);
  for (int i = 0; i <= 1024; i++) sineTab[i] = sinf(2.0f * PI * i / 1024.0f);
  for (int p = 0; p < 12; p++) { padMenu[p] = -1; padMode[p] = M_DJ; }
  for (int g = 0; g < NGROOVES; g++) for (int s = 0; s < 16; s++) {
    uint16_t b = 0;
    for (int l = 0; l < 4; l++) if (GROOVES[g].lane[l][s] == 'x') b |= 1 << GROOVES[g].drum[l];
    grooveMask[g][s] = b;
  }
  rgbLedWrite(PIN_RGB, 0, 0, 0);

  oled.setBusClock(400000);
  oled.begin();
  oled.setContrast(140);
  setupDisplayTask();

  if (psramFound()) { poolSize = 640 * 1024; pool = (int8_t*)ps_malloc(poolSize); dlyBuf = (float*)ps_calloc(DLY_N, sizeof(float)); }
  if (!pool) { poolSize = 150 * 1024; pool = (int8_t*)malloc(poolSize); }
  if (!dlyBuf) dlyBuf = (float*)calloc(DLY_N, sizeof(float));
  for (int k = 0; k < 4; k++) {                      // reverb lines: internal RAM first (faster), PSRAM if not
    rv[k] = (float*)heap_caps_calloc(RL[k], sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!rv[k]) rv[k] = (float*)ps_calloc(RL[k], sizeof(float));
  }
  logf("robo dj: PSRAM %s, pool %u KB, free heap %u\n", psramFound() ? "yes" : "no", poolSize / 1024, ESP.getFreeHeap());
  decodeTalk();
  buildMeshes();

  nstate = esp_random() | 1;
  randomSeed(esp_random());
  demoPattern();                                     // loaded but stopped: SHIFT tap = PLAY

  setupVocoder();
  vocChord(0);
  setupAmp();
  xTaskCreatePinnedToCore(audioTask, "audio", 16384, nullptr, 10, &audioHandle, 0);
  xTaskCreatePinnedToCore(bankTask, "bank", 12288, nullptr, 1, nullptr, 1);   // voices load while the film plays
}

void loop() {
  static int renderedVoice = 0;
  if (voiceIdx != renderedVoice && !bankBusy) {
    renderedVoice = voiceIdx;
    renderBank(renderedVoice, true);
    flash(VOICES[renderedVoice].name);
    curWord = okWord(curWord);
    if (mode == M_DJ) deckPlay(curWord, false);
  }
  drawFrame();
  static uint32_t lastLog = 0;
  if (STATUS_LOG && millis() - lastLog > 2000) {
    lastLog = millis();
    logf("t=%lus mode %s fx %s step %d blocks %lu heap %u stackfree %u knob %.2f\n", millis() / 1000,
         MODE_NAMES[mode], FX_NAMES[fx], curStep, (unsigned long)audioBlocks, (unsigned)ESP.getFreeHeap(),
         audioHandle ? (unsigned)uxTaskGetStackHighWaterMark(audioHandle) : 0u, knobVal);
  }
  delay(5);
}
