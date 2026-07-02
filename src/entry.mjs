// Auto-detecting entry point for box3d-wasm.
//
// Wasm SIMD is baseline in every modern browser and Node.js, so both builds
// use it. The only runtime question is threads: the deluxe build needs
// SharedArrayBuffer, which browsers only expose on cross-origin isolated
// pages. Node.js always has it.
//
//   deluxe   SIMD + threads
//   standard SIMD, single threaded
//
// Import a flavour directly with box3d-wasm/deluxe or box3d-wasm/standard
// to skip detection.

export default async (options) => {
  const canThread =
    typeof SharedArrayBuffer !== 'undefined' &&
    (typeof globalThis.crossOriginIsolated === 'undefined' || globalThis.crossOriginIsolated);

  const flavour = canThread ? await import('./box3d.deluxe.mjs') : await import('./box3d.mjs');
  return await flavour.default(options);
};
