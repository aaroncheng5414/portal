import { mkdir, copyFile, readdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
const root = new URL('../', import.meta.url);
const source = new URL('node_modules/@mediapipe/tasks-vision/wasm/', root);
const destination = new URL('public/wasm/', root);
await mkdir(destination, { recursive: true });
for (const filename of await readdir(source)) {
  if (/\.(wasm|js)$/.test(filename)) await copyFile(new URL(filename, source), new URL(filename, destination));
}
console.log(`Prepared local face-tracking runtime in ${fileURLToPath(destination)}`);
