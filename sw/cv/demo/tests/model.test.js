import test from 'node:test';
import assert from 'node:assert/strict';
import { acceptRecord, apiOrigin, connectionStatus, fetchResource, parseState } from '../model.js';
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
