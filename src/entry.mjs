// Auto-detecting entry point for box3d-wasm.
// Picks the best flavour for the current runtime:
//   deluxe   SIMD + threads (needs SharedArrayBuffer)
//   standard SIMD, single threaded
//   compat   no SIMD, single threaded, runs anywhere
// Import a specific flavour directly with box3d-wasm/standard,
// box3d-wasm/deluxe, or box3d-wasm/compat.

export default async (options) => {
  /**
   * This validation expression comes from wasm-feature-detect:
   * https://github.com/GoogleChromeLabs/wasm-feature-detect
   *
   * Copyright 2019 Google Inc. All Rights Reserved.
   * Licensed under the Apache License, Version 2.0 (the "License");
   * you may not use this file except in compliance with the License.
   * You may obtain a copy of the License at
   *     http://www.apache.org/licenses/LICENSE-2.0
   * Unless required by applicable law or agreed to in writing, software
   * distributed under the License is distributed on an "AS IS" BASIS,
   * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   * See the License for the specific language governing permissions and
   * limitations under the License.
   */
  const hasSIMD = WebAssembly.validate(
    new Uint8Array([
      0, 97, 115, 109, 1, 0, 0, 0, 1, 5, 1, 96, 0, 1, 123, 3, 2, 1, 0, 10, 10, 1, 8, 0, 65, 0,
      253, 15, 253, 98, 11,
    ]),
  );

  // Threads need SharedArrayBuffer. Browsers only expose it on cross-origin
  // isolated pages; Node.js and isolated workers always have it.
  const canThread =
    typeof SharedArrayBuffer !== 'undefined' &&
    (typeof globalThis.crossOriginIsolated === 'undefined' || globalThis.crossOriginIsolated);

  const flavour = hasSIMD
    ? canThread
      ? await import('./box3d.deluxe.mjs')
      : await import('./box3d.mjs')
    : await import('./box3d.compat.mjs');

  return await flavour.default(options);
};
