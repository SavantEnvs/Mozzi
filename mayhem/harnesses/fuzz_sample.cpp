// Fuzz Mozzi's Sample<> wavetable sample player (Sample.h) through its public API.
// Compiled twice: -DSAMPLE_INTERP=INTERP_NONE (fuzz_sample_none) and INTERP_LINEAR (fuzz_sample_linear).
//
// Input: byte 0 picks the table size (256 = power of two, or 300), the next bytes fill the sample
// table (an exact-size heap buffer, so a 1-byte overread is caught by ASan), the rest is an op stream.
//
// Caller contract kept by the harness (Sample.h docs: positions are "in samples from the beginning
// of the sound", i.e. within [0, NUM_TABLE_CELLS]): start < end <= NUM_TABLE_CELLS, and the phase
// increment per next() never exceeds the loop length (end - start) — Sample::next() wraps a loop once
// per call by design, so a sketch moves the end point / turns looping on and then calls start()
// (the documented way to reset the playhead); the harness does the same, since Sample.h does not
// re-clamp a phase that is already past a newly lowered end. Inside that contract every next() must
// read only table[0, end).
#include "fuzz_common.h"
#include <MozziHeadersOnly.h>
#include <Sample.h>
#include <vector>

#ifndef SAMPLE_INTERP
#define SAMPLE_INTERP INTERP_NONE
#endif

static const unsigned RATE = 16384;

template <unsigned N>
static void run(FuzzReader &r) {
  int8_t *table = new int8_t[N];
  for (unsigned i = 0; i < N; i++) table[i] = r.i8();
  Sample<N, RATE, SAMPLE_INTERP> s(table);
  // Sample's constructors leave startpos/phase/phase increment uninitialised (rangeWholeSample() is
  // commented out in Sample.h); on Arduino a global Sample is zero-initialised, so establish that
  // same state through the public API before fuzzing.
  s.rangeWholeSample(); s.setPhaseInc(0); s.start();
  // Mirror of Sample's private constants, used only to keep the phase increment within the contract.
  const uint8_t ADJ = (N < 2048) ? 8 : 0;
  unsigned start = 0, end = N;
  unsigned long inc = 0, maxinc = 0;  // maxinc: largest increment since the last start(), which
                                      // bounds how far a pending phase can overshoot the end point
  auto fits = [&](unsigned long new_inc, unsigned st, unsigned en) {
    unsigned long m = new_inc > maxinc ? new_inc : maxinc;
    return st < en && en <= N && m <= ((unsigned long)(en - st) << SAMPLE_F_BITS);
  };
  auto restart = [&]() { s.start(); maxinc = inc; };
  auto setinc = [&](unsigned long ni) { inc = ni; if (ni > maxinc) maxinc = ni; };
  volatile int acc = 0;
  for (int ops = 0; !r.empty() && ops < 512; ops++) {
    switch (r.u8() % 11) {
      case 0: { int f = r.u16();
        unsigned long ni = ((((unsigned long)N << ADJ) * f) / RATE) << (SAMPLE_F_BITS - ADJ);
        if (fits(ni, start, end)) { s.setFreq(f); setinc(ni); } break; }
      case 1: { float f = r.uf(4096.0f);
        unsigned long ni = (unsigned long)((((float)N * f) / RATE) * SAMPLE_F_BITS_AS_MULTIPLIER);
        if (fits(ni, start, end)) { s.setFreq(f); setinc(ni); } break; }
      // case 2 would be setFreq_Q24n8(): for NUM_TABLE_CELLS < 2048 it shifts by a negative count
      // (Sample.h:273, UB on every call), so it is left out to keep the other paths reachable.
      case 2: s.setLoopingOn(); restart(); break;
      case 3: { unsigned long ni = r.u32();
        if (fits(ni, start, end)) { s.setPhaseInc(ni); setinc(ni); } break; }
      case 4: { unsigned st = r.u16() % (N + 1);
        if (fits(inc, st, end)) { s.setStart(st); start = st; } break; }
      case 5: { unsigned en = r.u16() % (N + 1);
        if (fits(inc, start, en)) { s.setEnd(en); end = en; restart(); } break; }
      case 6: s.rangeWholeSample(); start = 0; end = N; break;
      case 7: if (r.u8() & 1) { s.setLoopingOn(); restart(); } else s.setLoopingOff(); break;
      case 8: restart(); break;
      // (no setPaused(): it is newer than the Sample.h this harness must also build against for mining)
      case 9: { unsigned st = r.u16() % (N + 1);
        if (fits(inc, st, end)) { start = st; s.start(st); maxinc = inc; } break; }
      case 10: { unsigned k = r.u16() % 4096 + 1;
        for (unsigned i = 0; i < k; i++) acc += s.next();
        acc += s.isPlaying() + s.atIndex(r.u16() % N); break; }
    }
  }
  delete[] table;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2) return 0;
  FuzzReader r(data, size);
  if (r.u8() & 1) run<300>(r); else run<256>(r);
  return 0;
}
