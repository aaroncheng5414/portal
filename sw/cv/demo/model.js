const isObject = value => value !== null && typeof value === 'object' && !Array.isArray(value);
const finite = value => typeof value === 'number' && Number.isFinite(value);
const unsigned = value => Number.isSafeInteger(value) && value >= 0;
const decimal = value => typeof value === 'string' && /^\d+$/.test(value);
const point = value => isObject(value) && finite(value.x) && finite(value.y);
const position = value => point(value) && finite(value.z);
const nullable = (value, predicate) => value === null || predicate(value);
const member = (value, values) => values.includes(value);
const nonnegative = value => finite(value) && value >= 0;

export function parseState(value) {
  const fail = message => { throw new Error(`Invalid API response: ${message}`); };
  if (!isObject(value) || value.schema_version !== '1.0') fail('expected schema_version 1.0');
  if (!unsigned(value.sequence) || !decimal(value.generated_at_ns)) fail('sequence or timestamp');
  const source = value.source;
  if (!isObject(source) || !member(source.kind, ['camera', 'synthetic']) || !member(source.status, ['starting', 'running', 'lost', 'error']) || source.timestamp_basis !== 'frame_received') fail('source');
  if (!nullable(source.device, v => typeof v === 'string') || !nullable(source.error, v => typeof v === 'string') || !unsigned(source.width) || !unsigned(source.height)) fail('camera fields');
  if (![source.requested_fps, source.capture_fps, source.inference_fps].every(nonnegative) || !nullable(source.frame_age_ms, nonnegative)) fail('source rates or age');
  const detector = value.detector;
  if (!isObject(detector) || !member(detector.backend, ['cuda', 'cpu', 'synthetic']) || !member(detector.target, ['fp32', 'fp16', null]) || !nullable(detector.model, v => typeof v === 'string') || !nullable(detector.inference_ms, nonnegative)) fail('detector');
  const coordinates = value.coordinates;
  if (!isObject(coordinates) || coordinates.frame !== 'camera' || coordinates.units !== 'm' || !isObject(coordinates.axes) || coordinates.axes.x !== 'image_right' || coordinates.axes.y !== 'up' || coordinates.axes.z !== 'forward' || !member(coordinates.calibration, ['approximate_ipd', 'synthetic']) || !nonnegative(coordinates.ipd_m) || !nonnegative(coordinates.focal_length_px)) fail('coordinate convention');
  const selected = value.selected;
  if (!isObject(selected) || !nullable(selected.person_id, decimal) || !nullable(selected.position_m, position) || !nullable(selected.velocity_mps, position) || !nullable(selected.observation_timestamp_ns, decimal) || !nullable(selected.age_ms, nonnegative) || !member(selected.state, ['acquiring', 'locked', 'lost']) || typeof selected.predicted !== 'boolean' || typeof selected.stale !== 'boolean') fail('selected person');
  if (!Array.isArray(value.persons)) fail('persons must be an array');
  const ids = new Set();
  for (const person of value.persons) {
    if (!isObject(person) || !decimal(person.id) || ids.has(person.id) || !position(person.position_m) || !position(person.velocity_mps) || !nonnegative(person.age_ms) || typeof person.observed !== 'boolean') fail('person record or duplicate ID');
    if (!point(person.box_px) || !finite(person.box_px.width) || person.box_px.width <= 0 || !finite(person.box_px.height) || person.box_px.height <= 0 || !Array.isArray(person.landmarks_px) || person.landmarks_px.length !== 5 || !person.landmarks_px.every(point)) fail('person box or five landmarks');
    ids.add(person.id);
  }
  if (!isObject(value.video) || value.video.snapshot_url !== '/api/v1/frame.jpg' || !unsigned(value.video.frame_sequence)) fail('video');
  return value;
}

export function apiOrigin(input) {
  const url = new URL(input);
  if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password || url.search || url.hash || !['', '/'].includes(url.pathname)) throw new Error('Use an HTTP or HTTPS origin, such as http://localhost:8080.');
  return url.origin;
}

export function acceptRecord(previous, next) {
  parseState(next);
  if (previous && (next.sequence < previous.sequence || BigInt(next.generated_at_ns) < BigInt(previous.generated_at_ns))) throw new Error('Backend returned an older state. Reconnect if the service restarted.');
  return next;
}

export function connectionStatus({ mode, record, error, now, receivedAt, progressedAt }) {
  if (mode === 'sample') return { tone: 'warning', title: 'Sample mode', message: 'Animated synthetic data. No camera or backend is connected.', live: false };
  if (mode === 'disconnected') return { tone: 'neutral', title: 'Disconnected', message: record ? 'Last displayed data is held. It is not live.' : 'Connect to see positions, or choose Sample.', live: false };
  if (error) return { tone: 'error', title: 'Connection error', message: `${error} Last received data is held.`, live: false };
  if (!record) return { tone: 'neutral', title: 'Connecting', message: 'Waiting for the backend state.', live: false };
  if (record.source.status === 'error' || record.source.status === 'lost') return { tone: 'error', title: record.source.status === 'lost' ? 'Camera lost' : 'Backend error', message: record.source.error || 'The source is not producing frames.', live: false };
  if (record.source.status === 'starting') return { tone: 'warning', title: 'Starting', message: 'The backend is starting its camera and detector.', live: false };
  const age = record.source.frame_age_ms === null ? Infinity : record.source.frame_age_ms + Math.max(0, now - receivedAt);
  if (age > 1500 || now - progressedAt > 1500) return { tone: 'warning', title: 'Outdated data', message: 'No recent camera frame. Values are held until the source catches up.', live: false };
  if (record.source.kind === 'synthetic') return { tone: 'warning', title: 'Synthetic backend', message: 'Connected to a backend generating synthetic observations.', live: false };
  return { tone: 'good', title: 'Connected', message: 'Receiving tracked positions.', live: true };
}

export function processedRate(input) {
  const { mode, record, error, now, receivedAt, progressedAt } = input;
  if (mode === 'sample') return { value: null, label: 'Sample', synthetic: true };
  const age = record?.source.frame_age_ms;
  const fresh = mode === 'connected' && !error && record?.source.status === 'running' && age !== null && age !== undefined && age + Math.max(0, now - receivedAt) <= 1500 && now - progressedAt <= 1500;
  return { value: fresh ? record.source.inference_fps : null, label: 'Processed', synthetic: fresh && record.source.kind === 'synthetic' };
}

export const projectionAxes = Object.freeze({ xy: ['x', 'y'], xz: ['x', 'z'], yz: ['y', 'z'] });

export function projectionPoints(record, plane) {
  const axes = projectionAxes[plane];
  if (!axes) throw new Error('Unknown coordinate plane.');
  return (record?.persons || []).map(person => ({ id: person.id, horizontal: person.position_m[axes[0]], vertical: person.position_m[axes[1]], position_m: person.position_m, observed: person.observed, active: record.selected.person_id === person.id }));
}

export function frameSequence(header, previous = null) {
  if (header === null) return { sequence: null, changed: true };
  const sequence = /^\d+$/.test(header) ? Number(header) : NaN;
  if (!unsigned(sequence)) throw new Error('Invalid camera frame sequence.');
  if (previous !== null && sequence < previous) throw new Error('Camera returned an older frame. Reconnect if the service restarted.');
  return { sequence, changed: sequence !== previous };
}

export async function fetchFrame(url, options = {}) {
  const { previousSequence = null, ...request } = options;
  const { body, headers } = await fetchResource(url, { ...request, responseType: 'blob' });
  if (!body.size || body.type.split(';')[0].toLowerCase() !== 'image/jpeg') throw new Error('The frame endpoint did not return a JPEG image.');
  return { blob: body, ...frameSequence(headers.get('X-Frame-Sequence'), previousSequence) };
}

export async function fetchResource(url, { signal, timeoutMs = 2500, fetchImpl = fetch, responseType = 'json' } = {}) {
  const controller = new AbortController();
  const abort = () => controller.abort(signal.reason || new DOMException('Request cancelled', 'AbortError'));
  if (signal?.aborted) abort();
  else signal?.addEventListener('abort', abort, { once: true });
  const timer = setTimeout(() => controller.abort(new Error(`Request timed out after ${timeoutMs} ms.`)), timeoutMs);
  try {
    if (controller.signal.aborted) throw controller.signal.reason;
    const response = await fetchImpl(url, { signal: controller.signal, cache: 'no-store', mode: 'cors', credentials: 'omit' });
    if (!response.ok) throw new Error(`Backend returned HTTP ${response.status}.`);
    const body = responseType === 'blob' ? await response.blob() : await response.json();
    return { body, headers: response.headers };
  } catch (error) {
    if (controller.signal.aborted) throw controller.signal.reason;
    throw error;
  } finally {
    clearTimeout(timer);
    signal?.removeEventListener('abort', abort);
  }
}
