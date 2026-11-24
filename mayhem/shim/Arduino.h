#pragma once
// Host shim for the Arduino core: just enough for Mozzi's headers to compile on Linux with -DESP32
// (which makes FLASH_OR_RAM_READ a plain dereference). Part of the mayhem/ fuzz layer, not upstream.
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef uint8_t byte; typedef bool boolean;
#define IRAM_ATTR
#define PROGMEM
#define HIGH 1
#define LOW 0
static inline long map(long x,long a,long b,long c,long d){return (x-a)*(d-c)/(b-a)+c;}
static inline unsigned long micros(){return 0;} static inline unsigned long millis(){return 0;}
static inline long random(long n){return n?rand()%n:0;}
#define constrain(x,l,h) ((x)<(l)?(l):((x)>(h)?(h):(x)))
#ifndef PI
#define PI 3.14159265358979
#endif
#include <algorithm>
using std::min; using std::max;
#define OUTPUT 1
#define INPUT 0
static inline void pinMode(int,int){} static inline void digitalWrite(int,int){} static inline int digitalRead(int){return 0;} static inline int analogRead(int){return 0;}
#define lowByte(w) ((uint8_t)((w)&0xff))
#define highByte(w) ((uint8_t)((w)>>8))
