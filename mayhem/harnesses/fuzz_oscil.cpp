// Fuzz Mozzi's wavetable oscillators: Oscil<> (Oscil.h) and MetaOscil<> (MetaOscil.h, band-limited
// oscillator switching by cutoff frequency). Input: a 512-cell table (exact-size heap buffer), then
// an op stream of setFreq*/setPhase*/phMod/atIndex/next calls with fuzzed arguments.
#include "fuzz_common.h"
#include <MozziHeadersOnly.h>
#include <Oscil.h>
#include <MetaOscil.h>

static const uint16_t N = 512, RATE = 16384;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 2) return 0;
  FuzzReader r(data, size);
  int8_t *table = new int8_t[N];
  for (unsigned i = 0; i < N; i++) table[i] = r.i8();
  Oscil<N, RATE> o1(table), o2(table), o3(table);
  MetaOscil<N, RATE, 3> meta(&o1, &o2, &o3);
  // Oscil's constructor leaves phase and increment uninitialised (zero for Arduino globals).
  for (auto *o : {&o1, &o2, &o3}) { o->setPhaseFractional(0); o->setPhaseInc(0); }
  // MetaOscil::setCutoffFreqs: "They have to be sorted in increasing values".
  int c0 = r.u16() % 8000, c1 = c0 + r.u16() % 8000, c2 = c1 + r.u16() % 8000;
  meta.setCutoffFreqs(c0, c1, c2);
  volatile int acc = 0;
  for (int ops = 0; !r.empty() && ops < 512; ops++) {
    switch (r.u8() % 14) {
      case 0: o1.setFreq((int)r.u16()); break;
      case 1: o1.setFreq(r.uf(8192.0f)); break;
      case 2: o1.setFreq_Q24n8(r.u32()); break;
      case 3: o1.setFreq_Q16n16(r.u32()); break;
      case 4: o1.setPhase(r.u16()); break;
      case 5: o1.setPhaseFractional(r.u32()); break;
      case 6: o1.setPhaseInc(r.u32()); break;
      // phMod's Q15n16 argument is a proportion of the table (+-1.0 = one table length); keep it
      // within +-64 table lengths so phmod*NUM_TABLE_CELLS fits the int32 Oscil.h computes it in.
      case 7: acc += o1.phMod((Q15n16)(r.u32() & 0x7fffff) - (1 << 22)); break;
      case 8: acc += o1.atIndex(r.u32()); break;
      case 9: { unsigned k = r.u8() + 1; for (unsigned i = 0; i < k; i++) acc += o1.next(); break; }
      case 10: meta.setFreq((int)r.u16()); break;
      case 11: meta.setFreq(r.uf(32768.0f)); break;
      case 12: meta.setFreq_Q16n16(r.u32()); break;
      case 13: { unsigned k = r.u8() + 1; for (unsigned i = 0; i < k; i++) acc += meta.next();
        acc += meta.atIndex(r.u16()); break; }
    }
  }
  delete[] table;
  return 0;
}
