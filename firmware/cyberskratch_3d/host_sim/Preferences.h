#pragma once
struct Preferences { void begin(const char*, bool) {} int getInt(const char*, int d) { return d; } void putInt(const char*, int) {} };
