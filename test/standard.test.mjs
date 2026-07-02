import assert from 'node:assert/strict';
import { test } from 'node:test';

import Box3D from '../dist/box3d.mjs';

const b3 = await Box3D();

const DT = 1 / 60;
const SUBSTEPS = 4;

function stepSeconds(world, seconds) {
  const steps = Math.round(seconds / DT);
  for (let i = 0; i < steps; i++) {
    world.step(DT, SUBSTEPS);
  }
}

test('module reports single threaded build', () => {
  assert.equal(b3.threaded, false);
  assert.ok(b3.maxWorkers >= 1);
});

test('world creation, gravity, destroy', () => {
  const world = new b3.World({ gravity: { x: 0, y: -9.81, z: 0 } });
  assert.ok(world.isValid());
  const g = world.getGravity();
  assert.ok(Math.abs(g.y + 9.81) < 1e-6);
  world.setGravity({ x: 0, y: -5, z: 0 });
  assert.ok(Math.abs(world.getGravity().y + 5) < 1e-6);
  world.destroy();
  assert.equal(world.isValid(), false);
  world.delete();
});

test('dynamic box falls onto static ground and settles', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const ground = world.createBody({ type: 'static', position: { x: 0, y: -0.5, z: 0 } });
  ground.createBox({ halfExtents: { x: 20, y: 0.5, z: 20 } });

  const box = world.createBody({ type: 'dynamic', position: { x: 0, y: 5, z: 0 } });
  box.createBox({ halfExtents: { x: 0.5, y: 0.5, z: 0.5 }, density: 1 });
  assert.ok(Math.abs(box.getMass() - 1) < 1e-5);

  stepSeconds(world, 4);

  const p = box.getPosition();
  assert.ok(Math.abs(p.y - 0.5) < 0.01, `expected y near 0.5, got ${p.y}`);
  assert.equal(box.isAwake(), false, 'box should fall asleep after settling');

  world.destroy();
  world.delete();
});

test('sphere with restitution bounces', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const ground = world.createBody({ type: 'static', position: { x: 0, y: -0.5, z: 0 } });
  ground.createBox({ halfExtents: { x: 20, y: 0.5, z: 20 }, restitution: 0.8 });

  const ball = world.createBody({ type: 'dynamic', position: { x: 0, y: 4, z: 0 } });
  ball.createSphere({ radius: 0.5, density: 1, restitution: 0.8 });

  let touchedGround = false;
  let bounceHeight = 0;
  for (let i = 0; i < 600; i++) {
    world.step(DT, SUBSTEPS);
    const y = ball.getPosition().y;
    if (!touchedGround && y < 0.55) {
      touchedGround = true;
    }
    if (touchedGround) {
      bounceHeight = Math.max(bounceHeight, y);
    }
  }
  assert.ok(touchedGround, 'ball should reach the ground');
  assert.ok(bounceHeight > 1.0, `ball should bounce back up, peaked at ${bounceHeight}`);

  world.destroy();
  world.delete();
});

test('box stack settles at expected heights', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const ground = world.createBody({ type: 'static', position: { x: 0, y: -0.5, z: 0 } });
  ground.createBox({ halfExtents: { x: 20, y: 0.5, z: 20 } });

  const boxes = [];
  for (let i = 0; i < 5; i++) {
    const body = world.createBody({ type: 'dynamic', position: { x: 0, y: 0.6 + i * 1.05, z: 0 } });
    body.createBox({ halfExtents: { x: 0.5, y: 0.5, z: 0.5 }, density: 1, friction: 0.6 });
    boxes.push(body);
  }

  stepSeconds(world, 5);

  for (let i = 0; i < 5; i++) {
    const y = boxes[i].getPosition().y;
    const expected = 0.5 + i;
    assert.ok(Math.abs(y - expected) < 0.05, `box ${i} expected y near ${expected}, got ${y}`);
  }

  world.destroy();
  world.delete();
});

test('capsule and hull shapes simulate', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const ground = world.createBody({ type: 'static', position: { x: 0, y: -0.5, z: 0 } });
  ground.createBox({ halfExtents: { x: 20, y: 0.5, z: 20 } });

  const capsuleBody = world.createBody({ type: 'dynamic', position: { x: -3, y: 3, z: 0 } });
  const capsule = capsuleBody.createCapsule({ height: 1, radius: 0.4, density: 1 });
  assert.equal(capsule.getType(), 'capsule');

  const hullBody = world.createBody({ type: 'dynamic', position: { x: 3, y: 3, z: 0 } });
  const hull = hullBody.createHull({
    points: [
      { x: -0.5, y: -0.5, z: -0.5 },
      { x: 0.5, y: -0.5, z: -0.5 },
      { x: 0.5, y: -0.5, z: 0.5 },
      { x: -0.5, y: -0.5, z: 0.5 },
      { x: 0, y: 0.7, z: 0 },
    ],
    density: 1,
  });
  assert.equal(hull.getType(), 'hull');
  assert.ok(hull.isValid());

  stepSeconds(world, 4);

  assert.ok(capsuleBody.getPosition().y < 1.0, 'capsule should come to rest near the ground');
  assert.ok(hullBody.getPosition().y < 1.0, 'hull should come to rest near the ground');

  world.destroy();
  world.delete();
});

test('castRayClosest hits the nearest shape', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const bodyA = world.createBody({ type: 'static', position: { x: 0, y: 0, z: 0 } });
  const shapeA = bodyA.createSphere({ radius: 1 });
  const bodyB = world.createBody({ type: 'static', position: { x: 5, y: 0, z: 0 } });
  bodyB.createSphere({ radius: 1 });

  const result = world.castRayClosest({ x: -5, y: 0, z: 0 }, { x: 20, y: 0, z: 0 }, undefined);
  assert.equal(result.hit, true);
  assert.ok(Math.abs(result.point.x + 1) < 1e-3, `nearest surface at x=-1, got ${result.point.x}`);
  assert.equal(result.shapeUserData, shapeA.getUserData());
  assert.equal(result.bodyUserData, bodyA.getUserData());
  assert.ok(result.shape.isValid());
  result.shape.delete();

  const miss = world.castRayClosest({ x: -5, y: 10, z: 0 }, { x: 20, y: 0, z: 0 }, undefined);
  assert.equal(miss.hit, false);

  world.destroy();
  world.delete();
});

test('body move events report motion and sleep', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const ground = world.createBody({ type: 'static', position: { x: 0, y: -0.5, z: 0 } });
  ground.createBox({ halfExtents: { x: 20, y: 0.5, z: 20 } });

  const box = world.createBody({ type: 'dynamic', position: { x: 0, y: 3, z: 0 }, userData: 42 });
  box.createBox({ halfExtents: { x: 0.5, y: 0.5, z: 0.5 }, density: 1 });

  world.step(DT, SUBSTEPS);
  const events = world.getBodyEvents();
  assert.ok(events.length >= 1, 'falling body should emit a move event');
  const e = events.find((ev) => ev.userData === 42);
  assert.ok(e, 'move event should carry the body userData tag');
  assert.ok(typeof e.position.y === 'number');
  assert.ok(typeof e.rotation.w === 'number');

  let sleepReported = false;
  for (let i = 0; i < 400 && !sleepReported; i++) {
    world.step(DT, SUBSTEPS);
    for (const ev of world.getBodyEvents()) {
      if (ev.userData === 42 && ev.fellAsleep) {
        sleepReported = true;
      }
    }
  }
  assert.ok(sleepReported, 'body should report falling asleep');

  world.destroy();
  world.delete();
});

test('contact events fire with shape tags', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const ground = world.createBody({ type: 'static', position: { x: 0, y: -0.5, z: 0 } });
  const groundShape = ground.createBox({
    halfExtents: { x: 20, y: 0.5, z: 20 },
    enableContactEvents: true,
  });

  const box = world.createBody({ type: 'dynamic', position: { x: 0, y: 2, z: 0 } });
  const boxShape = box.createBox({
    halfExtents: { x: 0.5, y: 0.5, z: 0.5 },
    density: 1,
    enableContactEvents: true,
  });

  let began = false;
  for (let i = 0; i < 240 && !began; i++) {
    world.step(DT, SUBSTEPS);
    const events = world.getContactEvents();
    for (const e of events.begin) {
      const tags = [e.shapeUserDataA, e.shapeUserDataB];
      if (tags.includes(groundShape.getUserData()) && tags.includes(boxShape.getUserData())) {
        began = true;
      }
    }
  }
  assert.ok(began, 'begin touch event should fire between box and ground');

  world.destroy();
  world.delete();
});

test('sensor events fire when a body passes through', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const sensorBody = world.createBody({ type: 'static', position: { x: 0, y: 1, z: 0 } });
  const sensorShape = sensorBody.createBox({
    halfExtents: { x: 2, y: 0.5, z: 2 },
    isSensor: true,
    enableSensorEvents: true,
  });

  const ball = world.createBody({ type: 'dynamic', position: { x: 0, y: 4, z: 0 } });
  ball.createSphere({ radius: 0.3, density: 1, enableSensorEvents: true });

  let begin = false;
  for (let i = 0; i < 240 && !begin; i++) {
    world.step(DT, SUBSTEPS);
    const events = world.getSensorEvents();
    for (const e of events.begin) {
      if (e.sensorUserData === sensorShape.getUserData()) {
        begin = true;
      }
    }
  }
  assert.ok(begin, 'sensor begin event should fire');

  world.destroy();
  world.delete();
});

test('distance joint holds bodies at rest length', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const anchor = world.createBody({ type: 'static', position: { x: 0, y: 5, z: 0 } });
  const bob = world.createBody({ type: 'dynamic', position: { x: 0, y: 3, z: 0 } });
  bob.createSphere({ radius: 0.2, density: 1 });

  const joint = world.createDistanceJoint(anchor, bob, { length: 2 });
  assert.equal(joint.getType(), 'distance');

  stepSeconds(world, 3);

  const p = bob.getPosition();
  const dist = Math.hypot(p.x - 0, p.y - 5, p.z - 0);
  assert.ok(Math.abs(dist - 2) < 0.05, `bob should hang 2 units below anchor, at distance ${dist}`);

  joint.delete();
  world.destroy();
  world.delete();
});

test('revolute joint motor spins a wheel', () => {
  const world = new b3.World({ gravity: { x: 0, y: 0, z: 0 } });

  const base = world.createBody({ type: 'static', position: { x: 0, y: 0, z: 0 } });
  const wheel = world.createBody({ type: 'dynamic', position: { x: 0, y: 0, z: 0 } });
  wheel.createSphere({ radius: 0.5, density: 1 });

  const joint = world.createRevoluteJoint(base, wheel, {
    enableMotor: true,
    motorSpeed: 5,
    maxMotorTorque: 100,
  });

  stepSeconds(world, 1);

  const w = wheel.getAngularVelocity();
  const spin = Math.hypot(w.x, w.y, w.z);
  assert.ok(spin > 4, `wheel should spin near motor speed, got ${spin}`);

  joint.delete();
  world.destroy();
  world.delete();
});

test('kinematic body moves by velocity and ignores gravity', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const platform = world.createBody({ type: 'kinematic', position: { x: 0, y: 2, z: 0 } });
  platform.createBox({ halfExtents: { x: 1, y: 0.1, z: 1 } });
  platform.setLinearVelocity({ x: 1, y: 0, z: 0 });

  stepSeconds(world, 2);

  const p = platform.getPosition();
  assert.ok(Math.abs(p.x - 2) < 0.02, `platform should travel 2 units, at x=${p.x}`);
  assert.ok(Math.abs(p.y - 2) < 1e-3, 'kinematic body should not fall');

  world.destroy();
  world.delete();
});

test('forces, impulses, and explosions move bodies', () => {
  const world = new b3.World({ gravity: { x: 0, y: 0, z: 0 } });

  const a = world.createBody({ type: 'dynamic', position: { x: 0, y: 0, z: 0 } });
  a.createSphere({ radius: 0.5, density: 1 });
  a.applyLinearImpulseToCenter({ x: 3, y: 0, z: 0 }, true);
  const v = a.getLinearVelocity();
  assert.ok(Math.abs(v.x - 3 / a.getMass()) < 1e-3);

  const b = world.createBody({ type: 'dynamic', position: { x: 0, y: 5, z: 0 } });
  b.createSphere({ radius: 0.5, density: 1 });
  world.explode({ position: { x: 0, y: 4, z: 0 }, radius: 2, falloff: 2, impulsePerArea: 10 });
  world.step(DT, SUBSTEPS);
  assert.ok(b.getLinearVelocity().y > 0.1, 'explosion should push the body away');

  world.destroy();
  world.delete();
});

test('collision filters keep shapes from colliding', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const ground = world.createBody({ type: 'static', position: { x: 0, y: -0.5, z: 0 } });
  ground.createBox({
    halfExtents: { x: 20, y: 0.5, z: 20 },
    filter: { categoryBits: 0x2, maskBits: 0x2 },
  });

  const ghost = world.createBody({ type: 'dynamic', position: { x: 0, y: 2, z: 0 } });
  ghost.createSphere({ radius: 0.5, density: 1, filter: { categoryBits: 0x4, maskBits: 0x4 } });

  stepSeconds(world, 2);

  assert.ok(ghost.getPosition().y < -3, 'filtered body should fall through the ground');

  world.destroy();
  world.delete();
});

test('body and shape userData round trips', () => {
  const world = new b3.World();
  const body = world.createBody({ type: 'dynamic', position: { x: 0, y: 1, z: 0 } });
  const shape = body.createSphere({ radius: 0.5 });

  assert.ok(body.getUserData() > 0, 'auto tag should be assigned');
  assert.ok(shape.getUserData() > 0, 'auto tag should be assigned');
  body.setUserData(1234);
  shape.setUserData(5678);
  assert.equal(body.getUserData(), 1234);
  assert.equal(shape.getUserData(), 5678);

  body.setName('hero');
  assert.equal(body.getName(), 'hero');

  world.destroy();
  world.delete();
});

test('destroying bodies and shapes invalidates them', () => {
  const world = new b3.World();
  const body = world.createBody({ type: 'dynamic', position: { x: 0, y: 1, z: 0 } });
  const shape = body.createSphere({ radius: 0.5 });

  assert.ok(body.isValid());
  assert.ok(shape.isValid());
  shape.destroy(true);
  assert.equal(shape.isValid(), false);
  body.destroy();
  assert.equal(body.isValid(), false);

  world.destroy();
  world.delete();
});

test('motion locks restrict movement', () => {
  const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

  const body = world.createBody({
    type: 'dynamic',
    position: { x: 0, y: 3, z: 0 },
    motionLocks: { linearY: true },
  });
  body.createSphere({ radius: 0.5, density: 1 });

  stepSeconds(world, 1);

  assert.ok(Math.abs(body.getPosition().y - 3) < 1e-3, 'linearY lock should prevent falling');

  world.destroy();
  world.delete();
});
