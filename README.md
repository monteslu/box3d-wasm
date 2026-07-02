# box3d

[Box3D](https://github.com/erincatto/box3d) compiled to WebAssembly, with SIMD and optional wasm threads. Works in browsers and Node.js from a single package.

Box3D is a 3D rigid body physics engine written by Erin Catto, the author of Box2D. All engine design and implementation credit belongs to him. This package only compiles his library to wasm and adds a JavaScript binding layer. Box3D is MIT licensed by Erin Catto; the full upstream license ships in this package as LICENSE.box3d.txt.

## Install

```bash
npm i box3d
```

## Quick start

```js
import Box3D from 'box3d';

const b3 = await Box3D();

const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 } });

const ground = world.createBody({ type: 'static', position: { x: 0, y: -0.5, z: 0 } });
ground.createBox({ halfExtents: { x: 20, y: 0.5, z: 20 } });

const body = world.createBody({ type: 'dynamic', position: { x: 0, y: 5, z: 0 } });
body.createBox({ halfExtents: { x: 0.5, y: 0.5, z: 0.5 }, density: 1, friction: 0.5 });

for (let i = 0; i < 120; i++) {
  world.step(1 / 60, 4);
}

console.log(body.getPosition()); // { x: ~0, y: ~0.5, z: ~0 }
```

The same code runs in Node.js and in the browser. The wasm file is loaded relative to the module, so bundlers that understand `new URL(..., import.meta.url)` (Vite, webpack 5, Rollup) pick it up automatically.

## Flavours

| import | SIMD | threads | notes |
| --- | --- | --- | --- |
| `box3d` | yes | no | works everywhere, no special headers |
| `box3d/deluxe` | yes | yes | needs cross-origin isolation in browsers |

```js
import Box3D from 'box3d/deluxe';

const b3 = await Box3D();
const world = new b3.World({ gravity: { x: 0, y: -10, z: 0 }, workerCount: 4 });
```

`workerCount` enables Box3D's internal multithreaded solver. It is clamped to `[1, b3.maxWorkers]`. The standard build ignores it and always runs single threaded. Check `b3.threaded` at runtime to see which build you have.

### Serving requirements for threads

Wasm threads use SharedArrayBuffer. Browsers require the page to be cross-origin isolated, so serve your app with:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Node.js needs no special setup; worker threads are used automatically.

## API overview

Vectors are plain objects `{ x, y, z }` and quaternions are `{ x, y, z, w }`, so values pass directly to and from libraries like three.js.

### World

```js
const world = new b3.World({
  gravity: { x: 0, y: -10, z: 0 },
  enableSleep: true,
  enableContinuous: true,
  workerCount: 4, // deluxe build only
});

world.step(1 / 60, 4);          // timeStep, subStepCount
world.setGravity({ x: 0, y: -3.7, z: 0 });
world.getAwakeBodyCount();
world.castRayClosest(origin, translation, filter);
world.explode({ position, radius: 3, falloff: 2, impulsePerArea: 10 });
world.getBodyEvents();          // [{ userData, position, rotation, fellAsleep }]
world.getContactEvents();       // { begin: [...], end: [...], hit: [...] }
world.getSensorEvents();        // { begin: [...], end: [...] }
world.destroy();
```

### Bodies

```js
const body = world.createBody({
  type: 'dynamic',              // 'static' | 'kinematic' | 'dynamic'
  position: { x: 0, y: 5, z: 0 },
  rotation: { x: 0, y: 0, z: 0, w: 1 },
  linearVelocity: { x: 0, y: 0, z: 0 },
  angularDamping: 0.05,
  motionLocks: { angularX: true, angularZ: true },
});

body.getPosition(); body.getRotation(); body.getTransform();
body.setLinearVelocity(v); body.applyLinearImpulseToCenter(v, true);
body.applyForce(force, worldPoint, true); body.applyTorque(t, true);
body.getMass(); body.isAwake(); body.setAwake(true);
body.destroy();
```

### Shapes

Each shape creator takes one options object with the geometry plus material fields (`density`, `friction`, `restitution`, `isSensor`, `filter`, event flags):

```js
body.createBox({ halfExtents: { x: 1, y: 0.5, z: 2 }, friction: 0.7 });
body.createSphere({ radius: 0.5, restitution: 0.8 });
body.createCapsule({ height: 1.2, radius: 0.3 });
body.createHull({ points: [{ x, y, z }, ...] });
```

### Joints

Distance, revolute, spherical, prismatic, weld, motor, wheel, parallel, and filter joints are bound:

```js
const hinge = world.createRevoluteJoint(bodyA, bodyB, {
  localFrameA: { position: { x: 0, y: 1, z: 0 } },
  enableMotor: true,
  motorSpeed: 5,
  maxMotorTorque: 100,
});
hinge.getAngle();
```

### Events and userData tags

Every body and shape gets an auto-assigned numeric `userData` tag (you can overwrite it with your own number). Event arrays reference these tags, so you can map physics events back to your scene objects with a plain `Map`.

### Memory notes

Wrapper objects returned by embind (`World`, `Body`, `Shape`, joints) are tiny handles. Call `.delete()` when you no longer need the JS handle, and `.destroy()` to remove the underlying object from the simulation. Destroying a world frees every body, shape, and joint inside it.

## Building from source

Requires [emsdk](https://emscripten.org/docs/getting_started/downloads.html) (tested with 4.0.18), CMake, and Node 22+.

```bash
npm ci
npm run fetch-deps   # clones Box3D at the SHA pinned in scripts/versions.json
npm run build        # builds standard and deluxe flavours into dist/
npm test
```

## License

MIT for the wrapper and build scripts, see LICENSE.

Box3D itself is Copyright (c) Erin Catto and MIT licensed, see LICENSE.box3d.txt and the upstream repository at https://github.com/erincatto/box3d. If you use this package, the physics engine you are running is his work.
