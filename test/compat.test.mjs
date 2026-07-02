import assert from 'node:assert/strict';
import { test } from 'node:test';

import Box3D from '../dist/box3d.compat.mjs';

const b3 = await Box3D();

test('compat build is single threaded', () => {
  assert.equal(b3.threaded, false);
});

test('compat build simulates correctly without SIMD', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const ground = world.createBody({ type: 'static', position: { x: 0, y: -0.5, z: 0 } });
  ground.createBox({ halfExtents: { x: 20, y: 0.5, z: 20 } });

  const box = world.createBody({ type: 'dynamic', position: { x: 0, y: 5, z: 0 } });
  box.createBox({ halfExtents: { x: 0.5, y: 0.5, z: 0.5 }, density: 1 });

  for (let i = 0; i < 240; i++) {
    world.step(1 / 60, 4);
  }

  const p = box.getPosition();
  assert.ok(Math.abs(p.y - 0.5) < 0.01, `expected y near 0.5, got ${p.y}`);
  assert.equal(box.isAwake(), false);

  world.destroy();
  world.delete();
});
