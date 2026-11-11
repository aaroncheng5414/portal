import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const args = process.argv.slice(2);
let host = '127.0.0.1';
let port = 5173;
for (let index = 0; index < args.length; index += 2) {
  if (!['--host', '--port'].includes(args[index]) || !args[index + 1]) throw new Error('Usage: npm start -- [--host 0.0.0.0] [--port 5173]');
  if (args[index] === '--host') host = args[index + 1];
  else port = Number(args[index + 1]);
}
if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Port must be an integer from 1 to 65535.');
const files = new Map([['/', 'index.html'], ['/index.html', 'index.html'], ['/app.js', 'app.js'], ['/model.js', 'model.js'], ['/sample.js', 'sample.js'], ['/style.css', 'style.css']]);
const mime = { html: 'text/html; charset=utf-8', js: 'text/javascript; charset=utf-8', css: 'text/css; charset=utf-8' };
const server = createServer(async (request, response) => {
  if (!['GET', 'HEAD'].includes(request.method)) { response.writeHead(405, { Allow: 'GET, HEAD' }).end(); return; }
  let pathname;
  try { pathname = new URL(request.url, 'http://localhost').pathname; }
  catch { response.writeHead(400).end('Invalid request'); return; }
  const name = files.get(pathname);
  if (!name) { response.writeHead(404).end('Not found'); return; }
  try {
    const content = await readFile(fileURLToPath(new URL(name, import.meta.url)));
    response.writeHead(200, { 'Content-Type': mime[name.split('.').pop()], 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer', 'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' blob:; connect-src http: https:; object-src 'none'; base-uri 'self'; frame-ancestors 'none'" });
    response.end(request.method === 'HEAD' ? undefined : content);
  } catch { response.writeHead(500).end('Unable to read page'); }
});
server.on('error', error => { console.error(error.message); process.exitCode = 1; });
server.listen(port, host, () => console.log(`Portal CV monitor: http://${host}:${port}`));
