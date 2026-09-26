import * as THREE from 'three';
import { computePortalFrustum } from './projection.js';

const SCREEN_WIDTH = 8;
const FLOOR_Y = -2.5;
const clamp = THREE.MathUtils.clamp;

function material(color, roughness = 0.85, metalness = 0) {
  return new THREE.MeshStandardMaterial({ color, roughness, metalness });
}

function mesh(parent, geometry, surface, position = [0, 0, 0], castsShadow = true) {
  const object = new THREE.Mesh(geometry, surface);
  object.position.set(...position);
  object.castShadow = castsShadow;
  object.receiveShadow = true;
  parent.add(object);
  return object;
}

function box(parent, dimensions, surface, position, castsShadow = true) {
  return mesh(parent, new THREE.BoxGeometry(...dimensions), surface, position, castsShadow);
}

function cylinder(parent, radiusTop, radiusBottom, height, surface, position, segments = 48) {
  return mesh(parent, new THREE.CylinderGeometry(radiusTop, radiusBottom, height, segments), surface, position);
}

function makeShadowTexture() {
  const surface = document.createElement('canvas');
  surface.width = surface.height = 128;
  const context = surface.getContext('2d');
  const gradient = context.createRadialGradient(64, 64, 0, 64, 64, 64);
  gradient.addColorStop(0, 'rgba(45,31,24,0.3)');
  gradient.addColorStop(0.38, 'rgba(45,31,24,0.15)');
  gradient.addColorStop(1, 'rgba(45,31,24,0)');
  context.fillStyle = gradient;
  context.fillRect(0, 0, 128, 128);
  return new THREE.CanvasTexture(surface);
}

function contactShadow(parent, texture, x, z, width, depth) {
  const surface = new THREE.MeshBasicMaterial({ map: texture, transparent: true, depthWrite: false, toneMapped: false });
  const shadow = mesh(parent, new THREE.PlaneGeometry(width, depth), surface, [x, FLOOR_Y + 0.018, z], false);
  shadow.rotation.x = -Math.PI / 2;
  shadow.receiveShadow = false;
}

function arch(parent, surface, { width, height, opening, spring, depth, z }) {
  const shape = new THREE.Shape();
  shape.moveTo(-width / 2, FLOOR_Y);
  shape.lineTo(-opening, FLOOR_Y);
  shape.lineTo(-opening, spring);
  shape.absarc(0, spring, opening, Math.PI, 0, true);
  shape.lineTo(opening, FLOOR_Y);
  shape.lineTo(width / 2, FLOOR_Y);
  shape.lineTo(width / 2, height);
  shape.lineTo(-width / 2, height);
  shape.closePath();
  return mesh(parent, new THREE.ExtrudeGeometry(shape, { depth, bevelEnabled: false, curveSegments: 48 }), surface, [0, 0, z]);
}

function makeGallery() {
  const group = new THREE.Group();
  const plaster = material('#eadfcb');
  const palePlaster = material('#f6eddd');
  const stone = material('#cfbea4');
  const floor = material('#d8c8b0', 0.68);
  const terracotta = material('#b65f42', 0.7);
  const teal = material('#265d59', 0.35, 0.17);
  const bronze = material('#b58a50', 0.45, 0.55);
  const shadowTexture = makeShadowTexture();

  box(group, [36, 0.3, 42], floor, [0, FLOOR_Y - 0.15, -21], false);
  box(group, [0.4, 13, 36], plaster, [-7.8, 3.8, -18]);
  box(group, [0.4, 13, 36], plaster, [7.8, 3.8, -18]);
  box(group, [16, 13, 0.3], material('#c79875'), [0, 3.8, -32]);

  arch(group, palePlaster, { width: 16, height: 10, opening: 3.9, spring: 1.1, depth: 0.65, z: -13 });
  arch(group, plaster, { width: 16, height: 10, opening: 3.55, spring: 1.05, depth: 0.55, z: -22 });

  // Seams in the floor make the changing depth perspective easy to see.
  const seam = material('#c6b79f');
  for (let z = -1; z > -32; z -= 3) box(group, [15.4, 0.008, 0.018], seam, [0, FLOOR_Y + 0.005, z], false);
  for (const x of [-5, -2.5, 0, 2.5, 5]) box(group, [0.016, 0.008, 33], seam, [x, FLOOR_Y + 0.007, -16.5], false);

  // Foreground sculpture: a dark green loop, tilted in three dimensions.
  cylinder(group, 1.03, 1.08, 0.63, stone, [-2.05, FLOOR_Y + 0.315, -5.2]);
  const loop = mesh(group, new THREE.TorusGeometry(1.02, 0.26, 28, 88), teal, [-2.05, -0.55, -5.2]);
  loop.rotation.set(-0.15, 0.48, -0.23);
  contactShadow(group, shadowTexture, -2.05, -5.2, 4, 3.4);

  // A small bronze sphere peeks through the opening as the viewer moves.
  mesh(group, new THREE.SphereGeometry(0.3, 32, 24), bronze, [-2.1, -0.52, -5.7]);

  // Terracotta totem, held on a sharply edged plinth in the middle distance.
  box(group, [1.5, 1.05, 1.5], palePlaster, [2.65, FLOOR_Y + 0.525, -9.4]);
  const totem = new THREE.Group();
  totem.position.set(2.65, -0.67, -9.4);
  group.add(totem);
  const lower = mesh(totem, new THREE.SphereGeometry(0.78, 40, 28), terracotta, [0, 0, 0]);
  lower.scale.set(0.9, 1.1, 0.65);
  const upper = mesh(totem, new THREE.SphereGeometry(0.57, 40, 28), terracotta, [0.04, 1.13, -0.03]);
  upper.scale.set(0.9, 0.87, 0.7);
  cylinder(totem, 0.16, 0.26, 0.36, bronze, [0.06, 1.7, -0.03]);
  contactShadow(group, shadowTexture, 2.65, -9.4, 3.5, 3.5);

  // Columns close to the window create a strong foreground reference.
  for (const x of [-5.1, 5.1]) {
    cylinder(group, 0.23, 0.29, 8.9, palePlaster, [x, 1.95, -3.5]);
    cylinder(group, 0.37, 0.37, 0.16, stone, [x, FLOOR_Y + 0.08, -3.5]);
    contactShadow(group, shadowTexture, x, -3.5, 1.8, 2.1);
  }

  // Geometric artwork and a bench sit behind the sculptural forms.
  box(group, [2.5, 3.15, 0.15], bronze, [-5.3, 0.9, -12.1]);
  box(group, [2.28, 2.93, 0.1], material('#f3ddbe'), [-5.3, 0.9, -11.99]);
  const artDisc = mesh(group, new THREE.CircleGeometry(0.86, 64), terracotta, [-5.3, 1.15, -11.93], false);
  artDisc.receiveShadow = false;
  box(group, [1.75, 0.43, 0.035], teal, [-5.3, -0.02, -11.9], false);

  box(group, [3.8, 0.2, 0.92], material('#9c6e48'), [0.1, -1.66, -18]);
  for (const x of [-1.35, 1.55]) box(group, [0.16, 0.75, 0.65], bronze, [x, -2.125, -18]);
  contactShadow(group, shadowTexture, 0.1, -18, 5, 2.6);

  // Far wall artwork remains visible through both arches.
  const farDisc = mesh(group, new THREE.CircleGeometry(2.05, 80), material('#efe0c4'), [0, 1.95, -31.8], false);
  farDisc.receiveShadow = false;
  mesh(group, new THREE.CircleGeometry(1.02, 64), teal, [0, 1.95, -31.76], false);

  return { group, background: '#e7dcc7', fog: [27, 75], animate(time) { loop.rotation.y = 0.48 + Math.sin(time * 0.22) * 0.07; } };
}

function randomGenerator(seed) {
  return () => {
    seed = (seed * 1664525 + 1013904223) >>> 0;
    return seed / 4294967296;
  };
}

function mountain(parent, surface, z, baseHeight, seed) {
  const random = randomGenerator(seed);
  const shape = new THREE.Shape();
  shape.moveTo(-85, -17);
  for (let x = -85; x <= 85; x += 5) {
    const y = baseHeight + Math.sin(x * 0.083 + seed) * 3 + Math.sin(x * 0.21) * 1.4 + random() * 2.4;
    shape.lineTo(x, y);
  }
  shape.lineTo(85, -17);
  shape.closePath();
  return mesh(parent, new THREE.ShapeGeometry(shape), surface, [0, 0, z], false);
}

function makeLandscape() {
  const group = new THREE.Group();
  const sand = material('#d8b898');
  const paleStone = material('#e9cfac');
  const terracotta = material('#c38266');
  const darkGreen = material('#325955');
  const green = material('#587d68');
  const shadowTexture = makeShadowTexture();

  // Distance layers are true geometry, so each moves by a different amount.
  mountain(group, material('#bfa1a4'), -77, 16, 12);
  mountain(group, material('#bd8d85'), -56, 10, 9);
  mountain(group, material('#b47761'), -35, 4.8, 3);
  mountain(group, material('#92715b'), -22, -0.25, 5);

  const sun = mesh(group, new THREE.SphereGeometry(4.1, 48, 32), new THREE.MeshBasicMaterial({ color: '#ffe6ac', fog: false }), [-13.2, 15.3, -76], false);
  sun.receiveShadow = false;

  box(group, [45, 0.4, 35], sand, [0, FLOOR_Y - 0.2, -17.5], false);
  // Low, winding terrace walls add a near-to-far visual cue.
  box(group, [16, 0.22, 17], paleStone, [0, FLOOR_Y + 0.11, -8.5], false);
  for (let i = 0; i < 9; i += 1) {
    const z = -1.3 - i * 1.6;
    const x = 1.3 + Math.sin(i * 0.47) * 1.35;
    const step = box(group, [2.2, 0.028, 0.85], material(i % 2 ? '#c9a584' : '#bd9679'), [x, FLOOR_Y + 0.24, z], false);
    step.rotation.y = Math.sin(i * 0.47 + 0.6) * -0.2;
  }
  box(group, [0.35, 0.9, 12], terracotta, [-7.2, FLOOR_Y + 0.45, -10]);
  box(group, [0.35, 0.9, 12], terracotta, [7.2, FLOOR_Y + 0.45, -10]);

  function tree(x, z, scale) {
    const tree = new THREE.Group();
    tree.position.set(x, FLOOR_Y + 0.23, z);
    tree.scale.setScalar(scale);
    group.add(tree);
    cylinder(tree, 0.1, 0.18, 2.3, material('#76614e'), [0, 1.15, 0], 12);
    const crown = mesh(tree, new THREE.IcosahedronGeometry(1.12, 2), green, [0, 2.55, 0]);
    crown.scale.set(1.13, 1, 0.87);
    const crown2 = mesh(tree, new THREE.IcosahedronGeometry(0.83, 2), darkGreen, [-0.58, 2.18, 0.1]);
    crown2.scale.set(1, 0.8, 1);
    contactShadow(group, shadowTexture, x, z, 4.5 * scale, 3.4 * scale);
    return tree;
  }

  const leftTree = tree(-4.1, -4.8, 1.25);
  const rightTree = tree(5.1, -10.5, 1.2);
  tree(-6, -16, 0.78);

  function planter(x, z, size) {
    cylinder(group, size, size * 0.65, size * 1.4, terracotta, [x, FLOOR_Y + 0.23 + size * 0.7, z]);
    cylinder(group, size * 0.85, size * 0.85, 0.05, material('#725c46'), [x, FLOOR_Y + 0.23 + size * 1.4, z]);
    for (let i = 0; i < 9; i += 1) {
      const angle = i / 9 * Math.PI * 2;
      const leaf = mesh(group, new THREE.SphereGeometry(size * 0.23, 12, 8), i % 2 ? green : darkGreen,
        [x + Math.cos(angle) * size * 0.48, FLOOR_Y + 0.23 + size * 1.85, z + Math.sin(angle) * size * 0.48]);
      leaf.scale.set(0.8, 3, 0.4);
      leaf.rotation.set(Math.sin(angle) * 0.65, 0, -Math.cos(angle) * 0.65);
    }
    contactShadow(group, shadowTexture, x, z, size * 3.5, size * 3.5);
  }
  planter(3.45, -3.4, 0.66);
  planter(-1.4, -9.5, 0.5);

  // A smooth stone sits close to the screen and anchors the parallax effect.
  const rock = mesh(group, new THREE.IcosahedronGeometry(0.74, 2), paleStone, [-2.7, -2, -1.5]);
  rock.scale.set(1.5, 0.66, 0.95);
  rock.rotation.y = 0.4;
  contactShadow(group, shadowTexture, -2.7, -1.5, 3, 2.5);

  return { group, background: '#efd7bd', fog: [36, 150], animate(time) {
    leftTree.rotation.z = Math.sin(time * 0.45) * 0.008;
    rightTree.rotation.z = Math.sin(time * 0.4 + 2) * 0.007;
  } };
}

function releaseGroup(group) {
  const geometries = new Set();
  const materials = new Set();
  const textures = new Set();
  group.traverse(object => {
    if (object.geometry) geometries.add(object.geometry);
    if (object.material) for (const surface of [].concat(object.material)) {
      materials.add(surface);
      for (const value of Object.values(surface)) if (value?.isTexture) textures.add(value);
    }
  });
  geometries.forEach(geometry => geometry.dispose());
  textures.forEach(texture => texture.dispose());
  materials.forEach(surface => surface.dispose());
}

/**
 * A window into a real 3D scene. x/y are normalized viewer translations,
 * not camera angles. z is a viewer distance multiplier (1 = neutral).
 */
export function createPortalScene(canvas) {
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false, powerPreference: 'high-performance' });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.75));
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.23;

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera();
  camera.position.set(0, 0, 6);
  scene.add(new THREE.HemisphereLight('#fff2da', '#8c8778', 2.4));
  const sunlight = new THREE.DirectionalLight('#ffe7c6', 3.8);
  sunlight.position.set(-6, 10, 3);
  sunlight.target.position.set(0, -1, -10);
  sunlight.castShadow = true;
  sunlight.shadow.mapSize.set(2048, 2048);
  Object.assign(sunlight.shadow.camera, { left: -14, right: 14, top: 15, bottom: -15, near: 0.5, far: 65 });
  sunlight.shadow.bias = -0.0003;
  sunlight.shadow.normalBias = 0.025;
  sunlight.shadow.radius = 3;
  scene.add(sunlight, sunlight.target);
  const fill = new THREE.DirectionalLight('#e4f1eb', 0.55);
  fill.position.set(4, 3, -7);
  scene.add(fill);

  let content;
  let width = 1;
  let height = 1;
  let disposed = false;
  const viewer = { x: 0, y: 0, z: 1 };

  function updateProjection() {
    const eyeX = viewer.x * 1.8;
    const eyeY = viewer.y * 1.15;
    const eyeZ = viewer.z * 6;
    const frustum = computePortalFrustum({ width: SCREEN_WIDTH, height: SCREEN_WIDTH * height / width, eyeX, eyeY, eyeZ, near: 0.05, far: 180 });
    camera.position.set(eyeX, eyeY, eyeZ);
    camera.projectionMatrix.makePerspective(frustum.left, frustum.right, frustum.top, frustum.bottom, frustum.near, frustum.far);
    camera.projectionMatrixInverse.copy(camera.projectionMatrix).invert();
    camera.updateMatrixWorld();
  }

  function setScene(id) {
    if (disposed) return;
    if (content) { scene.remove(content.group); releaseGroup(content.group); }
    content = id === 'landscape' ? makeLandscape() : makeGallery();
    scene.add(content.group);
    scene.background = new THREE.Color(content.background);
    scene.fog = new THREE.Fog(content.background, ...content.fog);
  }

  setScene('gallery');
  updateProjection();

  return {
    resize(nextWidth, nextHeight) {
      if (disposed) return;
      width = Math.max(1, nextWidth);
      height = Math.max(1, nextHeight);
      renderer.setSize(width, height, false);
      updateProjection();
    },
    setViewer(position) {
      if (disposed) return;
      viewer.x = clamp(Number.isFinite(position.x) ? position.x : 0, -1, 1);
      viewer.y = clamp(Number.isFinite(position.y) ? position.y : 0, -1, 1);
      viewer.z = clamp(Number.isFinite(position.z) ? position.z : 1, 0.55, 1.8);
      updateProjection();
    },
    setScene,
    render(timeSeconds = 0) {
      if (disposed) return;
      content.animate(Number.isFinite(timeSeconds) ? timeSeconds : 0);
      renderer.render(scene, camera);
    },
    dispose() {
      if (disposed) return;
      disposed = true;
      releaseGroup(content.group);
      sunlight.shadow.dispose();
      renderer.dispose();
    },
  };
}
