import { FaceLandmarker, FilesetResolver } from '@mediapipe/tasks-vision';

const LOST_AFTER_MS = 650;
const CENTERING_FRAMES = 5;
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

/**
 * Convert a face measurement to the demo's viewer coordinates.
 * Eye spacing gives relative distance, not a calibrated measurement in meters.
 * Camera pixels are unmirrored, so image-left is the viewer's right.
 */
export function estimateViewerPosition(face, baseline) {
  const distance = clamp(baseline.width / face.width, 0.65, 1.6);
  return {
    x: clamp(((baseline.x - face.x) / baseline.width) * distance * 0.6, -1, 1),
    y: clamp(((baseline.y - face.y) / baseline.width) * distance * 0.6, -1, 1),
    z: distance,
  };
}

function measureFace(landmarks) {
  // Average the inner/outer corners of each eye to reduce eyelid jitter.
  const corners = [33, 133, 362, 263].map((index) => landmarks?.[index]);
  if (corners.some((point) => !point || !Number.isFinite(point.x) || !Number.isFinite(point.y))) {
    return null;
  }
  const left = { x: (corners[0].x + corners[1].x) / 2, y: (corners[0].y + corners[1].y) / 2 };
  const right = { x: (corners[2].x + corners[3].x) / 2, y: (corners[2].y + corners[3].y) / 2 };
  const width = Math.hypot(right.x - left.x, right.y - left.y);
  if (width < 0.025) return null;
  return { x: (left.x + right.x) / 2, y: (left.y + right.y) / 2, width };
}

function averageFaces(samples) {
  return samples.reduce((average, face) => ({
    x: average.x + face.x / samples.length,
    y: average.y + face.y / samples.length,
    width: average.width + face.width / samples.length,
  }), { x: 0, y: 0, width: 0 });
}

function describeError(error, stage) {
  if (error?.name === 'NotAllowedError' || error?.name === 'PermissionDeniedError') {
    return 'Camera permission was denied. Allow the camera in your browser or use the cursor.';
  }
  if (error?.name === 'NotFoundError' || error?.name === 'DevicesNotFoundError') {
    return 'No camera was found. Connect a webcam or use the cursor.';
  }
  if (error?.name === 'NotReadableError' || error?.name === 'TrackStartError') {
    return 'The camera is busy or unavailable. Close other camera apps and try again.';
  }
  if (stage === 'model') return 'The face tracker could not load. Check your connection and try again, or use the cursor.';
  if (stage === 'inference') return 'Face tracking stopped. Try starting the camera again or use the cursor.';
  return 'The camera could not start. Try again or use the cursor.';
}

/**
 * Opt-in, entirely in-browser face tracking. No video is uploaded or recorded.
 * start() resolves true after setup, false after cancellation or a reported error.
 * The caller owns rendering and smooth interpolation of onPosition updates.
 */
export function createHeadTracker({ video, onPosition = () => {}, onStatus = () => {} }) {
  if (!video) throw new Error('createHeadTracker requires a video element.');

  let current = null;
  let lastStatus = '';
  const base = (import.meta.env?.BASE_URL || '/').replace(/\/?$/, '/');
  const isCurrent = (context) => current === context && !context.disposed;

  function status(state, message) {
    const key = `${state}:${message}`;
    if (key !== lastStatus) {
      lastStatus = key;
      onStatus({ state, message });
    }
  }

  function dispose(context) {
    if (!context || context.disposed) return;
    context.disposed = true;
    context.running = false;
    cancelAnimationFrame(context.raf);
    context.abort.abort();
    context.removeTrackListeners?.();
    context.stream?.getTracks().forEach((track) => track.stop());
    if (video.srcObject === context.stream) {
      video.pause();
      video.srcObject = null;
    }
    try { context.landmarker?.close(); } catch { /* Already released by the runtime. */ }
    context.landmarker = null;
  }

  function fail(context, error, stage) {
    if (!isCurrent(context)) return;
    current = null;
    dispose(context);
    status('error', describeError(error, stage));
  }

  function waitForVideo(context) {
    if (video.readyState >= 2 && video.videoWidth > 0) return Promise.resolve();
    return new Promise((resolve, reject) => {
      let timer;
      const cleanup = () => {
        clearTimeout(timer);
        video.removeEventListener('loadeddata', ready);
        video.removeEventListener('error', failed);
        context.abort.signal.removeEventListener('abort', cancelled);
      };
      const ready = () => { cleanup(); resolve(); };
      const failed = () => { cleanup(); reject(new Error('Camera video did not load.')); };
      const cancelled = () => { cleanup(); reject(new DOMException('Cancelled', 'AbortError')); };
      video.addEventListener('loadeddata', ready, { once: true });
      video.addEventListener('error', failed, { once: true });
      context.abort.signal.addEventListener('abort', cancelled, { once: true });
      timer = setTimeout(failed, 12000);
      if (context.abort.signal.aborted) cancelled();
    });
  }

  function tick(context, now) {
    if (!isCurrent(context)) return;
    if (document.visibilityState !== 'hidden' && video.readyState >= 2 &&
        video.currentTime !== context.lastVideoTime) {
      context.lastVideoTime = video.currentTime;
      try {
        const result = context.landmarker.detectForVideo(video, now);
        const face = measureFace(result.faceLandmarks?.[0]);
        if (face) {
          context.lastFace = face;
          context.lastSeen = now;
          if (!context.baseline) {
            context.samples.push(face);
            if (context.samples.length < CENTERING_FRAMES) {
              status('loading', 'Hold still to center the view.');
            } else {
              context.baseline = averageFaces(context.samples);
              context.samples = [];
            }
          }
          if (context.baseline) {
            onPosition(estimateViewerPosition(face, context.baseline));
            status('tracking', 'Following your head.');
          }
        } else if (now - context.lastSeen > LOST_AFTER_MS) {
          context.samples = [];
          status('lost', 'Face out of view. Look toward the camera.');
        }
      } catch (error) {
        fail(context, error, 'inference');
        return;
      }
    }
    if (isCurrent(context)) context.raf = requestAnimationFrame((time) => tick(context, time));
  }

  async function initialize(context) {
    let stage = 'camera';
    try {
      if (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia) {
        current = null;
        dispose(context);
        status('error', 'Camera mode needs HTTPS or localhost in a supported browser. Cursor mode still works.');
        return false;
      }
      status('loading', 'Waiting for camera permission…');
      const stream = await navigator.mediaDevices.getUserMedia({
        audio: false,
        video: { facingMode: 'user', width: { ideal: 640 }, height: { ideal: 480 }, frameRate: { ideal: 30, max: 30 } },
      });
      if (!isCurrent(context)) {
        stream.getTracks().forEach((track) => track.stop());
        return false;
      }
      context.stream = stream;
      const ended = () => fail(context, new Error('Camera disconnected.'), 'camera');
      stream.getVideoTracks().forEach((track) => track.addEventListener('ended', ended));
      context.removeTrackListeners = () => stream.getVideoTracks().forEach((track) => track.removeEventListener('ended', ended));
      video.muted = true;
      video.playsInline = true;
      video.srcObject = stream;
      await video.play();
      if (!isCurrent(context)) return false;
      await waitForVideo(context);
      if (!isCurrent(context)) return false;

      stage = 'model';
      status('loading', 'Loading face tracker…');
      const vision = await FilesetResolver.forVisionTasks(`${base}wasm`);
      if (!isCurrent(context)) return false;
      const options = (delegate) => ({
        baseOptions: { modelAssetPath: `${base}models/face_landmarker.task`, delegate },
        runningMode: 'VIDEO',
        numFaces: 1,
        minFaceDetectionConfidence: 0.6,
        minFacePresenceConfidence: 0.6,
        minTrackingConfidence: 0.6,
        outputFaceBlendshapes: false,
        outputFacialTransformationMatrixes: false,
      });
      let landmarker;
      try {
        landmarker = await FaceLandmarker.createFromOptions(vision, options('GPU'));
      } catch (error) {
        if (!isCurrent(context)) return false;
        landmarker = await FaceLandmarker.createFromOptions(vision, options('CPU'));
      }
      if (!isCurrent(context)) {
        landmarker.close();
        return false;
      }
      context.landmarker = landmarker;
      context.running = true;
      context.lastSeen = performance.now();
      status('lost', 'Look toward the camera to center.');
      context.raf = requestAnimationFrame((time) => tick(context, time));
      return true;
    } catch (error) {
      fail(context, error, stage);
      return false;
    }
  }

  return {
    start() {
      if (current) return current.promise;
      const context = {
        abort: new AbortController(), disposed: false, running: false, stream: null,
        landmarker: null, raf: 0, baseline: null, samples: [], lastFace: null,
        lastSeen: 0, lastVideoTime: -1, promise: null,
      };
      current = context;
      context.promise = initialize(context);
      return context.promise;
    },
    stop() {
      const context = current;
      current = null;
      dispose(context);
      status('idle', 'Camera off. Cursor ready.');
    },
    calibrate() {
      if (!current?.running) return false;
      if (!current.lastFace || performance.now() - current.lastSeen > LOST_AFTER_MS) {
        current.baseline = null;
        current.samples = [];
        status('lost', 'Look toward the camera to center.');
        return false;
      }
      current.baseline = { ...current.lastFace };
      onPosition({ x: 0, y: 0, z: 1 });
      status('tracking', 'View centered.');
      return true;
    },
    get running() { return Boolean(current?.running); },
  };
}
