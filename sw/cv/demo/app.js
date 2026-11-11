import { acceptRecord, apiOrigin, connectionStatus, fetchFrame, fetchResource, processedRate, projectionAxes, projectionPoints } from './model.js';
import { sampleState } from './sample.js';

const $ = id => document.getElementById(id);
const number = value => Number.isFinite(value) ? value.toFixed(3) : '—';
let mode = 'disconnected';
let record = null;
let receivedAt = 0;
let progressedAt = 0;
let error = '';
let session = null;
let sampleTimer = null;
let inspectedId = null;
let origin = '';
let displayedFrame = null;
let frameError = '';
const downloadURLs = new Map();
const hostname = location.hostname || '127.0.0.1';
$('api-url').value = `http://${hostname.includes(':') && !hostname.startsWith('[') ? `[${hostname}]` : hostname}:8080`;

function svgElement(tag, attributes = {}, text = null) {
  const element = document.createElementNS('http://www.w3.org/2000/svg', tag);
  for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, value);
  if (text !== null) element.textContent = text;
  return element;
}

function inspect(id) { inspectedId = id; renderDetails(); }

function drawPlot(plane) {
  const svg = $(`plot-${plane}`);
  const focusedId = svg.contains(document.activeElement) ? document.activeElement.dataset.person : null;
  svg.replaceChildren();
  const people = projectionPoints(record, plane);
  const [horizontal, vertical] = projectionAxes[plane];
  const range = axis => {
    const values = (record?.persons || []).map(person => person.position_m[axis]);
    return [Math.min(axis === 'z' ? 0 : -1, Math.floor(Math.min(0, ...values) * 1.15)), Math.max(axis === 'z' ? 4 : 1, Math.ceil(Math.max(0, ...values) * 1.15))];
  };
  const [xMin, xMax] = range(horizontal), [yMin, yMax] = range(vertical);
  const left = 49, right = 337, top = 16, bottom = 251;
  const mapX = x => left + ((x - xMin) / (xMax - xMin)) * (right - left);
  const mapY = y => bottom - ((y - yMin) / (yMax - yMin)) * (bottom - top);
  svg.append(svgElement('rect', { x: left, y: top, width: right - left, height: bottom - top, fill: '#eff3e5', rx: 3 }));
  for (let index = 0; index <= 4; index += 1) {
    const x = xMin + index * (xMax - xMin) / 4;
    const y = yMin + index * (yMax - yMin) / 4;
    svg.append(svgElement('line', { x1: mapX(x), y1: top, x2: mapX(x), y2: bottom, stroke: '#d9e1cc', 'stroke-width': 0.7 }));
    svg.append(svgElement('text', { x: mapX(x), y: bottom + 19, 'text-anchor': 'middle' }, Number.isInteger(x) ? String(x) : x.toFixed(1)));
    svg.append(svgElement('line', { x1: left, y1: mapY(y), x2: right, y2: mapY(y), stroke: '#d9e1cc', 'stroke-width': 0.7 }));
    svg.append(svgElement('text', { x: left - 9, y: mapY(y) + 4, 'text-anchor': 'end' }, Number.isInteger(y) ? String(y) : y.toFixed(1)));
  }
  svg.append(svgElement('line', { x1: mapX(0), y1: top, x2: mapX(0), y2: bottom, stroke: '#b9c9a8', 'stroke-dasharray': '3 5' }));
  const labels = { x: 'X · right (m)', y: 'Y · up (m)', z: 'Z · forward (m)' };
  svg.append(svgElement('text', { x: (left + right) / 2, y: 292, 'text-anchor': 'middle', class: 'axis-title' }, labels[horizontal]));
  svg.append(svgElement('text', { x: 13, y: (top + bottom) / 2, transform: `rotate(-90 13 ${(top + bottom) / 2})`, 'text-anchor': 'middle', class: 'axis-title' }, labels[vertical]));
  svg.append(svgElement('circle', { cx: mapX(0), cy: mapY(0), r: 2.5, fill: '#597c47' }));
  for (const person of people) {
    const active = person.active;
    const x = mapX(person.horizontal), y = mapY(person.vertical);
    const point = svgElement('g', { class: 'point', tabindex: '0', role: 'button', 'data-person': person.id, 'aria-label': `Person ${person.id}, ${active ? 'active, ' : ''}${person.observed ? 'observed' : 'unobserved'}, X ${number(person.position_m.x)}, Y ${number(person.position_m.y)}, Z ${number(person.position_m.z)} metres` });
    point.append(svgElement('circle', { cx: x, cy: y, r: 20, class: 'focus-ring' }));
    if (active) point.append(svgElement('circle', { cx: x, cy: y, r: 14, fill: '#b67a4620', stroke: '#b8825866' }));
    point.append(svgElement('circle', { cx: x, cy: y, r: active ? 6 : 5, fill: person.observed ? active ? '#aa6844' : '#8ca47b' : '#eff3e5', stroke: active ? '#aa6844' : '#8ca47b', 'stroke-width': 1.5, 'stroke-dasharray': person.observed ? 'none' : '2 2' }));
    const labelOnLeft = x > right - 85;
    point.append(svgElement('text', { x: x + (labelOnLeft ? -13 : 13), y: y + 4, 'text-anchor': labelOnLeft ? 'end' : 'start', class: 'person-label' }, `#${person.id}${active ? ' · active' : ''}`));
    point.append(svgElement('title', {}, `Person ${person.id} · X ${number(person.position_m.x)}, Y ${number(person.position_m.y)}, Z ${number(person.position_m.z)} m`));
    for (const event of ['pointerenter', 'focus', 'click']) point.addEventListener(event, () => inspect(person.id));
    point.addEventListener('keydown', event => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); inspect(person.id); } });
    svg.append(point);
  }
  if (!people.length) svg.append(svgElement('text', { x: (left + right) / 2, y: (top + bottom) / 2, 'text-anchor': 'middle', class: 'axis-title' }, 'No tracked positions'));
  if (focusedId) svg.querySelector(`[data-person="${focusedId}"]`)?.focus({ preventScroll: true });
}

function renderDetails() {
  const people = record?.persons || [];
  const person = people.find(item => item.id === inspectedId) || people.find(item => item.id === record?.selected.person_id);
  $('person-details').replaceChildren();
  if (!person) { $('person-details').textContent = 'Hover or select a person to inspect XYZ.'; return; }
  const label = document.createElement('strong'); label.textContent = `Person #${person.id}`;
  $('person-details').append(label, document.createTextNode(`X ${number(person.position_m.x)} · Y ${number(person.position_m.y)} · Z ${number(person.position_m.z)} m`));
}

function renderTable() {
  const focusedId = $('person-rows').contains(document.activeElement) ? document.activeElement.dataset.person : null;
  const people = record?.persons || [];
  $('person-count').textContent = String(people.length);
  $('person-rows').replaceChildren();
  if (!people.length) {
    const row = document.createElement('tr'); const cell = document.createElement('td'); cell.colSpan = 4; cell.className = 'empty'; cell.textContent = record ? 'No tracked people.' : 'No positions received.'; row.append(cell); $('person-rows').append(row);
  }
  for (const person of people) {
    const active = record.selected.person_id === person.id;
    const row = document.createElement('tr'); row.className = `${active ? 'active' : ''} ${person.observed ? '' : 'unobserved'}`;
    const name = document.createElement('td'); const button = document.createElement('button'); button.type = 'button'; button.dataset.person = person.id; button.textContent = `#${person.id}`; button.setAttribute('aria-label', `Inspect person ${person.id}${active ? ', active' : ''}`); button.addEventListener('click', () => inspect(person.id)); button.addEventListener('focus', () => inspect(person.id)); name.append(button);
    if (active) { const badge = document.createElement('span'); badge.className = 'active-label'; badge.textContent = 'active'; name.append(badge); }
    if (!person.observed) { const note = document.createElement('span'); note.className = 'unobserved-label'; note.textContent = 'unobserved'; name.append(note); }
    row.append(name);
    for (const axis of ['x', 'y', 'z']) { const cell = document.createElement('td'); cell.textContent = number(person.position_m[axis]); row.append(cell); }
    $('person-rows').append(row);
  }
  const selected = record?.selected;
  const absent = selected?.person_id && !people.some(person => person.id === selected.person_id);
  $('held-note').hidden = !absent;
  $('held-note').textContent = absent ? `Selected #${selected.person_id} is absent from the tracked vector (${selected.state}${selected.stale ? ', stale' : ''}).` : '';
  if (focusedId) $('person-rows').querySelector(`[data-person="${focusedId}"]`)?.focus({ preventScroll: true });
}

function renderFreshness() {
  const input = { mode, record, error, now: performance.now(), receivedAt, progressedAt };
  const status = connectionStatus(input);
  $('status').dataset.tone = status.tone; $('status-title').textContent = status.title; $('status-message').textContent = status.message;
  const rate = processedRate(input);
  $('processed-fps').replaceChildren();
  const value = document.createElement('strong'); value.textContent = rate.value === null ? '—' : rate.value.toFixed(1);
  $('processed-fps').append(document.createTextNode(`${rate.label} `), value, document.createTextNode(` fps${rate.value !== null && rate.synthetic ? ' · synthetic' : ''}`));
  renderFrameStatus(input.now, status);
}

function render() {
  for (const plane of Object.keys(projectionAxes)) drawPlot(plane);
  renderTable(); renderDetails(); renderFreshness();
  $('json-label').textContent = record ? `${mode === 'sample' ? 'SAMPLE · ' : ''}sequence ${record.sequence}` : 'GET /api/v1/state';
  if ($('raw-details').open) $('raw-json').textContent = record ? JSON.stringify(record, null, 2) : 'No response received.';
}

function delay(milliseconds, signal) {
  return new Promise(resolve => {
    if (signal.aborted) { resolve(); return; }
    const finish = () => { clearTimeout(timer); signal.removeEventListener('abort', finish); resolve(); };
    const timer = setTimeout(finish, milliseconds); signal.addEventListener('abort', finish, { once: true });
  });
}

async function poll(controller) {
  while (!controller.signal.aborted) {
    try {
      const { body } = await fetchResource(`${origin}/api/v1/state`, { signal: controller.signal });
      if (session !== controller) return;
      const next = acceptRecord(record, body); const now = performance.now();
      if (!record || next.sequence > record.sequence || next.video.frame_sequence > record.video.frame_sequence) progressedAt = now;
      record = next; receivedAt = now; error = ''; render();
    } catch (failure) {
      if (session !== controller || controller.signal.aborted) return;
      error = failure instanceof TypeError ? 'Cannot reach the API. Check its URL and CORS settings.' : failure.message; renderFreshness();
    }
    await delay(error ? 800 : 150, controller.signal);
  }
}

function clearFrame() {
  if (displayedFrame) URL.revokeObjectURL(displayedFrame.url);
  displayedFrame = null; frameError = '';
  $('camera-frame').removeAttribute('src'); $('camera-frame').hidden = true;
  $('frame-placeholder').hidden = false;
}

function renderFrameStatus(now, status) {
  $('save-frame').disabled = !displayedFrame || mode === 'sample';
  $('frame-sequence').textContent = displayedFrame ? `FRAME ${displayedFrame.sequence ?? 'UNKNOWN'}` : 'FRAME —';
  $('frame-badge').hidden = true;
  if (mode === 'sample') {
    $('frame-placeholder').textContent = 'Browser sample · no camera image';
    $('frame-status').textContent = 'Connect to the backend to view and save frames.';
    return;
  }
  $('frame-placeholder').textContent = mode === 'connected' ? 'Waiting for a backend image…' : 'Connect to receive camera frames.';
  if (!displayedFrame) { $('frame-status').textContent = frameError || 'No frame received'; return; }
  let message;
  let badge = '';
  if (mode === 'disconnected') { message = 'Disconnected · last image held'; badge = 'HELD'; }
  else if (frameError) { message = `Image error: ${frameError}`; badge = 'IMAGE ERROR'; }
  else if (error || ['error', 'lost', 'starting'].includes(record?.source.status)) { message = `${status.title} · last image held`; badge = 'HELD'; }
  else if (now - displayedFrame.receivedAt > 1500 || status.title === 'Outdated data') { message = 'Outdated image · waiting for a new frame'; badge = 'OUTDATED'; }
  else if (displayedFrame.sequence === null) { message = 'Image sequence unavailable'; badge = 'UNVERIFIED SEQUENCE'; }
  else {
    message = record && record.video.frame_sequence !== displayedFrame.sequence ? `Image ${displayedFrame.sequence} · state ${record.video.frame_sequence}` : 'Latest backend image';
    if (displayedFrame.sourceKind === 'synthetic') badge = 'SYNTHETIC';
  }
  $('frame-status').textContent = message;
  $('frame-badge').hidden = !badge; $('frame-badge').textContent = badge;
}

function decodeFrame(blob, signal) {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(blob);
    const image = new Image();
    let timer;
    const finish = failure => {
      clearTimeout(timer); signal.removeEventListener('abort', abort);
      image.onload = image.onerror = null;
      if (failure) { image.src = ''; URL.revokeObjectURL(url); reject(failure); }
      else resolve(url);
    };
    const abort = () => finish(signal.reason || new DOMException('Request cancelled', 'AbortError'));
    if (signal.aborted) { abort(); return; }
    signal.addEventListener('abort', abort, { once: true });
    image.onload = () => finish();
    image.onerror = () => finish(new Error('Unable to decode the backend JPEG.'));
    timer = setTimeout(() => finish(new Error('Image decode timed out.')), 2500);
    image.src = url;
  });
}

async function pollFrames(controller) {
  while (!controller.signal.aborted) {
    if (record) {
      let pendingURL = null;
      try {
        const frame = await fetchFrame(`${origin}/api/v1/frame.jpg`, { signal: controller.signal, previousSequence: displayedFrame?.sequence ?? null });
        if (session !== controller || controller.signal.aborted) return;
        if (frame.changed) {
          pendingURL = await decodeFrame(frame.blob, controller.signal);
          if (session !== controller || controller.signal.aborted) { URL.revokeObjectURL(pendingURL); return; }
          const previous = displayedFrame;
          displayedFrame = { ...frame, url: pendingURL, receivedAt: performance.now(), sourceKind: record.source.kind };
          pendingURL = null;
          $('camera-frame').src = displayedFrame.url;
          $('camera-frame').alt = `${displayedFrame.sourceKind === 'synthetic' ? 'Synthetic' : 'Camera'} frame ${frame.sequence ?? '(sequence unavailable)'}, annotated by the backend`;
          $('camera-frame').hidden = false; $('frame-placeholder').hidden = true;
          if (previous) URL.revokeObjectURL(previous.url);
        }
        frameError = '';
      } catch (failure) {
        if (pendingURL) URL.revokeObjectURL(pendingURL);
        if (session !== controller || controller.signal.aborted) return;
        frameError = failure instanceof TypeError ? 'Cannot reach the frame endpoint.' : failure.message;
      }
      renderFreshness();
    }
    await delay(frameError ? 800 : 120, controller.signal);
  }
}

function stop() {
  session?.abort(); session = null; clearInterval(sampleTimer); sampleTimer = null;
  mode = 'disconnected'; error = ''; frameError = ''; $('sample').setAttribute('aria-pressed', 'false'); $('disconnect').hidden = true; $('connect').hidden = false; $('api-url').disabled = false; renderFreshness();
}

function updateSchemaLink() {
  try { $('schema-link').href = `${apiOrigin($('api-url').value)}/api/v1/schema`; $('schema-link').removeAttribute('aria-disabled'); }
  catch { $('schema-link').removeAttribute('href'); $('schema-link').setAttribute('aria-disabled', 'true'); }
}

$('connection-form').addEventListener('submit', event => {
  event.preventDefault();
  try { origin = apiOrigin($('api-url').value.trim()); }
  catch (failure) { $('api-url').setCustomValidity(failure.message); $('api-url').reportValidity(); return; }
  stop(); clearFrame(); record = null; inspectedId = null; mode = 'connected'; receivedAt = progressedAt = performance.now();
  $('api-url').value = origin; $('api-url').disabled = true; $('connect').hidden = true; $('disconnect').hidden = false; updateSchemaLink();
  session = new AbortController(); render(); void poll(session); void pollFrames(session);
});
$('api-url').addEventListener('input', () => { $('api-url').setCustomValidity(''); updateSchemaLink(); });
$('disconnect').addEventListener('click', stop);
$('sample').addEventListener('click', () => {
  if (mode === 'sample') { stop(); return; }
  stop(); clearFrame(); record = null; inspectedId = null; mode = 'sample'; $('sample').setAttribute('aria-pressed', 'true'); $('disconnect').hidden = false;
  const started = performance.now();
  const step = () => { record = sampleState((performance.now() - started) / 1000); receivedAt = progressedAt = performance.now(); render(); };
  step(); sampleTimer = setInterval(step, matchMedia('(prefers-reduced-motion: reduce)').matches ? 500 : 100);
});
$('raw-details').addEventListener('toggle', () => { if ($('raw-details').open) $('raw-json').textContent = record ? JSON.stringify(record, null, 2) : 'No response received.'; });
$('save-frame').addEventListener('click', () => {
  if (!displayedFrame || mode === 'sample') return;
  const saved = displayedFrame;
  const url = URL.createObjectURL(saved.blob);
  const link = document.createElement('a'); link.href = url; link.download = `portal-frame-${saved.sequence ?? 'unknown-sequence'}.jpg`; link.hidden = true;
  document.body.append(link); link.click(); link.remove();
  const timer = setTimeout(() => { URL.revokeObjectURL(url); downloadURLs.delete(url); }, 60000);
  downloadURLs.set(url, timer);
});
const freshnessTimer = setInterval(renderFreshness, 250);
window.addEventListener('pagehide', () => { stop(); clearInterval(freshnessTimer); clearFrame(); for (const [url, timer] of downloadURLs) { clearTimeout(timer); URL.revokeObjectURL(url); } downloadURLs.clear(); });
updateSchemaLink(); render();
