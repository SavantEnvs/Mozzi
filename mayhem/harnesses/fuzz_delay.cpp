// Fuzz Mozzi's delay lines: AudioDelay, AudioDelayFeedback (LINEAR and ALLPASS interpolation),
// ControlDelay and ReverbTank — buffer indexing with caller-supplied delay times and feedback.
// Input: an op stream; every op's arguments come from the fuzz input.
#include "fuzz_common.h"
#include <MozziHeadersOnly.h>
#include <mozzi_fixmath.h>  // AudioDelayFeedback.h uses Q16n16 without including it
#include <AudioDelay.h>
#include <AudioDelayFeedback.h>
#include <ControlDelay.h>
#include <ReverbTank.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1) return 0;
  FuzzReader r(data, size);
  AudioDelay<256> d1;
  AudioDelay<512, int> d2;
  AudioDelayFeedback<256, LINEAR> f1;
  AudioDelayFeedback<512, ALLPASS> f2;
  AudioDelayFeedback<256, LINEAR, int16_t> f3;
  ControlDelay<128, int> cd;
  int8_t e1 = r.i8() & 127, e2 = e1 + (r.u8() % (128 - e1)), e3 = e2 + (r.u8() % (128 - e2));
  ReverbTank rv(e1, e2, e3, r.i8() & 127, r.u8(), r.i8());
  volatile int acc = 0;
  for (int ops = 0; !r.empty() && ops < 1024; ops++) {
    switch (r.u8() % 16) {
      case 0: acc += d1.next(r.i8(), r.u16()); break;
      case 1: d1.set(r.u16()); acc += d1.next(r.i8()); break;
      case 2: acc += d1.read(r.u16()) + d2.read(r.u16()); break;
      case 3: acc += d2.next(r.i16(), r.u16()); break;
      case 4: f1.setDelayTimeCells(r.u16()); acc += f1.next(r.i8()); break;
      case 5: acc += f1.next(r.i8(), (Q16n16)r.u32()); break;
      case 6: f1.setFeedbackLevel(r.i8()); f1.write(r.i8(), r.u16()); acc += f1.read((Q16n16)r.u32()); break;  // read() without args does not compile for LINEAR
      case 7: f2.setDelayTimeCells((Q16n16)r.u32()); acc += f2.next(r.i8()); break;
      case 8: f2.setDelayTimeCells(r.uf(1024.0f)); f2.setFeedbackLevel(r.i8()); acc += f2.next(r.i8()); break;
      case 9: acc += f2.next(r.i8(), r.u16()); f2.writeFeedback(r.i8()); break;
      case 10: acc += f3.next(r.i16(), (Q16n16)r.u32()); f3.write(r.i16()); break;
      case 11: acc += cd.next(r.i16(), r.u16()); acc += cd.read(r.u16()); break;
      case 12: acc += rv.next(r.i16()); break;
      case 13: rv.setLoopDelays(r.i8() & 127, r.u8()); break;
      case 14: rv.setFeebackLevel(r.i8()); break;
      case 15: { int8_t a = r.i8() & 127, b = a + (r.u8() % (128 - a)), c = b + (r.u8() % (128 - b));
        rv.setEarlyReflections(a, b, c); for (int i = 0; i < 64; i++) acc += rv.next(r.i16()); break; }
    }
  }
  return 0;
}
