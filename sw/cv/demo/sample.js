export function sampleState(seconds = 0) {
  const t = Math.max(0, seconds);
  const stamp = Math.round((100 + t) * 1e9);
  const positions = [{ x: -0.48 + Math.sin(t * 0.48) * 0.32, y: 0.12, z: 1.65 + Math.sin(t * 0.31) * 0.16 }, { x: 0.64 + Math.sin(t * 0.32 + 1) * 0.23, y: -0.06, z: 2.75 + Math.cos(t * 0.25) * 0.2 }];
  const persons = positions.map((position_m, index) => {
    const width = 105 / position_m.z;
    const center = { x: 480 + 610 * position_m.x / position_m.z, y: 330 - 610 * position_m.y / position_m.z };
    return { id: String(index + 1), position_m, velocity_mps: { x: index ? 0.23 * 0.32 * Math.cos(t * 0.32 + 1) : 0.32 * 0.48 * Math.cos(t * 0.48), y: 0, z: 0 }, age_ms: 28, observed: true, box_px: { x: center.x - width, y: center.y - width * 1.2, width: width * 2, height: width * 2.6 }, landmarks_px: [{ x: center.x - width * 0.35, y: center.y - width * 0.2 }, { x: center.x + width * 0.35, y: center.y - width * 0.2 }, { x: center.x, y: center.y + width * 0.2 }, { x: center.x - width * 0.25, y: center.y + width * 0.55 }, { x: center.x + width * 0.25, y: center.y + width * 0.55 }] };
  });
  return { schema_version: '1.0', sequence: Math.floor(t * 30), generated_at_ns: String(stamp), source: { kind: 'synthetic', device: null, status: 'running', error: null, width: 960, height: 720, requested_fps: 30, capture_fps: 30, inference_fps: 26.4, frame_age_ms: 28, timestamp_basis: 'frame_received' }, detector: { backend: 'synthetic', target: null, model: null, inference_ms: 12.1 }, coordinates: { frame: 'camera', units: 'm', axes: { x: 'image_right', y: 'up', z: 'forward' }, calibration: 'synthetic', ipd_m: 0.063, focal_length_px: 610 }, selected: { person_id: '1', position_m: { ...positions[0] }, velocity_mps: { ...persons[0].velocity_mps }, observation_timestamp_ns: String(stamp - 28e6), age_ms: 28, state: 'locked', predicted: false, stale: false }, persons, video: { snapshot_url: '/api/v1/frame.jpg', frame_sequence: Math.floor(t * 30) } };
}
