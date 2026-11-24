// Shared helpers for the Mozzi fuzz harnesses: a bounded reader over the fuzz input.
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

struct FuzzReader {
  const uint8_t *p; size_t n;
  FuzzReader(const uint8_t *d, size_t s) : p(d), n(s) {}
  bool empty() const { return n == 0; }
  uint8_t u8() { if (!n) return 0; uint8_t v = *p++; --n; return v; }
  uint16_t u16() { uint16_t v = u8(); return (uint16_t)(v | (uint16_t)u8() << 8); }
  uint32_t u32() { uint32_t v = u16(); return v | (uint32_t)u16() << 16; }
  int8_t i8() { return (int8_t)u8(); }
  int16_t i16() { return (int16_t)u16(); }
  // finite float in a bounded range [0, max)
  float uf(float max) { return (float)u16() * (max / 65536.0f); }
};
