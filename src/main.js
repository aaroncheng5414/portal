import './style.css';
import { createPortalScene } from './scene.js';

const $ = (id) => document.getElementById(id);
const canvas = $('portal-canvas');
const screen = $('screen');
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));
const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
let mode = 'pointer';
let demo = false;
let demoStart = 0;
let trackingRequest = 0;
let tracker;
let scene;
let frame;
let lastTime = 0;
let hasExplored = false;
let fatalRenderError = false;
let cameraError = '';
const target = { x: 0, y: 0, z: 1 };
const viewer = { ...target };

function showStatus(message, error = false) {
  $('status-text').textContent = message;
  $('status-dot').classList.toggle('error', error);
}

function setDemo(active) {
  demo = active;
  demoStart = performance.now();
  $('demo-label').textContent = active ? 'Pause demo' : 'Play demo';
  $('play-icon').textContent = active ? 'Ⅱ' : '▷';
  $('demo-button').setAttribute('aria-pressed', String(active));
  if (mode === 'pointer') showStatus(active ? 'Demo playing' : 'Cursor active');
}

function syncMode() {
  for (const id of ['pointer', 'camera']) {
    $(`${id}-button`).classList.toggle('selected', mode === id);
    $(`${id}-button`).setAttribute('aria-pressed', String(mode === id));
  }
  $('camera-actions').hidden = mode !== 'camera';
  $('distance-control').hidden = mode === 'camera';
  $('demo-button').disabled = mode === 'camera' || fatalRenderError;
  $('input-help').textContent = mode === 'camera'
    ? 'Choose Cursor to turn the camera off.'
    : 'Drag on touch. Focus the scene to use arrow keys.';
}

function usePointer() {
  mode = 'pointer';
  trackingRequest++;
  tracker?.stop();
  $('camera-preview').hidden = true;
  $('calibrate-button').disabled = true;
  target.z = Number($('distance').value);
  syncMode();
  showStatus('Cursor active');
}

function cameraStatus({ state, message }) {
  if (mode !== 'camera') return;
  showStatus(message, state === 'error');
  $('calibrate-button').disabled = state !== 'tracking';
  if (state === 'tracking' || state === 'lost') $('camera-preview').hidden = false;
  if (state === 'error') {
    cameraError = message;
    usePointer();
    showStatus(message, true);
  }
}

async function useCamera() {
  if (mode === 'camera' || fatalRenderError) return;
  setDemo(false);
  mode = 'camera';
  cameraError = '';
  const request = ++trackingRequest;
  Object.assign(target, { x: 0, y: 0, z: 1 });
  syncMode();
  showStatus('Preparing camera mode…');
  try {
    if (!tracker) {
      const { createHeadTracker } = await import('./tracking.js');
      if (request !== trackingRequest) return;
      tracker = createHeadTracker({ video: $('camera-video'), onStatus: cameraStatus,
        onPosition(position) { if (mode === 'camera') Object.assign(target, position); } });
    }
    if (request !== trackingRequest) return;
    const started = await tracker.start();
    if (request !== trackingRequest) return;
    if (!started) {
      usePointer();
      showStatus(cameraError || 'Camera could not start. Cursor active.', true);
    }
  } catch {
    if (request !== trackingRequest) return;
    usePointer();
    showStatus('Camera mode could not load. Cursor active.', true);
  }
}

function explored() {
  if (!hasExplored) {
    hasExplored = true;
    $('screen-hint').classList.add('dismissed');
  }
}

function movePointer(event) {
  if (mode !== 'pointer') return;
  if (demo) setDemo(false);
  const rect = canvas.getBoundingClientRect();
  target.x = clamp((event.clientX - rect.left) / rect.width * 2 - 1, -1, 1);
  target.y = clamp(1 - (event.clientY - rect.top) / rect.height * 2, -1, 1);
  explored();
}

canvas.addEventListener('pointermove', movePointer);
canvas.addEventListener('pointerdown', (event) => {
  if (mode !== 'pointer') return;
  canvas.setPointerCapture(event.pointerId);
  movePointer(event);
});
canvas.addEventListener('keydown', (event) => {
  if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home'].includes(event.key) || mode !== 'pointer') return;
  event.preventDefault();
  setDemo(false);
  if (event.key === 'Home') Object.assign(target, { x: 0, y: 0 });
  else {
    target.x = clamp(target.x + (event.key === 'ArrowRight' ? 0.12 : event.key === 'ArrowLeft' ? -0.12 : 0), -1, 1);
    target.y = clamp(target.y + (event.key === 'ArrowUp' ? 0.12 : event.key === 'ArrowDown' ? -0.12 : 0), -1, 1);
  }
  explored();
});
$('pointer-button').addEventListener('click', usePointer);
$('camera-button').addEventListener('click', useCamera);
$('calibrate-button').addEventListener('click', () => tracker?.calibrate());
$('demo-button').addEventListener('click', () => { setDemo(!demo); explored(); });
$('reset-button').addEventListener('click', () => {
  setDemo(false);
  Object.assign(target, { x: 0, y: 0, z: 1 });
  $('sensitivity').value = '1';
  $('sensitivity-output').textContent = '1.0×';
  $('distance').value = '1';
  $('distance-output').textContent = 'Natural';
  if (mode === 'camera') tracker?.calibrate();
});
$('sensitivity').addEventListener('input', event => { $('sensitivity-output').textContent = `${Number(event.target.value).toFixed(1)}×`; });
$('distance').addEventListener('input', event => {
  target.z = Number(event.target.value);
  $('distance-output').textContent = target.z < .95 ? 'Closer' : target.z > 1.05 ? 'Further' : 'Natural';
});
document.querySelectorAll('[data-scene]').forEach(button => button.addEventListener('click', () => {
  scene?.setScene(button.dataset.scene);
  document.querySelectorAll('[data-scene]').forEach(tab => {
    const selected = tab === button;
    tab.classList.toggle('selected', selected);
    tab.setAttribute('aria-pressed', String(selected));
  });
  $('scene-caption').textContent = button.dataset.scene === 'gallery' ? 'Gallery' : 'Landscape';
}));

$('fullscreen-button').hidden = !document.fullscreenEnabled;
$('fullscreen-button').addEventListener('click', async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await $('stage').requestFullscreen();
  } catch { showStatus('Fullscreen is unavailable in this browser.', true); }
});
document.addEventListener('fullscreenchange', () => {
  $('fullscreen-button').setAttribute('aria-label', document.fullscreenElement ? 'Exit fullscreen' : 'Enter fullscreen');
});

function renderFailure() {
  fatalRenderError = true;
  $('render-error').hidden = false;
  $('screen-hint').hidden = true;
  $('camera-button').disabled = true;
  usePointer();
  setDemo(false);
  syncMode();
  showStatus('3D rendering is unavailable.', true);
  document.querySelectorAll('[data-scene]').forEach(button => { button.disabled = true; });
}

try {
  scene = createPortalScene(canvas);
  const resize = new ResizeObserver(() => { scene.resize(screen.clientWidth, screen.clientHeight); });
  resize.observe(screen);
  scene.resize(screen.clientWidth, screen.clientHeight);
  const animate = (now) => {
    const dt = Math.min((now - lastTime) / 1000 || .016, .1);
    lastTime = now;
    if (demo) {
      const elapsed = (now - demoStart) / 1000;
      target.x = Math.sin(elapsed * .52) * .85;
      target.y = Math.sin(elapsed * .37) * .47;
    }
    const gain = Number($('sensitivity').value);
    // Camera input needs less easing so the view follows head movement promptly.
    const response = mode === 'camera' ? 24 : 9;
    const smooth = reducedMotion.matches ? 1 : 1 - Math.exp(-dt * response);
    viewer.x += (clamp(target.x * gain, -1, 1) - viewer.x) * smooth;
    viewer.y += (clamp(target.y * gain, -1, 1) - viewer.y) * smooth;
    viewer.z += (target.z - viewer.z) * smooth;
    scene.setViewer(viewer);
    scene.render(reducedMotion.matches ? 0 : now / 1000);
    $('map-dot').style.left = `${50 + viewer.x * 35}%`;
    $('map-dot').style.top = `${42 - viewer.y * 30}%`;
    frame = requestAnimationFrame(animate);
  };
  frame = requestAnimationFrame(animate);
  canvas.addEventListener('webglcontextlost', (event) => {
    event.preventDefault();
    cancelAnimationFrame(frame);
    renderFailure();
    $('render-error').innerHTML = '<strong>The graphics connection was interrupted.</strong><p>Reload the page to open the window again.</p>';
  });
  window.addEventListener('pagehide', () => { tracker?.stop(); });
  window.addEventListener('pageshow', event => { if (event.persisted) usePointer(); });
} catch (error) {
  console.error('Portal could not initialize:', error);
  renderFailure();
}
syncMode();
