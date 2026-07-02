import assert from 'node:assert/strict';
import { test } from 'node:test';

import Box3D from '../dist/box3d.deluxe.mjs';

const b3 = await Box3D();

const DT = 1 / 60;
const SUBSTEPS = 4;

function buildPile(world, count) {
  const ground = world.createBody({ type: 'static', position: { x: 0, y: -0.5, z: 0 } });
  ground.createBox({ halfExtents: { x: 40, y: 0.5, z: 40 } });

  const bodies = [];
  for (let i = 0; i < count; i++) {
    const body = world.createBody({
      type: 'dynamic',
      position: {
        x: (i % 10) * 1.1 - 5.5,
        y: 1 + Math.floor(i / 10) * 1.1,
        z: (i % 3) * 0.05,
      },
    });
    body.createBox({ halfExtents: { x: 0.5, y: 0.5, z: 0.5 }, density: 1 });
    bodies.push(body);
  }
  return bodies;
}

test('module reports threaded build', () => {
  assert.equal(b3.threaded, true);
});

test('multithreaded world simulates a pile of boxes', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 }, workerCount: 4 });
  assert.equal(world.getWorkerCount(), 4);

  const bodies = buildPile(world, 100);
  for (let i = 0; i < 240; i++) {
    world.step(DT, SUBSTEPS);
  }

  for (const body of bodies) {
    const p = body.getPosition();
    assert.ok(p.y > 0 && p.y < 15, `body should stay in a sane range, y=${p.y}`);
  }

  world.destroy();
  world.delete();
});

test('worker count above the maximum is clamped', () => {
  const world = new b3.World({ workerCount: 10000 });
  assert.ok(world.getWorkerCount() <= b3.maxWorkers);
  world.destroy();
  world.delete();
});

test('single worker and multi worker runs agree', () => {
  const run = (workerCount) => {
    const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 }, workerCount });
    const bodies = buildPile(world, 50);
    for (let i = 0; i < 120; i++) {
      world.step(DT, SUBSTEPS);
    }
    const out = bodies.map((body) => body.getPosition());
    world.destroy();
    world.delete();
    return out;
  };

  const serial = run(1);
  const parallel = run(4);

  for (let i = 0; i < serial.length; i++) {
    const d = Math.hypot(
      serial[i].x - parallel[i].x,
      serial[i].y - parallel[i].y,
      serial[i].z - parallel[i].z,
    );
    assert.ok(d < 0.01, `body ${i} diverged by ${d} between 1 and 4 workers`);
  }
});
