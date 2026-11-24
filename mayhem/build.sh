#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# Mozzi is a header-only Arduino library (plus mozzi_fixmath.cpp) with no host build system. It is
# compiled for the host with -DESP32 (the board profile whose FLASH_OR_RAM_READ is a plain pointer
# dereference) and mayhem/shim/Arduino.h standing in for the Arduino core. Its one dependency, the
# FixMath header library, is cached in the image under /opt/toolchains/deps (mayhem/Dockerfile).
: "${FIXMATH_DIR:=/opt/toolchains/deps/fixmath-21f3f8d/include}"
[ -f "$FIXMATH_DIR/FixMath.h" ] || { echo "FixMath not found at $FIXMATH_DIR" >&2; exit 1; }
INC=(-DESP32 -I"$SRC/mayhem/shim" -I"$SRC" -I"$FIXMATH_DIR")
BIN="$SRC/mayhem/bin"; OBJ="$SRC/mayhem/obj"
mkdir -p "$BIN" "$OBJ"
CXXSTD=-std=gnu++20  # FixMath relies on C++20 shift semantics (left shift of negatives)

# 1) The project's one compiled source, instrumented like the harnesses (the headers are compiled into
#    each harness TU with the same flags). Fuzzer objects also get SanCov (-fsanitize=fuzzer-no-link) so
#    libFuzzer/Mayhem see the Mozzi code's edges; the standalone reproducers are compiled without it.
FUZZ_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -O1"
COV_FLAGS="-fsanitize=fuzzer-no-link"
$CXX $CXXSTD $FUZZ_FLAGS $COV_FLAGS "${INC[@]}" -c "$SRC/mozzi_fixmath.cpp" -o "$OBJ/mozzi_fixmath.o"
$CXX $CXXSTD $FUZZ_FLAGS "${INC[@]}" -c "$SRC/mozzi_fixmath.cpp" -o "$OBJ/mozzi_fixmath-standalone.o"
$CXX $FUZZ_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OBJ/lsan_off.o"
$CC $FUZZ_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/standalone_main.o"

# 2) Each harness twice: libFuzzer binary + run-once standalone reproducer.
build_harness() {  # <target> <source> [extra flags...]
  local name="$1" src="$2"; shift 2
  $CXX $CXXSTD $FUZZ_FLAGS $COV_FLAGS "${INC[@]}" "$@" -c "$src" -o "$OBJ/fuzz_$name.o"
  $CXX $FUZZ_FLAGS $LIB_FUZZING_ENGINE "$OBJ/fuzz_$name.o" "$OBJ/mozzi_fixmath.o" "$OBJ/lsan_off.o" \
      -lm -o "$BIN/fuzz_$name"
  $CXX $CXXSTD $FUZZ_FLAGS "${INC[@]}" "$@" -c "$src" -o "$OBJ/fuzz_$name-standalone.o"
  $CXX $FUZZ_FLAGS "$OBJ/standalone_main.o" "$OBJ/fuzz_$name-standalone.o" "$OBJ/mozzi_fixmath-standalone.o" \
      "$OBJ/lsan_off.o" -lm -o "$BIN/fuzz_$name-standalone"
}
H="$SRC/mayhem/harnesses"
build_harness sample_none    "$H/fuzz_sample.cpp" -DSAMPLE_INTERP=INTERP_NONE
build_harness sample_linear  "$H/fuzz_sample.cpp" -DSAMPLE_INTERP=INTERP_LINEAR
build_harness sample_huffman "$H/fuzz_sample_huffman.cpp"
build_harness oscil          "$H/fuzz_oscil.cpp"
build_harness delay          "$H/fuzz_delay.cpp"
build_harness midi_fixmath   "$H/fuzz_midi_fixmath.cpp"
build_harness dsp            "$H/fuzz_dsp.cpp"

# 3) Known-answer functional tests (Mozzi ships no host test suite), normal flags, no sanitizers.
$CXX $CXXSTD -O2 $COVERAGE_FLAGS "${INC[@]}" "$SRC/mayhem/tests/mozzi_known_answers.cpp" "$SRC/mozzi_fixmath.cpp" \
    -lm -o "$BIN/mozzi_known_answers"
echo "build.sh: done"
