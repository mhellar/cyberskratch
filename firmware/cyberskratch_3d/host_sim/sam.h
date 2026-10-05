#pragma once
inline void SetInput(char*) {} inline void SetSpeed(unsigned char) {} inline void SetPitch(unsigned char) {}
inline void SetMouth(unsigned char) {} inline void SetThroat(unsigned char) {} inline void EnableSingmode(int) {}
inline int SAMMain(void (*cb)(void*, unsigned char), void* d) { for (int i = 0; i < 8000; i++) cb(d, 128 + (i % 40) - 20); return 1; }
