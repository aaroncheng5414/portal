import test from 'node:test';
import assert from 'node:assert/strict';
import { acceptRecord, apiOrigin, connectionStatus, fetchFrame, fetchResource, frameSequence, parseState, processedRate, projectionPoints } from '../model.js';
import { sampleState } from '../sample.js';

test('accepts a complete state without replacing its raw record or measured positions', () => {
  const state = sampleState(2);
  state.selected.position_m.x = 9;
  assert.equal(parseState(state), state);
  assert.notEqual(state.persons[0].position_m.x, state.selected.position_m.x);
  assert.equal(typeof state.generated_at_ns, 'string');
});

test('rejects malformed coordinates, repeated IDs, and unsupported contracts', () => {
  for (const mutate of [state => { state.schema_version = '2.0'; }, state => { state.persons[0].position_m.z = NaN; }, state => { state.persons[1].id = state.persons[0].id; }, state => { state.coordinates.units = 'cm'; }, state => { state.persons[0].landmarks_px.pop(); }, state => { state.generated_at_ns = 123; }]) {
    const state = sampleState(); mutate(state);
    assert.throws(() => parseState(state), /Invalid API response/);
  }
});

test('accepts an absent selected ID without inventing a person in the vector', () => {
  const state = sampleState(); state.selected.person_id = '99'; state.selected.state = 'lost'; state.selected.stale = true;
  const result = parseState(state);
  assert.equal(result.persons.length, 2);
  assert.ok(!result.persons.some(person => person.id === result.selected.person_id));
});

test('rejects state replay while allowing the same frame with a newer generated timestamp', () => {
  const original = sampleState(5);
  assert.throws(() => acceptRecord(original, sampleState(4)), /older state/);
  const repeated = structuredClone(original); repeated.generated_at_ns = String(BigInt(original.generated_at_ns) + 1000000n); repeated.source.frame_age_ms = 500;
  assert.equal(acceptRecord(original, repeated), repeated);
});

test('repeated successful fetches do not make a frozen frame live', () => {
  const record = sampleState(); record.source.kind = 'camera';
  const status = connectionStatus({ mode: 'connected', record, error: '', now: 2000, receivedAt: 2000, progressedAt: 0 });
  assert.equal(status.title, 'Outdated data'); assert.equal(status.live, false);
});

test('disconnection, source failure, and explicit sample are never reported live', () => {
  const record = sampleState(); record.source.kind = 'camera';
  const input = { record, error: '', now: 10, receivedAt: 10, progressedAt: 10 };
  assert.equal(connectionStatus({ ...input, mode: 'connected' }).live, true);
  for (const mode of ['sample', 'disconnected']) assert.equal(connectionStatus({ ...input, mode }).live, false);
  record.source.status = 'lost'; assert.equal(connectionStatus({ ...input, mode: 'connected' }).title, 'Camera lost');
  record.source.status = 'running'; assert.equal(connectionStatus({ ...input, mode: 'connected', error: 'Network error' }).live, false);
});

test('sample animation is deterministic and changes each vector position independently', () => {
  assert.deepEqual(sampleState(1.5), sampleState(1.5));
  const first = sampleState(0), later = sampleState(2);
  assert.deepEqual(first.persons.map(person => person.id), later.persons.map(person => person.id));
  for (let index = 0; index < first.persons.length; index += 1) assert.notDeepEqual(first.persons[index].position_m, later.persons[index].position_m);
  assert.equal(first.source.kind, 'synthetic'); assert.equal(first.detector.backend, 'synthetic');
});

test('only explicit HTTP origins are accepted', () => {
  assert.equal(apiOrigin('http://localhost:8080/'), 'http://localhost:8080');
  assert.equal(apiOrigin('http://[::1]:8080'), 'http://[::1]:8080');
  for (const url of ['javascript:alert(1)', 'http://user:pass@localhost:8080', 'http://localhost:8080/path', 'http://localhost:8080?secret=x']) assert.throws(() => apiOrigin(url));
});

test('request timeout aborts a stalled request', async () => {
  let aborted = false;
  const fetchImpl = (_url, { signal }) => new Promise((_resolve, reject) => signal.addEventListener('abort', () => { aborted = true; reject(signal.reason); }, { once: true }));
  await assert.rejects(fetchResource('http://localhost/api/v1/state', { timeoutMs: 15, fetchImpl }), /timed out/);
  assert.equal(aborted, true);
});

test('timeout covers the response body, and disconnect cancels pending work', async () => {
  const fetchImpl = async (_url, { signal }) => ({ ok: true, headers: new Headers(), json: () => new Promise((_resolve, reject) => signal.addEventListener('abort', () => reject(signal.reason), { once: true })) });
  await assert.rejects(fetchResource('http://localhost/api/v1/state', { timeoutMs: 15, fetchImpl }), /timed out/);
  const controller = new AbortController();
  const pending = fetchResource('http://localhost/api/v1/state', { signal: controller.signal, fetchImpl });
  await new Promise(resolve => setTimeout(resolve, 1)); controller.abort();
  await assert.rejects(pending, { name: 'AbortError' });
});

test('a pre-cancelled request never starts and HTTP failures are surfaced', async () => {
  const controller = new AbortController(); controller.abort();
  let started = false;
  await assert.rejects(fetchResource('http://localhost/', { signal: controller.signal, fetchImpl: async () => { started = true; } }), { name: 'AbortError' });
  assert.equal(started, false);
  await assert.rejects(fetchResource('http://localhost/', { fetchImpl: async () => ({ ok: false, status: 503 }) }), /503/);
});

test('all three plots use measured vector coordinates and the active ID', () => {
  const record = sampleState();
  record.persons[0].position_m = { x: -0.45, y: 0.8, z: 2.3 };
  record.persons[1].observed = false;
  record.selected.position_m = { x: 99, y: 99, z: 99 };
  for (const [plane, expected] of [['xy', [-0.45, 0.8]], ['xz', [-0.45, 2.3]], ['yz', [0.8, 2.3]]]) {
    const points = projectionPoints(record, plane);
    assert.equal(points.length, record.persons.length);
    assert.deepEqual([points[0].horizontal, points[0].vertical], expected);
    assert.equal(points[0].active, true);
    assert.equal(points[1].observed, false);
  }
  record.selected.person_id = '99';
  assert.equal(projectionPoints(record, 'xy').filter(point => point.active).length, 0);
  assert.deepEqual(projectionPoints(null, 'xz'), []);
  assert.throws(() => projectionPoints(record, 'zz'), /Unknown/);
});

test('processed FPS comes from backend throughput and disappears for stale or disconnected data', () => {
  const record = sampleState(); record.source.kind = 'camera'; record.source.inference_fps = 17.25; record.source.capture_fps = 60;
  const input = { mode: 'connected', record, error: '', now: 100, receivedAt: 100, progressedAt: 100 };
  assert.equal(processedRate(input).value, 17.25);
  for (const changed of [{ mode: 'disconnected' }, { mode: 'sample' }, { error: 'Network down' }, { now: 1700 }]) assert.equal(processedRate({ ...input, ...changed }).value, null);
  record.source.status = 'lost'; assert.equal(processedRate(input).value, null);
  record.source.status = 'running'; record.source.kind = 'synthetic';
  assert.deepEqual(processedRate(input), { value: 17.25, label: 'Processed', synthetic: true });
  assert.equal(processedRate({ ...input, mode: 'sample' }).value, null);
});

test('camera sequence detects duplicates and replays without inventing alignment with JSON state', () => {
  assert.deepEqual(frameSequence('14', 13), { sequence: 14, changed: true });
  assert.deepEqual(frameSequence('14', 14), { sequence: 14, changed: false });
  assert.deepEqual(frameSequence(null, 14), { sequence: null, changed: true });
  assert.throws(() => frameSequence('13', 14), /older frame/);
  for (const header of ['garbage', '-1', '9007199254740992']) assert.throws(() => frameSequence(header), /Invalid/);
});

test('JPEG fetch retains the exact blob and its own frame sequence', async () => {
  const blob = new Blob([new Uint8Array([255, 216, 255, 217])], { type: 'image/jpeg' });
  const headers = new Headers({ 'X-Frame-Sequence': '23' });
  const result = await fetchFrame('http://localhost/api/v1/frame.jpg', { previousSequence: 21, fetchImpl: async () => ({ ok: true, headers, blob: async () => blob }) });
  assert.equal(result.blob, blob); assert.equal(result.sequence, 23); assert.equal(result.changed, true);
  await assert.rejects(fetchFrame('http://localhost/api/v1/frame.jpg', { fetchImpl: async () => ({ ok: true, headers, blob: async () => new Blob(['not a JPEG'], { type: 'text/html' }) }) }), /JPEG/);
});

test('image download body is cancelled on disconnect and bounded by a timeout', async () => {
  const fetchImpl = async (_url, { signal }) => ({ ok: true, headers: new Headers(), blob: () => new Promise((_resolve, reject) => signal.addEventListener('abort', () => reject(signal.reason), { once: true })) });
  await assert.rejects(fetchFrame('http://localhost/api/v1/frame.jpg', { timeoutMs: 15, fetchImpl }), /timed out/);
  const controller = new AbortController();
  const pending = fetchFrame('http://localhost/api/v1/frame.jpg', { signal: controller.signal, fetchImpl });
  await new Promise(resolve => setTimeout(resolve, 1)); controller.abort();
  await assert.rejects(pending, { name: 'AbortError' });
});
