/**
 * Frustum through a fixed screen at z=0, viewed from (eyeX, eyeY, eyeZ).
 * The camera looks along -Z without rotating. Dimensions use world units.
 */
export function computePortalFrustum({ width, height, eyeX, eyeY, eyeZ, near, far }) {
  const values = [width, height, eyeX, eyeY, eyeZ, near, far];
  if (!values.every(Number.isFinite) || width <= 0 || height <= 0 || eyeZ <= 0 || near <= 0 || far <= near) {
    throw new RangeError('Portal projection requires positive dimensions, eye distance, and valid clipping planes.');
  }
  const scale = near / eyeZ;
  return {
    left: (-width / 2 - eyeX) * scale,
    right: (width / 2 - eyeX) * scale,
    bottom: (-height / 2 - eyeY) * scale,
    top: (height / 2 - eyeY) * scale,
    near,
    far,
  };
}
