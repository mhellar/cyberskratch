#pragma once
#define HOST_SIM 1
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <cctype>
#include <algorithm>
#include <random>
using std::min; using std::max;
#define PI 3.14159265358979f
#define sq(x) ((x)*(x))
#define constrain(a,l,h) ((a)<(l)?(l):((a)>(h)?(h):(a)))
extern uint32_t fakeMs;
inline uint32_t millis() { return fakeMs; }
inline void delay(int) {}
inline uint32_t micros() { return fakeMs * 1000; }
inline void delayMicroseconds(int) {}
inline uint32_t esp_random() { static std::mt19937 g(1234); return g(); }
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define LOW 0
#define HIGH 1
#define ADC_11db 3
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 1; }
inline int analogRead(int) { return 1350; }
inline void analogReadResolution(int) {}
inline void analogSetPinAttenuation(int, int) {}
inline void rgbLedWrite(int, int, int, int) {}
inline bool psramFound() { return true; }
inline void* ps_malloc(size_t n) { return malloc(n); }
struct SerialC { void setTxTimeoutMs(int) {} void begin(int) {} int availableForWrite() { return 1000; } void write(const uint8_t* b, int n) { fwrite(b, 1, n, stdout); } };
extern SerialC Serial;
struct EspC { uint32_t getFreeHeap() { return 200000; } };
extern EspC ESP;
typedef void* TaskHandle_t;
inline void xTaskCreatePinnedToCore(void (*)(void*), const char*, int, void*, int, TaskHandle_t*, int) {}
inline void vTaskDelete(void*) {}
inline unsigned uxTaskGetStackHighWaterMark(void*) { return 0; }
#define PROGMEM
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
inline void* heap_caps_calloc(size_t n, size_t s, int) { return calloc(n, s); }
inline void* ps_calloc(size_t n, size_t s) { return calloc(n, s); }
inline long random(long a) { return a > 0 ? (long)(esp_random() % (uint32_t)a) : 0; }
inline long random(long a, long b) { return b > a ? a + random(b - a) : a; }
inline void randomSeed(uint32_t) {}
inline float exp2f_(float x) { return exp2f(x); }
