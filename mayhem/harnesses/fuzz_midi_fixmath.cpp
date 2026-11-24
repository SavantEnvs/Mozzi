// Fuzz Mozzi's MIDI-note-to-frequency conversions (mozzi_midi.h: mtof() overloads, Q16n16_mtof)
// and the fixed-point helpers in mozzi_fixmath.cpp (Q16n16_pow2, isqrt16/32, uint8_tMod/Div).
//
// MIDI note numbers are 7-bit data bytes on the MIDI wire (0..127), which is the documented domain
// of the note-number arguments, so notes are taken modulo 128 (with any fractional part for the
// fixed-point forms). uint8_tMod/uint8_tDiv need d != 0 (they divide by d).
#include "fuzz_common.h"
#include <MozziHeadersOnly.h>
#include <mozzi_midi.h>
#include <mozzi_fixmath.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzReader r(data, size);
  volatile int64_t acc = 0;
  for (int ops = 0; !r.empty() && ops < 1024; ops++) {
    switch (r.u8() % 9) {
      case 0: acc += mtof((uint8_t)(r.u8() & 127)); break;
      case 1: acc += mtof((int)(r.u8() & 127)); break;
      case 2: acc += (int64_t)mtof(r.uf(128.0f)); break;
      case 3: acc += Q16n16_mtof(r.u32() & 0x7fffff); break;           // note 0..127.99 in Q16n16
      case 4: acc += mtof(UFix<7, 0>::fromRaw(r.u8() & 127)).asRaw(); break;
      case 5: acc += mtof(UFix<7, 9>::fromRaw(r.u16())).asRaw(); break;
      // Q16n16 result: the integer part of the exponent must be < 16 (1 << Q with Q >= 32 is UB).
      case 6: acc += Q16n16_pow2((Q8n8)(r.u16() % (16 << 8))); break;
      case 7: acc += isqrt16(r.u16()) + isqrt32(r.u32()); break;
      case 8: { uint8_t n = r.u8(), d = r.u8(); if (d) acc += uint8_tMod(n, d) + uint8_tDiv(n, d); break; }
    }
  }
  return 0;
}
