// Known-answer functional tests for Mozzi's host-runnable DSP code (built by mayhem/build.sh with
// normal flags, run by mayhem/test.sh). Mozzi ships no host test suite, so these check documented
// behaviour of the public API: each case prints "PASS <name>" or "FAIL <name>: ...".
#include <Arduino.h>
#include <MozziHeadersOnly.h>
#include <Sample.h>
#include <Oscil.h>
#include <AudioDelay.h>
#include <Line.h>
#include <mozzi_midi.h>
#include <mozzi_fixmath.h>
#include <stdio.h>

static int fails = 0;
static void check(const char *name, bool ok, long got, long want) {
  if (ok) printf("PASS %s\n", name);
  else { printf("FAIL %s: got %ld want %ld\n", name, got, want); fails++; }
}

static int8_t stable[256];
static int8_t otable[512];

int main() {
  // MIDI note 69 is A4 = 440 Hz; note 60 is middle C = 261.6 Hz; 81 = 880 Hz.
  // (the integer forms truncate the Q16n16 table entry, so allow 1 Hz below the true pitch)
  check("mtof_69", mtof((uint8_t)69) >= 439 && mtof((uint8_t)69) <= 440, mtof((uint8_t)69), 440);
  check("mtof_60", mtof((uint8_t)60) == 261, mtof((uint8_t)60), 261);
  check("mtof_81", mtof((uint8_t)81) >= 879 && mtof((uint8_t)81) <= 880, mtof((uint8_t)81), 880);
  { long q = Q16n16_mtof(69UL << 16); check("Q16n16_mtof_69", q > (439L << 16) + 60000 && q < (440L << 16) + 1000, q >> 16, 440); }
  { float f = mtof(69.0f); check("mtof_float_69", f > 439.9f && f < 440.1f, (long)f, 440); }
  check("isqrt16_10000", isqrt16(10000) == 100, isqrt16(10000), 100);
  check("isqrt32_1e8", isqrt32(100000000UL) == 10000, isqrt32(100000000UL), 10000);
  check("uint8_tDiv_200_7", uint8_tDiv(200, 7) == 28, uint8_tDiv(200, 7), 28);
  check("uint8_tMod_200_7", uint8_tMod(200, 7) == 4, uint8_tMod(200, 7), 4);
  check("Q16n16_pow2_3", (Q16n16_pow2(3 << 8) >> 16) == 8, Q16n16_pow2(3 << 8) >> 16, 8);

  // Sample: one cell per next() plays the table back in order, then 0 once finished (no looping).
  for (int i = 0; i < 256; i++) stable[i] = (int8_t)(i - 128);
  {
    Sample<256, 16384> s(stable);
    s.rangeWholeSample();
    s.setPhaseInc(1UL << SAMPLE_F_BITS);
    s.start();
    long bad = -1;
    for (int i = 0; i < 256; i++) { int8_t v = s.next(); if (v != (int8_t)(i - 128) && bad < 0) bad = i; }
    check("sample_playback_in_order", bad < 0, bad, -1);
    check("sample_not_playing_at_end", !s.isPlaying(), s.isPlaying(), 0);
    check("sample_returns_0_after_end", s.next() == 0, 0, 0);
    s.setLoopingOn(); s.start(64);
    check("sample_start_pos", s.next() == (int8_t)(64 - 128), 0, 64 - 128);
  }
  {
    Sample<256, 16384> s(stable);
    s.rangeWholeSample(); s.setLoopingOn(); s.setPhaseInc(1UL << SAMPLE_F_BITS); s.start();
    long bad = -1;
    for (int i = 0; i < 1024; i++) { int8_t v = s.next(); if (v != (int8_t)((i & 255) - 128) && bad < 0) bad = i; }
    check("sample_looping_wraps", bad < 0, bad, -1);
    check("sample_atIndex", s.atIndex(10) == (int8_t)(10 - 128), s.atIndex(10), 10 - 128);
  }

  // Oscil: one cell per step, index wraps modulo the table size.
  for (int i = 0; i < 512; i++) otable[i] = (int8_t)(i & 0x7f);
  {
    Oscil<512, 16384> o(otable);
    o.setPhase(0); o.setPhaseInc(1UL << OSCIL_F_BITS);
    int8_t first = o.next();
    long bad = -1;
    for (int i = 1; i < 2000; i++) { int8_t v = o.next(); if (v != (int8_t)((first + i) & 0x7f) && bad < 0) bad = i; }
    check("oscil_steps_one_cell", bad < 0, bad, -1);
    check("oscil_atIndex_wraps", o.atIndex(512 + 5) == 5, o.atIndex(512 + 5), 5);
    o.setPhase(100);
    check("oscil_setPhase", (o.getPhaseFractional() >> OSCIL_F_BITS) == 100, o.getPhaseFractional() >> OSCIL_F_BITS, 100);
  }

  // AudioDelay: next(x, d) returns the input from d calls earlier.
  {
    AudioDelay<256> d;
    long bad = -1;
    for (int i = 0; i < 600; i++) {
      int8_t out = d.next((int8_t)(i & 0x7f), 7);
      if (i >= 7 && out != (int8_t)((i - 7) & 0x7f) && bad < 0) bad = i;
    }
    check("audiodelay_7_cells", bad < 0, bad, -1);
  }

  // Line<long>: reaches its target in num_steps steps.
  {
    Line<long> l; l.set(0, 1000, 10);
    long v = 0; for (int i = 0; i < 10; i++) v = l.next();
    check("line_reaches_target", v == 1000, v, 1000);
  }
  printf("SUMMARY %d failed\n", fails);
  return fails ? 1 : 0;
}
