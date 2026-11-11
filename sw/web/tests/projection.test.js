import test from 'node:test';
import assert from 'node:assert/strict';
import { Matrix4, Vector3 } from 'three';
import { computePortalFrustum } from '../src/projection.js';

const defaults = { width: 8, height: 5, eyeX: 0, eyeY: 0, eyeZ: 6, near: 0.05, far: 180 };
const close = (actual, expected) => assert.ok(Math.abs(actual - expected) < 1e-10, `${actual} should equal ${expected}`);

test('a centered viewer has a symmetric frustum with the physical screen aspect ratio', () => {
  const frustum = computePortalFrustum(defaults);
  close(frustum.left, -frustum.right);
  close(frustum.bottom, -frustum.top);
  close((frustum.right - frustum.left) / (frustum.top - frustum.bottom), defaults.width / defaults.height);
  assert.equal(frustum.near, defaults.near);
  assert.equal(frustum.far, defaults.far);
});

test('all physical screen corners remain at the canvas edges when the viewer moves', () => {
  const views = [[0, 0, 6], [1.8, 1.15, 3.3], [-1.8, -1.15, 10.8], [1.2, -0.7, 5.2]];
  for (const [eyeX, eyeY, eyeZ] of views) {
    for (const [width, height] of [[8, 5], [8, 12]]) {
      const frustum = computePortalFrustum({ ...defaults, width, height, eyeX, eyeY, eyeZ });
      const projection = new Matrix4().makePerspective(frustum.left, frustum.right, frustum.top, frustum.bottom, frustum.near, frustum.far);
      for (const xSign of [-1, 1]) {
        for (const ySign of [-1, 1]) {
          const corner = new Vector3(xSign * width / 2 - eyeX, ySign * height / 2 - eyeY, -eyeZ).applyMatrix4(projection);
          close(corner.x, xSign);
          close(corner.y, ySign);
          assert.ok(corner.z > -1 && corner.z < 1, 'the screen must lie within the clipping range');
        }
      }
    }
  }
});

test('rejects invalid geometry and clipping planes before they can produce a broken matrix', () => {
  const invalid = [
    { width: 0 }, { width: -1 }, { height: 0 }, { height: NaN },
    { eyeX: Infinity }, { eyeY: undefined }, { eyeZ: 0 }, { eyeZ: -1 },
    { near: 0 }, { near: -1 }, { far: 0.05 }, { far: 0.01 }, { far: Infinity },
  ];
  for (const change of invalid) {
    assert.throws(() => computePortalFrustum({ ...defaults, ...change }), RangeError);
  }
});
