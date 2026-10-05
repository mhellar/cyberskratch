#pragma once
#include <cstdint>
#include <cstddef>
#define I2S_MODE_STD 0
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_STEREO 2
extern void (*i2sHook)(const int16_t*, size_t);
struct I2SClass { void setPins(int, int, int) {} bool begin(int, int, int, int) { return true; } void write(uint8_t* b, size_t n) { if (i2sHook) i2sHook((const int16_t*)b, n / 2); } };
