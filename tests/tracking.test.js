import assert from 'node:assert/strict';
import test from 'node:test';
import { FaceLandmarker, FilesetResolver } from '@mediapipe/tasks-vision';
import { createHeadTracker, estimateViewerPosition } from '../src/tracking.js';

const centered = { x: 0.5, y: 0.5, width: 0.2 };

test('the calibrated face produces a centered viewpoint at the original distance', () => {
  assert.deepEqual(estimateViewerPosition(centered, centered), { x: 0, y: 0, z: 1 });
});

test('unmirrored camera coordinates map to viewer-right and up', () => {
  const rightAndUp = estimateViewerPosition({ x: 0.4, y: 0.4, width: 0.2 }, centered);
  const leftAndDown = estimateViewerPosition({ x: 0.6, y: 0.6, width: 0.2 }, centered);
  assert.ok(rightAndUp.x > 0 && rightAndUp.y > 0);
  assert.ok(leftAndDown.x < 0 && leftAndDown.y < 0);
  assert.equal(rightAndUp.z, 1);
});

test('larger eye spacing moves the viewpoint closer and smaller spacing moves it away', () => {
  const closer = estimateViewerPosition({ ...centered, width: 0.25 }, centered);
  const farther = estimateViewerPosition({ ...centered, width: 0.16 }, centered);
  assert.equal(closer.z, 0.8);
  assert.equal(farther.z, 1.25);
  assert.equal(closer.x, 0);
  assert.equal(farther.y, 0);
});

test('extreme valid measurements stay inside the demo viewing range', () => {
  assert.deepEqual(
    estimateViewerPosition({ x: -5, y: 5, width: 0.001 }, centered),
    { x: 1, y: -1, z: 1.6 },
  );
  assert.deepEqual(
    estimateViewerPosition({ x: 5, y: -5, width: 5 }, centered),
    { x: -1, y: 1, z: 0.65 },
  );
});

function deferred() {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
}

function makeStream(t) {
  const track = new EventTarget();
  track.stop = t.mock.fn();
  return { track, getTracks: () => [track], getVideoTracks: () => [track] };
}

function makeModel(t) {
  return { close: t.mock.fn(), detectForVideo: () => ({ faceLandmarks: [] }) };
}

function browserFixture(t) {
  const frames = new Map();
  const trackers = [];
  let nextFrame = 0;
  const mediaDevices = {};
  const globals = {
    window: { isSecureContext: true },
    document: { visibilityState: 'visible' },
    navigator: { mediaDevices },
    requestAnimationFrame: (callback) => { frames.set(++nextFrame, callback); return nextFrame; },
    cancelAnimationFrame: (id) => frames.delete(id),
  };
  const originals = Object.fromEntries(Object.keys(globals).map((key) => [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
  for (const [key, value] of Object.entries(globals)) {
    Object.defineProperty(globalThis, key, { configurable: true, value });
  }
  t.after(() => {
    trackers.forEach((tracker) => tracker.stop());
    for (const [key, descriptor] of Object.entries(originals)) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor);
      else delete globalThis[key];
    }
  });
  t.mock.method(FilesetResolver, 'forVisionTasks', async () => ({}));
  const video = Object.assign(new EventTarget(), {
    readyState: 2, videoWidth: 640, currentTime: 0, srcObject: null,
    play: async () => {}, pause() {},
  });
  const statuses = [];
  const tracker = createHeadTracker({ video, onStatus: (status) => statuses.push(status) });
  trackers.push(tracker);
  return { mediaDevices, video, tracker, frames, statuses };
}

test('a camera permission result arriving after stop cannot replace the new session', async (t) => {
  const { mediaDevices, video, tracker, frames } = browserFixture(t);
  const permission = deferred();
  const abandoned = makeStream(t);
  const active = makeStream(t);
  const model = makeModel(t);
  let requests = 0;
  mediaDevices.getUserMedia = t.mock.fn(() => ++requests === 1 ? permission.promise : Promise.resolve(active));
  t.mock.method(FaceLandmarker, 'createFromOptions', async () => model);

  const oldStart = tracker.start();
  tracker.stop();
  assert.equal(await tracker.start(), true);
  permission.resolve(abandoned);
  assert.equal(await oldStart, false);
  assert.equal(abandoned.track.stop.mock.callCount(), 1);
  assert.equal(active.track.stop.mock.callCount(), 0);
  assert.equal(video.srcObject, active);
  assert.equal(tracker.running, true);
  assert.equal(mediaDevices.getUserMedia.mock.calls[0].arguments[0].audio, false);

  tracker.stop();
  assert.equal(active.track.stop.mock.callCount(), 1);
  assert.equal(model.close.mock.callCount(), 1);
  assert.equal(video.srcObject, null);
  assert.equal(frames.size, 0);
});

test('a model finishing after stop is closed without disposing the replacement session', async (t) => {
  const { mediaDevices, video, tracker } = browserFixture(t);
  const firstStream = makeStream(t);
  const activeStream = makeStream(t);
  const abandonedModel = makeModel(t);
  const activeModel = makeModel(t);
  const loading = deferred();
  const modelRequested = deferred();
  let cameraRequests = 0;
  let modelRequests = 0;
  mediaDevices.getUserMedia = async () => ++cameraRequests === 1 ? firstStream : activeStream;
  t.mock.method(FaceLandmarker, 'createFromOptions', async () => {
    if (++modelRequests === 1) { modelRequested.resolve(); return loading.promise; }
    return activeModel;
  });

  const oldStart = tracker.start();
  await modelRequested.promise;
  tracker.stop();
  assert.equal(await tracker.start(), true);
  loading.resolve(abandonedModel);
  assert.equal(await oldStart, false);
  assert.equal(abandonedModel.close.mock.callCount(), 1);
  assert.equal(firstStream.track.stop.mock.callCount(), 1);
  assert.equal(activeModel.close.mock.callCount(), 0);
  assert.equal(activeStream.track.stop.mock.callCount(), 0);
  assert.equal(video.srcObject, activeStream);
  assert.equal(tracker.running, true);
});

test('GPU initialization failure retries CPU and stop releases the successful model', async (t) => {
  const { mediaDevices, tracker, frames } = browserFixture(t);
  const stream = makeStream(t);
  const model = makeModel(t);
  const delegates = [];
  mediaDevices.getUserMedia = async () => stream;
  t.mock.method(FaceLandmarker, 'createFromOptions', async (_, options) => {
    delegates.push(options.baseOptions.delegate);
    if (options.baseOptions.delegate === 'GPU') throw new Error('GPU unavailable');
    return model;
  });

  assert.equal(await tracker.start(), true);
  assert.deepEqual(delegates, ['GPU', 'CPU']);
  tracker.stop();
  assert.equal(tracker.running, false);
  assert.equal(model.close.mock.callCount(), 1);
  assert.equal(stream.track.stop.mock.callCount(), 1);
  assert.equal(frames.size, 0);
});

test('denied camera permission reports a usable error without starting the model', async (t) => {
  const { mediaDevices, tracker, frames, statuses } = browserFixture(t);
  mediaDevices.getUserMedia = async () => { throw new DOMException('Denied', 'NotAllowedError'); };
  const createModel = t.mock.method(FaceLandmarker, 'createFromOptions', async () => makeModel(t));
  assert.equal(await tracker.start(), false);
  assert.equal(tracker.running, false);
  assert.equal(createModel.mock.callCount(), 0);
  assert.equal(frames.size, 0);
  assert.equal(statuses.at(-1).state, 'error');
  assert.match(statuses.at(-1).message, /permission was denied/i);
});
