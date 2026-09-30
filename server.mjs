// Static server for the split ranch project. three and its addons are served
// from node_modules so the page has no CDN dependency; the import map in
// index.html points 'three' and 'three/addons/' at these two routes.
//
// Everything the project does not own is proxied to the self-hosted Velxio on
// :3080 -- the ESP32 panel inside the 电路系统 drawer reads the simulated board
// out of a same-origin iframe, and the QEMU session runs over /ws/sim/{boardId},
// so the upgrade has to be forwarded too or the simulation never starts.
//
// /api/* is the phone door. The telemetry lives in the browser (that is where the
// simulator frames are scraped from), so the page reports it here and the apps read
// it back; commands go the other way and wait in a queue the page drains. The server
// is a mailbox, not a second authority -- it never invents a reading of its own.
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.dirname(fileURLToPath(import.meta.url));
const PORT = Number(process.env.PORT || 3082);
const THREE = path.join(ROOT, 'node_modules', 'three');
const UPSTREAM = { host: '127.0.0.1', port: Number(process.env.VELXIO_PORT || 3080) };

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
};

const ROUTES = [
  [/^\/vendor\/addons\/(.*)$/, m => path.join(THREE, 'examples', 'jsm', m[1])],
  [/^\/vendor\/three\.module\.js$/, () => path.join(THREE, 'build', 'three.module.js')],
  [/^\/$/, () => path.join(ROOT, 'index.html')],
  [/^\/(css|js|reference|firmware)\/(.+)$/, m => path.join(ROOT, m[1], m[2])],
];

// The mailbox. `state` is whatever the page last saw; `cmds` are what the phones
// asked for. Both carry a stamp so a reader can tell how old the truth is.
const mailbox = { state: null, stateAt: 0, cmds: [], cmdSeq: 0 };
const CMD_LIMIT = 32;
const STATE_MAX_BYTES = 64 * 1024;

// One JSON writer for both this file and what it wires up: a body on a 204 is not a
// legal response, so callers that want an empty answer use 200 with `{}`.
function json(res, code, obj) {
  res.writeHead(code, {
    'content-type': 'application/json; charset=utf-8',
    'cache-control': 'no-store',
    'access-control-allow-origin': '*',
    'access-control-allow-headers': 'content-type',
    'access-control-allow-methods': 'GET,POST,PUT,DELETE,OPTIONS',
  });
  res.end(JSON.stringify(obj));
}

// The one command grammar: a board name, a command name, and a value that is a scalar
// or a flat object (the drone sticks send four axes at once). Anything deeper, or with
// an array in it, is not something the page knows how to apply, so it is refused here
// rather than half-executed there.
function checkCmd(c) {
  const flat = v => v !== null && typeof v === 'object' && !Array.isArray(v)
    && Object.values(v).every(x => typeof x === 'number' || typeof x === 'boolean' || typeof x === 'string');
  if (typeof c.board !== 'string' || !/^[a-z]{2,8}$/.test(c.board)) return 'board 名称不对';
  if (typeof c.cmd !== 'string' || !/^[a-z_]{2,16}$/.test(c.cmd)) return 'cmd 名称不对';
  if (!(c.value === undefined || ['number', 'boolean', 'string'].includes(typeof c.value) || flat(c.value))) {
    return 'value 要是标量或扁平对象';
  }
  return null;
}

function readBody(req, limit, then) {
  let size = 0;
  const chunks = [];
  req.on('data', c => {
    size += c.length;
    if (size > limit) { req.destroy(); then(new Error('body too large'), null); return; }
    chunks.push(c);
  });
  req.on('end', () => then(null, Buffer.concat(chunks).toString('utf8')));
  req.on('error', e => then(e, null));
}

// The phone endpoints, matched exactly. A prefix match here would have swallowed
// the editor's own /api/compile/start -- same prefix, different owner -- and the
// simulator would report "compile finished but no compiledProgram" instead of
// compiling, which is what it did until this line was made specific.
//
// /api/metrics/run is the embedded editor's, not ours: the page posts one run report per
// run and ignores the answer (`await api.post(...)` inside a catch). The self-hosted
// Velxio is the OSS image, which ships no metrics router -- its own main.py says the
// metrics router used to be wired up and is gated out -- so forwarded, that POST comes
// back 404 and paints the console red on every single run. It is telemetry neither
// this server nor the image keeps, so it is answered here and dropped instead of being
// sent through to a door that does not exist.
const API = new Set(['/api/hello', '/api/state', '/api/report', '/api/cmd', '/api/cmds', '/api/metrics/run']);

// Returns true when the request was one of ours and has been answered.
function api(req, res) {
  const url = req.url.split('?')[0];
  if (!API.has(url)) return false;
  if (req.method === 'OPTIONS') { json(res, 204, {}); return true; }

  if (url === '/api/metrics/run' && req.method === 'POST') {
    // Drain the body before answering: a request that is answered with its body unread
    // is one the browser may still be writing into, and the reset shows up as a second
    // console error in place of the 404 that was just removed.
    req.resume();
    // 200 with a body rather than 204: `json()` always writes one, and a body on a 204
    // is not a legal response.
    json(res, 200, { ok: true });
    return true;
  }

  if (url === '/api/hello' && req.method === 'GET') {
    json(res, 200, { ok: true, boards: ['light', 'drone', 'water', 'power', 'fire'],
                    stateAgeMs: mailbox.stateAt ? Date.now() - mailbox.stateAt : null });
    return true;
  }
  if (url === '/api/state' && req.method === 'GET') {
    json(res, 200, {
      ok: true,
      ageMs: mailbox.stateAt ? Date.now() - mailbox.stateAt : null,
      boards: mailbox.state?.boards ?? null,
      scene: mailbox.state?.scene ?? null,
      boardsUp: mailbox.state?.boardsUp ?? null,
      // The page's answer to the last command it drained: applied, or refused with
      // the reason. Without it a phone would have to guess whether its button did
      // anything at all.
      ack: mailbox.state?.ack ?? null,
    });
    return true;
  }
  if (url === '/api/report' && req.method === 'POST') {
    readBody(req, STATE_MAX_BYTES, (err, text) => {
      if (err) { json(res, 413, { ok: false, error: String(err.message) }); return; }
      let parsed;
      try { parsed = JSON.parse(text); } catch (e) { json(res, 400, { ok: false, error: '不是 JSON' }); return; }
      if (!parsed || typeof parsed !== 'object' || !parsed.boards) {
        json(res, 400, { ok: false, error: '缺 boards' });
        return;
      }
      mailbox.state = parsed;
      mailbox.stateAt = Date.now();
      json(res, 200, { ok: true, pending: mailbox.cmds.length });
    });
    return true;
  }
  if (url === '/api/cmd' && req.method === 'POST') {
    readBody(req, 4096, (err, text) => {
      if (err) { json(res, 413, { ok: false, error: String(err.message) }); return; }
      let parsed;
      try { parsed = JSON.parse(text); } catch { json(res, 400, { ok: false, error: '不是 JSON' }); return; }
      const c = parsed || {};
      // `value` is a scalar or a flat object (the drone sticks send four axes at
      // once). Anything deeper, or with an array in it, is not something the page
      // knows how to apply, so it is refused here rather than half-executed there.
      const bad = checkCmd(c);
      if (bad) {
        json(res, 400, { ok: false, error: `指令格式不对（board/cmd/value）：${bad}` });
        return;
      }
      if (mailbox.cmds.length >= CMD_LIMIT) { json(res, 429, { ok: false, error: '指令队列已满，等页面消化' }); return; }
      const item = { id: ++mailbox.cmdSeq, board: c.board, cmd: c.cmd, value: c.value, at: Date.now() };
      mailbox.cmds.push(item);
      json(res, 200, { ok: true, id: item.id, queued: mailbox.cmds.length });
    });
    return true;
  }
  if (url === '/api/cmds' && req.method === 'GET') {
    // Drained by the page: a command is delivered once, and the page's own ack
    // (the next reported state) is the only proof it took effect.
    const out = mailbox.cmds.splice(0, mailbox.cmds.length);
    json(res, 200, { ok: true, cmds: out });
    return true;
  }
  json(res, 404, { ok: false, error: `没有这个接口：${url}` });
  return true;
}

/* ---------- three vendor patches, in the proxy -------------------------------- */
// Everything the embedded editor says that we do not want in the console, and the two
// attributes Chrome's Issues panel asks for, answered where they can be answered without
// editing the image: the entry bundle is rewritten on the way through, and the editor's
// HTML shell gets one labeler script appended. Nothing here changes what the editor does
// -- a log level, a wire that is not drawn before it has coordinates, and an id/name on
// fields that already work -- and every substitution is matched literally, so an image
// that changes shape makes a patch stale rather than wrong.
//
// First, the wires.
// The embedded editor renders a project's wires in the same commit that adds its parts,
// and works out where those wires actually go one animation frame later
// (`loadProjectState` -> `requestAnimationFrame(recalculateAllWirePositions)`). On that
// first frame every wire end resolves to nothing, so the console fills with two species
// of red line, per wire, on every project load:
//
//   [pinPositionCalculator] Component k2 not found in DOM
//   Error: <path> attribute d: Expected number, "M undefined undefined L ..."
//
// It is the editor's ordering, not our data. Measured on the running image: its own
// Blink example (three parts, no ranch data in it) logs the first, a two-part project
// logs both, and after the pin fix in tools/circuits.mjs our six boards log neither a
// bad pin nor a dropped part. Both lines are cosmetic -- the next frame draws every wire
// in the right place -- and both come out of two minified functions in one asset, so
// they are answered here instead of by editing the image, which the next
// `docker compose pull` would throw away.
//
//   1. a part that is not mounted *yet* is not an error: the message drops to debug, so
//      it is still there for anyone who turns Verbose on. A pin that is genuinely wrong
//      keeps its warning, because that one is an authoring mistake worth seeing.
//   2. a wire whose ends have no coordinates yet draws nothing, instead of drawing
//      "M undefined undefined" and making React complain about the `d` attribute.
//
// Both substitutions are matched literally: an asset without them is served untouched,
// so a future image that renames or splits this code makes the patch go stale (the
// console goes noisy again) and never corrupts the editor. Patched bytes are cached for
// the life of the process.
const VENDOR_PATCHES = [
  {
    what: 'unmounted part: warning -> debug',
    from: 'if(!s)return console.warn(`[pinPositionCalculator] Component ${t} not found in DOM`),null;',
    to: 'if(!s)return console.debug(`[pinPositionCalculator] Component ${t} not found in DOM`),null;',
  },
  {
    what: 'wire path: skip until the ends have coordinates',
    from: 'function uM(t,e,n){const i=[t,...e??[],n];',
    to: 'function uM(t,e,n){if(!t||!n||!isFinite(t.x)||!isFinite(t.y)||!isFinite(n.x)||!isFinite(n.y))return"";const i=[t,...e??[],n];',
  },
];

// The run path is also narrated with plain `console.log`: 16 lines per Run click (the
// metadata count, [handleRun] at every branch, [verify], the compile request and its
// job, [compileBoardProgram], the QEMU boot over the ESP32 bridge) plus the rest of that
// bridge's chatter, which would print if the link ever dropped. They are development
// prints, not errors, and the same treatment fits: `console.log` -> `console.debug`, so
// the console is quiet by default and every one of them comes back when Verbose is on.
//
// Each entry is `console.log(` plus the first argument's own literal, so it matches one
// call site and nothing else -- a message that changes shape in a future build stops
// matching and simply prints again, rather than silencing a neighbour by accident.
const VENDOR_QUIET = [
  'console.log(`Loaded ${this.allComponents.length} components from metadata`',
  'console.log("[handleRun] click"',
  'console.log("[handleRun] active board"',
  'console.log("[handleRun] QEMU path"',
  'console.log("[handleRun] auto-compile + run"',
  'console.log("[handleRun] after compile"',
  'console.log("[handleRun] → startBoard"',
  'console.log("[handleRun] → startBoard (already compiled)"',
  'console.log("[handleRun] → startBoard (QEMU-Linux, no firmware)"',
  'console.log("[verify]"',
  'console.log("Sending compilation request to:"',
  'console.log("Board:"',
  'console.log("Files:"',
  'console.log("[compile] queued job"',
  'console.log(`[compile] job ${u} done in',
  'console.log(`[compileBoardProgram] ',
  'console.log(`[Esp32Bridge:${this.boardId}] WebSocket connected',
  'console.log(`[Esp32Bridge:${this.boardId}] WebSocket closed',
  'console.log(`[Esp32Bridge:${this.boardId}] system event:',
  'console.log(`[Esp32Bridge:${this.boardId}] wifi_status:',
  'console.log(`[Esp32Bridge:${this.boardId}] ble_status:',
];

const vendorCache = new Map();

// One buffered GET to the editor, asking for the bytes uncompressed -- the patch is a
// text substitution and nginx would otherwise answer with gzip.
function upstreamGet(pathname) {
  return new Promise((resolve, reject) => {
    const r = http.request(
      { ...UPSTREAM, path: pathname, method: 'GET', headers: { host: `${UPSTREAM.host}:${UPSTREAM.port}`, 'accept-encoding': 'identity' } },
      up => {
        const chunks = [];
        up.on('data', c => chunks.push(c));
        up.on('end', () => resolve({ status: up.statusCode ?? 502, type: up.headers['content-type'], body: Buffer.concat(chunks) }));
      },
    );
    r.on('error', reject);
    r.end();
  });
}

// Returns true when the request was the entry bundle and has been answered.
async function vendorAsset(req, res, url) {
  // The wire code lives in the entry bundle; every other chunk is proxied untouched.
  if (req.method !== 'GET' || !/^\/assets\/index-[\w.-]+\.js$/.test(url)) return false;

  let hit = vendorCache.get(url);
  if (!hit) {
    let up;
    try {
      up = await upstreamGet(url);
    } catch {
      return false; // let the plain proxy answer, so the error message stays the usual one
    }
    if (up.status !== 200) return false;
    let body = up.body.toString('utf8');
    const applied = [];
    const missed = [];
    for (const p of VENDOR_PATCHES) {
      if (!body.includes(p.from)) { missed.push(p.what); continue; }
      body = body.split(p.from).join(p.to);
      applied.push(p.what);
    }
    let quiet = 0;
    for (const from of VENDOR_QUIET) {
      if (!body.includes(from)) { missed.push(`静音: ${from.slice(12, 60)}`); continue; }
      body = body.split(from).join(`console.debug(${from.slice('console.log('.length)}`);
      quiet++;
    }
    hit = { body: Buffer.from(body, 'utf8'), applied, missed };
    vendorCache.set(url, hit);
    console.log(`[vendor] ${url}: 语义补丁 ${applied.length}/${VENDOR_PATCHES.length}，` +
      `静音 console.log ${quiet}/${VENDOR_QUIET.length}`);
    if (missed.length) {
      console.log(`[vendor] ${url}: ${missed.length} 处没匹配上（镜像可能换过前端），这些原样留着：${missed.join('；')}`);
    }
  }

  res.writeHead(200, {
    'content-type': hit.type || 'text/javascript; charset=utf-8',
    // no-store because the patch is ours and the URL is the upstream hash: a stale copy
    // in the browser would outlive a change to the substitutions above.
    'cache-control': 'no-store',
  });
  res.end(hit.body);
  return true;
}

// Chrome's Issues panel flags the editor's own hidden inputs and its Monaco IME textarea:
// "A form field element has neither an id nor a name attribute" (four hidden `type=file`
// pickers -- import .vlx, upload firmware, image-to-code -- plus Monaco's aria-hidden
// proxy textarea, and whatever a dialog adds later). It is a recommendation, not a
// fault: nothing is autofilled there. Our own pages have none of these, so the answer
// is a labeler injected into the editor's HTML shell, which names each field as it
// appears.
//
// It runs inside the editor document, so it covers every instance served through this
// proxy (the ranch drawer, a phone): the first pass plus one debounced
// pass per frame while the editor keeps mutating. Fields that already carry an id or a
// name are left exactly as they are, and the injection is guarded on two markers of the
// shell -- an HTML response without both is passed through byte for byte.
const EDITOR_SHELL_MARKERS = ['id="root"', '</body>'];
const EDITOR_LABEL_SCRIPT = [
  '<script>(function(){',
  'var n=0,pending=false;',
  'function label(){var f=document.querySelectorAll("input,select,textarea");',
  // Per attribute, not per element: a field that raced the first pass and got only one of
  // the two is finished off by the next one, and a field that carries its own id keeps it.
  // The missing half mirrors the one that exists, so a field ends up with the same value
  // in both rather than numbers from two different passes.
  'for(var i=0;i<f.length;i++){var el=f[i];',
  'var hasName=!!el.getAttribute("name"),hasId=!!el.id;',
  'if(hasName&&hasId)continue;n++;',
  'var v="velxio-"+el.tagName.toLowerCase()+"-"+n;',
  'if(!hasName)el.setAttribute("name",hasId?el.id:v);',
  'if(!hasId)el.setAttribute("id",hasName?el.getAttribute("name"):v);}}',
  'function schedule(){if(pending)return;pending=true;requestAnimationFrame(function(){pending=false;label();});}',
  'label();if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",label);',
  // attributes too: the editor re-creates these fields as it opens dialogs, and a field
  // that loses one of the two attributes gets it back on the next pass (the pass that
  // writes them changes nothing else, so this settles after one extra frame).
  'new MutationObserver(schedule).observe(document.documentElement,{childList:true,subtree:true,attributes:true,attributeFilter:["id","name"]});',
  '})();</script>',
].join('');

const htmlCache = new Map();

// Returns true when the request was a document from the editor and has been answered.
async function vendorHtml(req, res, url) {
  // Only navigations: the SPA asks for its documents with `Accept: text/html`, while its
  // assets, wasm and API calls do not -- buffering those to look for a </body> would put
  // megabytes through this function for nothing.
  if (req.method !== 'GET' || !(req.headers.accept || '').includes('text/html')) return false;

  let hit = htmlCache.get(url);
  if (!hit) {
    let up;
    try {
      up = await upstreamGet(url);
    } catch {
      return false;
    }
    if (up.status !== 200 || !/text\/html/i.test(up.type || '')) return false;
    const html = up.body.toString('utf8');
    const isShell = EDITOR_SHELL_MARKERS.every(m => html.includes(m));
    if (!isShell) return false; // not the editor's shell: let the plain proxy answer
    const body = Buffer.from(html.replace('</body>', `${EDITOR_LABEL_SCRIPT}</body>`), 'utf8');
    hit = { body, type: up.type };
    htmlCache.set(url, hit);
    console.log(`[vendor] ${url}: 已注入表单字段标注器（编辑器 HTML 壳）`);
  }

  res.writeHead(200, { 'content-type': hit.type || 'text/html; charset=utf-8', 'cache-control': 'no-store' });
  res.end(hit.body);
  return true;
}

const server = http.createServer(async (req, res) => {
  const url = decodeURIComponent(req.url.split('?')[0]);
  console.log('[SERVER] URL:', url);
  console.log('[SERVER] checking api()...');
  if (api(req, res)) return true;
  // A path this server serves itself is never asked of Velxio. The editor's HTML pass
  // below forwards *any* navigation upstream, and Velxio's SPA answers 200 with its own
  // shell for paths it does not know -- which is how a local page used to be replaced by
  // the simulator's marketing shell. The local table wins; the label injector only ever
  // needed the documents nobody else owns.
  const localRoute = ROUTES.some(([re]) => re.test(url));
  if (!localRoute) {
    if (await vendorAsset(req, res, url)) return;
    if (await vendorHtml(req, res, url)) return;
  }
  for (const [re, to] of ROUTES) {
    const m = url.match(re);
    if (!m) continue;
    const file = to(m);
    if (!file.startsWith(ROOT)) continue;
    fs.readFile(file, (err, buf) => {
      if (err) {
        res.writeHead(404, { 'content-type': 'text/plain' });
        res.end(`404 ${path.relative(ROOT, file)}`);
        return;
      }
      res.writeHead(200, { 'content-type': MIME[path.extname(file)] || 'application/octet-stream', 'cache-control': 'no-store' });
      res.end(buf);
    });
    return;
  }

  const proxy = http.request(
    { ...UPSTREAM, path: req.url, method: req.method, headers: { ...req.headers, host: `${UPSTREAM.host}:${UPSTREAM.port}` } },
    up => {
      res.writeHead(up.statusCode ?? 502, up.headers);
      up.pipe(res);
    },
  );
  proxy.on('error', e => {
    res.writeHead(502, { 'content-type': 'text/plain' });
    res.end(`upstream ${UPSTREAM.port} unreachable: ${e.message}`);
  });
  req.pipe(proxy);
});

server.on('upgrade', (req, socket) => {
  const target = net.connect(UPSTREAM.port, UPSTREAM.host, () => {
    const headers = Object.entries(req.headers)
      .map(([k, v]) => `${k}: ${Array.isArray(v) ? v.join(', ') : v}`)
      .join('\r\n');
    target.write(`${req.method} ${req.url} HTTP/1.1\r\n${headers}\r\n\r\n`);
    socket.pipe(target).pipe(socket);
  });
  target.on('error', () => socket.destroy());
  socket.on('error', () => target.destroy());
});

server.listen(PORT, '0.0.0.0', () => {
  // The phone has to be able to type this in, so print the LAN address rather than
  // localhost.
  const ips = Object.values(os.networkInterfaces()).flat()
    .filter(i => i && i.family === 'IPv4' && !i.internal).map(i => i.address);
  console.log(`ranch on http://localhost:${PORT}/  (upstream velxio :${UPSTREAM.port})`);
  console.log(`手机用: ${ips.map(i => `http://${i}:${PORT}/api/hello`).join('  ') || '没有局域网 IPv4'}`);
});
