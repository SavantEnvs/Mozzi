// Fuzz Mozzi's remaining runtime-parameterised DSP classes: ADSR, Ead, Line, ResonantFilter /
// MultiResonantFilter, StateVariable, Smooth, DCfilter, RollingAverage and WaveFolder.
// Input: an op stream; each op sets fuzzed parameters (times, levels, steps, cutoff/resonance,
// limits) and runs the object's next()/update(). Mostly a UBSan target (division by fuzzed step
// counts, shifts, signed overflow).
#include "fuzz_common.h"
#include <MozziHeadersOnly.h>
#include <ADSR.h>
#include <Ead.h>
#include <Line.h>
#include <ResonantFilter.h>
#include <StateVariable.h>
#include <Smooth.h>
#include <DCfilter.h>
#include <RollingAverage.h>
#include <WaveFolder.h>

#include <new>

// Arduino sketches declare these objects as globals, which start zeroed (.bss). Several constructors
// rely on that: Line() and StateVariable() are empty, and StateVariable's low/band are never set by
// any public API. So each input gets fresh objects placement-new'd into a zeroed static buffer,
// matching the Arduino contract; no state survives from one input to the next.
struct DspState {
  ADSR<64, 16384> adsr;
  Ead ead{64};
  Line<unsigned int> lui;
  Line<long> ll;
  LowPassFilter lpf;
  MultiResonantFilter<uint16_t> mrf;
  StateVariable<LOWPASS> sv;
  StateVariable<BANDPASS> svb;
  Smooth<int> sm{0.5f};
  DCfilter dc{0.995f};
  RollingAverage<int, 16> ra;
  WaveFolder<int> wf;
};
alignas(DspState) static unsigned char dsp_buf[sizeof(DspState)];

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzReader r(data, size);
  memset(dsp_buf, 0, sizeof(dsp_buf));
  DspState &st = *new (dsp_buf) DspState;
  auto &adsr = st.adsr; auto &ead = st.ead; auto &lui = st.lui; auto &ll = st.ll;
  auto &lpf = st.lpf; auto &mrf = st.mrf; auto &sv = st.sv; auto &svb = st.svb;
  auto &sm = st.sm; auto &dc = st.dc; auto &ra = st.ra; auto &wf = st.wf;
  // Line::next() is only meaningful after a set() (Line.h: "calculates the step size").
  bool lui_set = false, ll_set = false;
  volatile long acc = 0;
  for (int ops = 0; !r.empty() && ops < 1024; ops++) {
    switch (r.u8() % 16) {
      case 0: adsr.setLevels(r.u8(), r.u8(), r.u8(), r.u8()); break;
      case 1: adsr.setTimes(r.u16(), r.u16(), r.u16(), r.u16()); break;
      case 2: if (r.u8() & 1) adsr.noteOn(r.u8() & 1); else adsr.noteOff(); break;
      case 3: { unsigned k = r.u8() + 1; for (unsigned i = 0; i < k; i++) { adsr.update(); acc += adsr.next(); }
        acc += adsr.playing(); break; }
      case 4: ead.set(r.u16(), r.u16()); ead.start(); break;
      case 5: { unsigned k = r.u8() + 1; for (unsigned i = 0; i < k; i++) acc += ead.next(); break; }
      case 6: { unsigned s = r.u16(), t = r.u16(), n = r.u16(); if (n) { lui.set(s, t, n); lui_set = true; }
        if (lui_set) for (int i = 0; i < 32; i++) acc += lui.next(); break; }
      case 7: { long s = (int32_t)r.u32(), t = (int32_t)r.u32(), n = r.u16(); if (n) { ll.set(s, t, n); ll_set = true; }
        if (ll_set) for (int i = 0; i < 32; i++) acc += ll.next(); break; }
      case 8: lpf.setCutoffFreqAndResonance(r.u8(), r.u8()); acc += lpf.next(r.i16()); break;
      case 9: mrf.setCutoffFreqAndResonance(r.u16(), r.u16()); mrf.next(r.i16());
        acc += mrf.low() + mrf.high() + mrf.band() + mrf.notch(); break;
      // setCentreFreq: documented range 20 - 4096 Hz (MOZZI_AUDIO_RATE/4).
      case 10: sv.setResonance(r.u8()); sv.setCentreFreq(20 + r.u16() % 4077); svb.setResonance(r.u8());
        svb.setCentreFreq(20 + r.u16() % 4077); break;
      case 11: { int in = r.i16(); acc += sv.next(in) + svb.next(in); break; }
      // smoothness in (0, 1): setSmoothness(0) converts 1.0 to Q0n16 (65536), out of range.
      case 12: sm.setSmoothness((float)(r.u16() % 65535 + 1) / 65536.0f); acc += sm.next(r.i16()); break;
      case 13: acc += dc.next(r.i16()); break;
      case 14: acc += ra.next(r.i16()); break;
      case 15: { int lo = r.i16(), hi = r.i16(); if (lo < hi) { wf.setLimits(lo, hi); acc += wf.next((int)(int32_t)r.u32() >> 8); } break; }
    }
  }
  st.~DspState();
  return 0;
}
