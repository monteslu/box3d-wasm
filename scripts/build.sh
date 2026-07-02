#!/usr/bin/env bash
# Build the Box3D static library with emscripten, then link the embind glue
# into isomorphic ES modules (browser + node).
#
# Flavours:
#   standard  SIMD (wasm simd128 via the SSE2 path), single threaded
#   deluxe    SIMD + wasm threads (SharedArrayBuffer, pthreads)
#   compat    no SIMD, single threaded, runs anywhere
#
# The auto-detecting entry (src/entry.mjs) is copied to dist/ and picks the
# best flavour at runtime.
#
# Usage:
#   scripts/build.sh                 build all flavours, Release
#   FLAVOURS=standard scripts/build.sh
#   TARGET_TYPE=Debug scripts/build.sh
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ROOT="$(dirname "$DIR")"

FLAVOURS="${FLAVOURS:-standard deluxe compat}"
TARGET_TYPE="${TARGET_TYPE:-Release}"
BOX3D_SRC="$ROOT/deps/box3d"

if [ ! -f "$BOX3D_SRC/CMakeLists.txt" ]; then
  echo "deps/box3d missing. Run: npm run fetch-deps" >&2
  exit 1
fi

command -v emcc >/dev/null || { echo "emcc not found. Activate emsdk first." >&2; exit 1; }

mkdir -p "$ROOT/dist" "$ROOT/build"

build_lib_cmake() {
  # standard and deluxe use upstream CMake, which adds -msimd128 -msse2 for
  # emscripten builds on its own.
  local flavour="$1"
  local cmake_build_dir="$2"
  local extra_cflags="$3"

  echo "==> cmake ($flavour, $TARGET_TYPE)"
  CFLAGS="$extra_cflags" emcmake cmake \
    -S "$BOX3D_SRC" \
    -B "$cmake_build_dir" \
    -DCMAKE_BUILD_TYPE="$TARGET_TYPE" \
    -DBOX3D_SAMPLES=OFF \
    -DBOX3D_UNIT_TESTS=OFF \
    -DBOX3D_BENCHMARKS=OFF \
    -DBOX3D_DOCS=OFF \
    -DBOX3D_VALIDATE=OFF \
    > "$cmake_build_dir.cmake.log" 2>&1 || { cat "$cmake_build_dir.cmake.log" >&2; exit 1; }

  echo "==> build libbox3d.a ($flavour)"
  cmake --build "$cmake_build_dir" -j"$(nproc)" > "$cmake_build_dir.build.log" 2>&1 \
    || { tail -50 "$cmake_build_dir.build.log" >&2; exit 1; }
}

build_lib_compat() {
  # Upstream CMake force-adds -msimd128 for emscripten, so the no-SIMD compat
  # library is compiled directly: no simd feature flag means the backend
  # cannot emit vector instructions at all.
  local objdir="$1"
  local lib="$2"
  local opt="-O3 -DNDEBUG"
  if [ "$TARGET_TYPE" = "Debug" ]; then
    opt="-g3"
  fi

  echo "==> compile libbox3d.compat.a (scalar, no simd128)"
  rm -rf "$objdir"
  mkdir -p "$objdir"
  local pids=()
  for f in "$BOX3D_SRC"/src/*.c; do
    emcc -c "$f" \
      -o "$objdir/$(basename "${f%.c}").o" \
      -I "$BOX3D_SRC/include" \
      -I "$BOX3D_SRC/src" \
      -DBOX3D_DISABLE_SIMD \
      -ffp-contract=off \
      $opt &
    pids+=("$!")
    if [ "${#pids[@]}" -ge "$(nproc)" ]; then
      wait "${pids[0]}"
      pids=("${pids[@]:1}")
    fi
  done
  wait
  emar rcs "$lib" "$objdir"/*.o
}

for FLAVOUR in $FLAVOURS; do
  FLAVOUR_FLAGS=()
  SIMD_FLAGS=(-msimd128 -msse2)
  BASENAME="box3d"
  case "$FLAVOUR" in
    standard)
      CMAKE_BUILD_DIR="$ROOT/build/cmake-$FLAVOUR-$TARGET_TYPE"
      build_lib_cmake "$FLAVOUR" "$CMAKE_BUILD_DIR" ""
      LIB="$CMAKE_BUILD_DIR/src/libbox3d.a"
      ;;
    deluxe)
      FLAVOUR_FLAGS=(-pthread)
      BASENAME="box3d.deluxe"
      CMAKE_BUILD_DIR="$ROOT/build/cmake-$FLAVOUR-$TARGET_TYPE"
      build_lib_cmake "$FLAVOUR" "$CMAKE_BUILD_DIR" "-pthread"
      LIB="$CMAKE_BUILD_DIR/src/libbox3d.a"
      ;;
    compat)
      SIMD_FLAGS=()
      BASENAME="box3d.compat"
      LIB="$ROOT/build/libbox3d.compat-$TARGET_TYPE.a"
      build_lib_compat "$ROOT/build/compat-objs-$TARGET_TYPE" "$LIB"
      ;;
    *)
      echo "Unknown flavour: $FLAVOUR (expected standard, deluxe, or compat)" >&2
      exit 1
      ;;
  esac

  [ -f "$LIB" ] || { echo "missing $LIB" >&2; exit 1; }

  EMCC_OPTS=(
    -std=c++17
    -lembind
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=Box3D
    -sENVIRONMENT=web,worker,node
    -sALLOW_MEMORY_GROWTH=1
    -sALLOW_TABLE_GROWTH=1
    -sFILESYSTEM=0
    -sEXPORTED_RUNTIME_METHODS=HEAPF32,HEAPU8,HEAPU32
    "${SIMD_FLAGS[@]}"
  )

  if [ "$FLAVOUR" = "deluxe" ]; then
    EMCC_OPTS+=(
      -pthread
      "-sPTHREAD_POOL_SIZE=(globalThis.navigator&&navigator.hardwareConcurrency)||8"
    )
  fi

  case "$TARGET_TYPE" in
    Debug)
      EMCC_OPTS+=(-g3 -gsource-map -sASSERTIONS=2)
      ;;
    *)
      EMCC_OPTS+=(-O3)
      ;;
  esac

  echo "==> emcc link ($FLAVOUR) -> dist/$BASENAME.mjs"
  emcc "$ROOT/csrc/glue.cpp" "$LIB" \
    -I "$BOX3D_SRC/include" \
    "${EMCC_OPTS[@]}" \
    "${FLAVOUR_FLAGS[@]:-}" \
    -o "$ROOT/dist/$BASENAME.mjs"
done

cp "$ROOT/src/entry.mjs" "$ROOT/dist/entry.mjs"

echo "==> done"
ls -la "$ROOT/dist"
