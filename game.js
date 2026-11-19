'use strict';
/* MTA 6 - eigenständiges Open-World-Fahrspiel (Three.js) */

// ---------- Konstanten & Helfer ----------
const P = 80, N = 8, HALF = N * P / 2, RW = 16;
const R = k => k * P - HALF;
const rnd = (a, b) => a + Math.random() * (b - a);
const pick = a => a[Math.floor(Math.random() * a.length)];
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const lerp = (a, b, t) => a + (b - a) * t;
const angDiff = (a, b) => {
  let d = (b - a) % (Math.PI * 2);
  if (d > Math.PI) d -= Math.PI * 2;
  if (d < -Math.PI) d += Math.PI * 2;
  return d;
};
const $ = id => document.getElementById(id);

// ---------- Renderer / Szene ----------
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.setSize(innerWidth, innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.domElement.id = 'game';
document.body.insertBefore(renderer.domElement, document.body.firstChild);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x8fc3ee);
scene.fog = new THREE.Fog(0x8fc3ee, 200, 1000);
const camera = new THREE.PerspectiveCamera(65, innerWidth / innerHeight, 0.5, 1800);

const hemi = new THREE.HemisphereLight(0xbfd8ff, 0x555555, 0.8);
scene.add(hemi);
const sun = new THREE.DirectionalLight(0xfff2d0, 1.0);
sun.castShadow = true;
sun.shadow.mapSize.set(2048, 2048);
Object.assign(sun.shadow.camera, { left: -100, right: 100, top: 100, bottom: -100, near: 1, far: 600 });
scene.add(sun, sun.target);

addEventListener('resize', () => {
  renderer.setSize(innerWidth, innerHeight);
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
});

// ---------- Texturen ----------
function makeRoadTex() {
  const c = document.createElement('canvas'); c.width = 512; c.height = 128;
  const g = c.getContext('2d');
  g.fillStyle = '#2c2c30'; g.fillRect(0, 0, 512, 128);
  for (let i = 0; i < 1800; i++) {
    g.fillStyle = `rgba(255,255,255,${Math.random() * 0.04})`;
    g.fillRect(Math.random() * 512, Math.random() * 128, 2 + Math.random() * 3, 2);
  }
  // Mittellinie (doppelt, gelb) nur zwischen den Kreuzungen
  g.fillStyle = '#e8b923';
  g.fillRect(77, 60, 358, 3); g.fillRect(77, 65, 358, 3);
  // Randlinien
  g.fillStyle = '#d8d8d8';
  g.fillRect(51, 6, 410, 3); g.fillRect(51, 119, 410, 3);
  // Zebrastreifen
  g.fillStyle = 'rgba(240,240,240,.85)';
  for (let v = 14; v < 114; v += 12) { g.fillRect(53, v, 18, 6); g.fillRect(441, v, 18, 6); }
  const t = new THREE.CanvasTexture(c);
  t.wrapS = THREE.RepeatWrapping; t.anisotropy = 8;
  t.repeat.set(N + 0.2, 1); t.offset.x = -0.1;
  return t;
}

const winCanv = [];
for (let v = 0; v < 4; v++) {
  const m = document.createElement('canvas'), e = document.createElement('canvas');
  m.width = m.height = e.width = e.height = 128;
  const gm = m.getContext('2d'), ge = e.getContext('2d');
  gm.fillStyle = '#d8d8d8'; gm.fillRect(0, 0, 128, 128);
  ge.fillStyle = '#000'; ge.fillRect(0, 0, 128, 128);
  for (let cy = 0; cy < 4; cy++) for (let cx = 0; cx < 4; cx++) {
    const x = cx * 32 + 6, y = cy * 32 + 5;
    gm.fillStyle = '#22313f'; gm.fillRect(x, y, 20, 22);
    gm.fillStyle = 'rgba(255,255,255,.12)'; gm.fillRect(x, y, 20, 6);
    if (Math.random() < 0.45) { ge.fillStyle = pick(['#ffcf70', '#ffe2a0', '#9fd4ff']); ge.fillRect(x, y, 20, 22); }
  }
  winCanv.push({ m, e });
}
const winCache = {};
function getWinTex(v, rx, ry) {
  const key = v + '_' + rx + '_' + ry;
  if (winCache[key]) return winCache[key];
  const mk = cv => {
    const t = new THREE.CanvasTexture(cv);
    t.wrapS = t.wrapT = THREE.RepeatWrapping; t.repeat.set(rx, ry); t.anisotropy = 4;
    return t;
  };
  return (winCache[key] = { m: mk(winCanv[v].m), e: mk(winCanv[v].e) });
}

// ---------- Stadt ----------
const winMats = [];
const blocks = [];
const roofMat = new THREE.MeshStandardMaterial({ color: 0x55585e, roughness: 1 });

const ground = new THREE.Mesh(new THREE.PlaneGeometry(4000, 4000), new THREE.MeshStandardMaterial({ color: 0x4a5d3a, roughness: 1 }));
ground.rotation.x = -Math.PI / 2; ground.position.y = -0.05; ground.receiveShadow = true;
scene.add(ground);

(function buildRoads() {
  const tex = makeRoadTex();
  const mat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.95 });
  const geo = new THREE.PlaneGeometry(N * P + 16, RW);
  for (let k = 0; k <= N; k++) {
    const a = new THREE.Mesh(geo, mat);
    a.rotation.x = -Math.PI / 2; a.position.set(0, 0.05, R(k)); a.receiveShadow = true; scene.add(a);
    const b = new THREE.Mesh(geo, mat);
    b.rotation.x = -Math.PI / 2; b.receiveShadow = true;
    const grp = new THREE.Group(); grp.add(b); grp.rotation.y = Math.PI / 2; grp.position.set(R(k), 0.06, 0);
    scene.add(grp);
  }
})();

function makeBuilding(cx, cz, w, d, h, color) {
  const v = Math.floor(Math.random() * 4);
  const ry = clamp(Math.round(h / 16), 1, 8);
  const t1 = getWinTex(v, clamp(Math.round(d / 16), 1, 4), ry);
  const t2 = getWinTex(v, clamp(Math.round(w / 16), 1, 4), ry);
  const mk = t => {
    const m = new THREE.MeshStandardMaterial({ color, map: t.m, emissive: 0xffffff, emissiveMap: t.e, emissiveIntensity: 0, roughness: 0.8 });
    winMats.push(m); return m;
  };
  const mx = mk(t1), mz = mk(t2);
  const mesh = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), [mx, mx, roofMat, roofMat, mz, mz]);
  mesh.position.set(cx, h / 2 + 0.3, cz);
  mesh.castShadow = mesh.receiveShadow = true;
  scene.add(mesh);
  if (h > 34) {
    const ant = new THREE.Mesh(new THREE.CylinderGeometry(0.2, 0.3, 10, 6), roofMat);
    ant.position.set(cx, h + 5.3, cz); scene.add(ant);
  }
}

const wallCols = [0xcfcfd4, 0xd8c8b0, 0xb7c3d0, 0xc9b9b0, 0xa8b0b8, 0xe0d8c8, 0x9aa5b5];
const trunkGeo = new THREE.CylinderGeometry(0.4, 0.5, 3, 6), trunkMat = new THREE.MeshStandardMaterial({ color: 0x6b4a2b });
const crownGeo = new THREE.ConeGeometry(2.6, 6.5, 7), crownMat = new THREE.MeshStandardMaterial({ color: 0x2f7a3a });

for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) {
  const cx = R(i) + P / 2, cz = R(j) + P / 2;
  const side = new THREE.Mesh(new THREE.BoxGeometry(64, 0.3, 64), new THREE.MeshStandardMaterial({ color: 0x9a9a98, roughness: 1 }));
  side.position.set(cx, 0.15, cz); side.receiveShadow = true; scene.add(side);
  const park = Math.random() < 0.14;
  blocks.push({ x0: cx - 29, x1: cx + 29, z0: cz - 29, z1: cz + 29, cx, cz, park });
  if (park) {
    const gr = new THREE.Mesh(new THREE.BoxGeometry(56, 0.1, 56), new THREE.MeshStandardMaterial({ color: 0x4e8c3a, roughness: 1 }));
    gr.position.set(cx, 0.33, cz); gr.receiveShadow = true; scene.add(gr);
    for (let t = 0; t < 14; t++) {
      const tx = cx + rnd(-25, 25), tz = cz + rnd(-25, 25);
      const tr = new THREE.Mesh(trunkGeo, trunkMat); tr.position.set(tx, 1.8, tz); tr.castShadow = true; scene.add(tr);
      const cr = new THREE.Mesh(crownGeo, crownMat); cr.position.set(tx, 6, tz); cr.castShadow = true; scene.add(cr);
    }
    continue;
  }
  const n = pick([1, 2, 2, 4]);
  const dist = Math.hypot(cx, cz) / (HALF * 1.2);
  const maxH = lerp(80, 16, clamp(dist, 0, 1));
  const lots = [];
  if (n === 1) lots.push([cx, cz, rnd(40, 54), rnd(40, 54)]);
  else if (n === 2) {
    if (Math.random() < 0.5) lots.push([cx - 14, cz, 26, rnd(44, 54)], [cx + 14, cz, 26, rnd(44, 54)]);
    else lots.push([cx, cz - 14, rnd(44, 54), 26], [cx, cz + 14, rnd(44, 54), 26]);
  } else {
    for (const sx of [-1, 1]) for (const sz of [-1, 1]) lots.push([cx + sx * 14, cz + sz * 14, rnd(22, 26), rnd(22, 26)]);
  }
  for (const [lx, lz, w, d] of lots) makeBuilding(lx, lz, w, d, Math.round(rnd(10, maxH)), pick(wallCols));
}

// Berge am Horizont
for (let i = 0; i < 28; i++) {
  const a = i / 28 * Math.PI * 2, r = rnd(900, 1100);
  const m = new THREE.Mesh(new THREE.ConeGeometry(rnd(100, 180), rnd(120, 260), 5), new THREE.MeshStandardMaterial({ color: 0x56705a, roughness: 1, flatShading: true }));
  m.position.set(Math.cos(a) * r, 40, Math.sin(a) * r); scene.add(m);
}

// ---------- Fahrzeuge ----------
const gBody = new THREE.BoxGeometry(2.0, 0.7, 4.4), gCab = new THREE.BoxGeometry(1.75, 0.65, 2.2);
const gWheel = new THREE.CylinderGeometry(0.42, 0.42, 0.3, 12), gLamp = new THREE.BoxGeometry(0.4, 0.2, 0.1);
const matWheel = new THREE.MeshStandardMaterial({ color: 0x111111 });
const matGlass = new THREE.MeshStandardMaterial({ color: 0x1b2a3a, roughness: 0.2, metalness: 0.6 });
const matHead = new THREE.MeshBasicMaterial({ color: 0xfff3c0 });
const matTail = new THREE.MeshBasicMaterial({ color: 0xff2020 });
const bodyMats = {};
const bodyMat = c => bodyMats[c] || (bodyMats[c] = new THREE.MeshStandardMaterial({ color: c, roughness: 0.4, metalness: 0.3 }));

function makeCar(color, police) {
  const g = new THREE.Group(), body = new THREE.Group();
  g.add(body); g.userData.body = body;
  const mb = new THREE.Mesh(gBody, bodyMat(color)); mb.position.y = 0.75; mb.castShadow = true; body.add(mb);
  const cab = new THREE.Mesh(gCab, matGlass); cab.position.set(0, 1.4, -0.25); cab.castShadow = true; body.add(cab);
  for (const sx of [-1, 1]) {
    for (const sz of [1.4, -1.4]) {
      const w = new THREE.Mesh(gWheel, matWheel); w.rotation.z = Math.PI / 2; w.position.set(sx * 1.05, 0.42, sz); g.add(w);
    }
    const h = new THREE.Mesh(gLamp, matHead); h.position.set(sx * 0.7, 0.85, 2.2); body.add(h);
    const t = new THREE.Mesh(gLamp, matTail); t.position.set(sx * 0.7, 0.9, -2.2); body.add(t);
  }
  if (police) {
    const st = new THREE.Mesh(new THREE.BoxGeometry(2.02, 0.25, 2.6), bodyMat(0xf2f2f2)); st.position.set(0, 0.8, 0); body.add(st);
    const lr = new THREE.Mesh(new THREE.BoxGeometry(0.7, 0.18, 0.4), new THREE.MeshBasicMaterial({ color: 0xff2020 }));
    const lb = new THREE.Mesh(new THREE.BoxGeometry(0.7, 0.18, 0.4), new THREE.MeshBasicMaterial({ color: 0x2040ff }));
    lr.position.set(-0.45, 1.8, -0.25); lb.position.set(0.45, 1.8, -0.25); body.add(lr, lb);
    g.userData.lr = lr; g.userData.lb = lb;
  }
  return g;
}

// ---------- Spieler ----------
const pl = { x: -4, z: -40, h: 0, speed: 0, steer: 0, hp: 100, hitCool: 0 };
const playerMesh = makeCar(0xe03a3e);
scene.add(playerMesh);
const spot = new THREE.SpotLight(0xfff0cc, 0, 80, 0.5, 0.5, 1);
spot.position.set(0, 1.2, 2); spot.target.position.set(0, 0, 30);
playerMesh.add(spot, spot.target);

const keys = {};
addEventListener('keydown', e => {
  keys[e.code] = true;
  if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Space'].includes(e.code)) e.preventDefault();
  if (e.code === 'KeyR' && started && state === 'play') resetCar();
  if (e.code === 'KeyM') muted = !muted, audio && (audio.gain.gain.value = muted ? 0 : 0.05);
});
addEventListener('keyup', e => { keys[e.code] = false; });

// ---------- Spielzustand ----------
let started = false, state = 'play', muted = false;
let money = 1000, heat = 0, lastCrime = -99, bustT = 0, wastedT = 0, shake = 0, gtime = 0;
let hour = 9;
const cooldown = { traffic: 0 };

function toast(txt, ms = 2200) {
  const t = $('toast'); t.textContent = txt; t.style.opacity = 1;
  clearTimeout(toast.t); toast.t = setTimeout(() => (t.style.opacity = 0), ms);
}
function addHeat(v) { heat = Math.min(5, heat + v); lastCrime = gtime; }
function hurt(v) {
  pl.hp = Math.max(0, pl.hp - v); shake = Math.min(1.2, shake + v / 20);
  const f = $('flash'); f.style.opacity = 1; setTimeout(() => (f.style.opacity = 0), 120);
}

// ---------- Pfad-Agenten (Verkehr, Fußgänger, Polizei) ----------
function randomRoadState() {
  return { axis: Math.random() < 0.5 ? 0 : 1, line: Math.floor(rnd(0, N + 1)), sign: Math.random() < 0.5 ? 1 : -1, pos: rnd(-HALF + 5, HALF - 5) };
}
function setOff(a) {
  if (a.kind === 'ped') a.off = a.side * 9.5;
  else if (a.kind === 'police') a.off = 0;
  else a.off = a.axis === 0 ? 4 * a.sign : -4 * a.sign;
}
function computeTarget(a) {
  const c = R(a.line);
  if (a.axis === 0) { a.tx = a.pos; a.tz = c + a.off; } else { a.tx = c + a.off; a.tz = a.pos; }
  a.th = Math.atan2(a.axis === 0 ? a.sign : 0, a.axis === 1 ? a.sign : 0);
}
function atIntersection(a, m) {
  const k = a.line;
  const opts = [];
  const straightOK = !(m === N && a.sign > 0) && !(m === 0 && a.sign < 0);
  const ix = a.axis === 0 ? R(m) : R(k), iz = a.axis === 0 ? R(k) : R(m);
  if (straightOK) {
    const dx = a.axis === 0 ? a.sign : 0, dz = a.axis === 1 ? a.sign : 0;
    opts.push({ s: 0, w: 0.6, px: ix + dx * P * 0.7, pz: iz + dz * P * 0.7 });
  }
  for (const s of [1, -1]) {
    if ((k === 0 && s < 0) || (k === N && s > 0)) continue;
    const dx = a.axis === 0 ? 0 : s, dz = a.axis === 0 ? s : 0;
    opts.push({ s, w: 0.2, px: ix + dx * P * 0.7, pz: iz + dz * P * 0.7 });
  }
  let ch;
  if (a.kind === 'police' && Math.random() > 0.1) {
    ch = opts.reduce((b, o) => (Math.hypot(o.px - pl.x, o.pz - pl.z) < Math.hypot(b.px - pl.x, b.pz - pl.z) ? o : b));
  } else {
    let tot = opts.reduce((s, o) => s + o.w, 0), r = Math.random() * tot;
    ch = opts[opts.length - 1];
    for (const o of opts) { r -= o.w; if (r <= 0) { ch = o; break; } }
  }
  if (ch.s === 0) return;
  a.axis = 1 - a.axis; a.line = m; a.sign = ch.s; a.pos = R(k) + ch.s * 1;
  if (a.kind === 'ped') a.side = Math.random() < 0.5 ? -1 : 1;
  setOff(a);
}
function advance(a, dt) {
  const prev = a.pos; a.pos += a.sign * a.v * dt;
  const k1 = Math.floor((prev + HALF) / P), k2 = Math.floor((a.pos + HALF) / P);
  if (k1 !== k2) atIntersection(a, a.sign > 0 ? k2 : k1);
}
function syncMesh(a, dt, rate) {
  computeTarget(a);
  const f = 1 - Math.exp(-dt * rate), m = a.mesh;
  m.position.x += (a.tx - m.position.x) * f;
  m.position.z += (a.tz - m.position.z) * f;
  m.rotation.y += angDiff(m.rotation.y, a.th) * f;
}
function blockedAhead(a, others) {
  const m = a.mesh, fx = Math.sin(m.rotation.y), fz = Math.cos(m.rotation.y);
  for (const o of others) {
    if (o === m) continue;
    const dx = o.position.x - m.position.x, dz = o.position.z - m.position.z;
    const fwd = dx * fx + dz * fz;
    if (fwd > 0 && fwd < 11 && Math.abs(dx * fz - dz * fx) < 2.6) return true;
  }
  return false;
}

const traffic = [], police = [], peds = [];
const carCols = [0x3a6ee0, 0xf0f0f0, 0x222222, 0xe0c030, 0x3aa860, 0x888c94, 0xb04ad0, 0xe07a2a, 0xc0c0c8];

function makeAgent(kind, state) {
  const a = Object.assign({ kind, v: 0, stun: 0, wait: 0, ghost: 0, side: Math.random() < 0.5 ? -1 : 1 }, state || randomRoadState());
  setOff(a);
  if (kind === 'ped') {
    a.cruise = rnd(1.6, 2.6);
    a.mesh = makePed();
  } else if (kind === 'police') {
    a.cruise = 26 + heat * 2.5;
    a.mesh = makeCar(0x15151c, true);
  } else {
    a.cruise = rnd(11, 19);
    a.mesh = makeCar(pick(carCols));
  }
  computeTarget(a);
  a.mesh.position.set(a.tx, 0, a.tz); a.mesh.rotation.y = a.th;
  scene.add(a.mesh);
  return a;
}

const pedCols = [0xd04a4a, 0x4a7ad0, 0x4ad08a, 0xd0c04a, 0xaa4ad0, 0xeeeeee, 0x333333];
const gPB = new THREE.CylinderGeometry(0.28, 0.28, 1.0, 8), gPH = new THREE.SphereGeometry(0.22, 8, 8);
const skinMat = new THREE.MeshStandardMaterial({ color: 0xe0b090 });
const pedMats = {};
function makePed() {
  const c = pick(pedCols);
  const mat = pedMats[c] || (pedMats[c] = new THREE.MeshStandardMaterial({ color: c }));
  const g = new THREE.Group();
  const b = new THREE.Mesh(gPB, mat); b.position.y = 0.95; b.castShadow = true;
  const h = new THREE.Mesh(gPH, skinMat); h.position.y = 1.65;
  g.add(b, h); return g;
}

for (let i = 0; i < 36; i++) traffic.push(makeAgent('traffic'));
for (let i = 0; i < 70; i++) peds.push(makeAgent('ped'));

// ---------- Kollision ----------
function collideBuildings() {
  const r = 2.0;
  for (const b of blocks) {
    const cx = clamp(pl.x, b.x0, b.x1), cz = clamp(pl.z, b.z0, b.z1);
    let dx = pl.x - cx, dz = pl.z - cz;
    const d2 = dx * dx + dz * dz;
    if (d2 >= r * r) continue;
    let nx, nz;
    if (d2 > 1e-6) { const d = Math.sqrt(d2); nx = dx / d; nz = dz / d; pl.x += nx * (r - d); pl.z += nz * (r - d); }
    else {
      const l = pl.x - b.x0, rr = b.x1 - pl.x, t = pl.z - b.z0, bt = b.z1 - pl.z, mn = Math.min(l, rr, t, bt);
      if (mn === l) { nx = -1; nz = 0; pl.x = b.x0 - r; } else if (mn === rr) { nx = 1; nz = 0; pl.x = b.x1 + r; }
      else if (mn === t) { nx = 0; nz = -1; pl.z = b.z0 - r; } else { nx = 0; nz = 1; pl.z = b.z1 + r; }
    }
    const fx = Math.sin(pl.h), fz = Math.cos(pl.h), dot = fx * nx + fz * nz;
    const into = -dot * pl.speed;
    if (into > 0) {
      if (into > 9) hurt((into - 9) * 1.2);
      pl.speed *= 1 - 0.8 * Math.min(1, Math.abs(dot));
    }
  }
}
function carCollide(o, isPolice) {
  const m = o.mesh, dx = pl.x - m.position.x, dz = pl.z - m.position.z, d = Math.hypot(dx, dz), RR = 3.6;
  if (d >= RR || d < 0.001) return;
  const nx = dx / d, nz = dz / d, fx = Math.sin(pl.h), fz = Math.cos(pl.h);
  const into = -(fx * nx + fz * nz) * pl.speed;
  pl.x += nx * (RR - d); pl.z += nz * (RR - d);
  if (into > 0) pl.speed *= 0.6;
  if (pl.hitCool <= 0) {
    pl.hitCool = 0.5;
    if (isPolice) hurt(2 + o.v * 0.25);
    else if (into > 6) { hurt((into - 6) * 0.9); addHeat(0.35); }
  }
  o.stun = 1.5;
}

// ---------- Polizei ----------
function spawnPolice() {
  for (let t = 0; t < 30; t++) {
    const s = randomRoadState();
    const a = makeAgent('police', s);
    const d = Math.hypot(a.tx - pl.x, a.tz - pl.z);
    if (d > 90 && d < 200) { a.v = 15; police.push(a); return; }
    scene.remove(a.mesh);
  }
}
function clearPolice() { for (const p of police) scene.remove(p.mesh); police.length = 0; }

// ---------- Missionen ----------
const marker = new THREE.Group();
{
  const cyl = new THREE.Mesh(new THREE.CylinderGeometry(5, 5, 70, 20, 1, true), new THREE.MeshBasicMaterial({ color: 0xffd24a, transparent: true, opacity: 0.28, side: THREE.DoubleSide, depthWrite: false }));
  cyl.position.y = 35;
  const ring = new THREE.Mesh(new THREE.TorusGeometry(5, 0.4, 8, 24), new THREE.MeshBasicMaterial({ color: 0xffd24a }));
  ring.rotation.x = Math.PI / 2; ring.position.y = 0.6;
  marker.add(cyl, ring); scene.add(marker);
}
const mission = { x: 0, z: 0, t: 0, n: 0 };
function newMission() {
  for (let t = 0; t < 30; t++) {
    const x = R(Math.floor(rnd(0, N + 1))), z = R(Math.floor(rnd(0, N + 1)));
    if (Math.hypot(x - pl.x, z - pl.z) > 150) {
      mission.x = x; mission.z = z;
      mission.t = Math.hypot(x - pl.x, z - pl.z) / 14 + 20;
      marker.position.set(x, 0, z); return;
    }
  }
}
newMission();

// ---------- Audio ----------
let audio = null;
function initAudio() {
  try {
    const ac = new (window.AudioContext || window.webkitAudioContext)();
    const osc = ac.createOscillator(), osc2 = ac.createOscillator(), f = ac.createBiquadFilter(), gain = ac.createGain();
    osc.type = 'sawtooth'; osc2.type = 'square'; f.type = 'lowpass'; f.frequency.value = 500; gain.gain.value = muted ? 0 : 0.05;
    osc.connect(f); osc2.connect(f); f.connect(gain); gain.connect(ac.destination);
    osc.start(); osc2.start();
    audio = { ac, osc, osc2, gain, f };
  } catch (e) { audio = null; }
}

// ---------- Spielerupdate ----------
function resetCar() {
  const kx = Math.round((pl.x + HALF) / P), kz = Math.round((pl.z + HALF) / P);
  const dx = Math.abs(pl.x - R(clamp(kx, 0, N))), dz = Math.abs(pl.z - R(clamp(kz, 0, N)));
  if (dx < dz) { pl.x = R(clamp(kx, 0, N)) - 4; pl.h = 0; } else { pl.z = R(clamp(kz, 0, N)) + 4; pl.h = Math.PI / 2; }
  pl.speed = 0; money = Math.max(0, money - 50); toast('Fahrzeug zurückgesetzt (−$50)');
}
function updatePlayer(dt) {
  const up = keys.KeyW || keys.ArrowUp, dn = keys.KeyS || keys.ArrowDown;
  const l = keys.KeyA || keys.ArrowLeft, r = keys.KeyD || keys.ArrowRight, hb = keys.Space;
  const MAX = 48, REV = 14;
  if (up) pl.speed += pl.speed < 0 ? 45 * dt : 24 * (1 - Math.max(0, pl.speed) / MAX) * dt;
  else if (dn) pl.speed -= pl.speed > 0.5 ? 45 * dt : 14 * dt;
  else pl.speed *= Math.exp(-0.35 * dt);
  if (hb) pl.speed *= Math.exp(-1.8 * dt);
  pl.speed = clamp(pl.speed, -REV, MAX);
  const st = (l ? 1 : 0) - (r ? 1 : 0);
  pl.steer += (st - pl.steer) * Math.min(1, dt * 8);
  const rate = (2.0 - 1.1 * clamp(Math.abs(pl.speed) / MAX, 0, 1)) * (hb ? 1.5 : 1);
  pl.h += pl.steer * rate * dt * clamp(pl.speed / 8, -1, 1);
  pl.x += Math.sin(pl.h) * pl.speed * dt;
  pl.z += Math.cos(pl.h) * pl.speed * dt;
  pl.x = clamp(pl.x, -HALF - 10, HALF + 10); pl.z = clamp(pl.z, -HALF - 10, HALF + 10);
  collideBuildings();
  pl.hitCool -= dt;
  const m = playerMesh;
  m.position.set(pl.x, 0, pl.z); m.rotation.y = pl.h;
  m.userData.body.rotation.z = -pl.steer * pl.speed * 0.003;
  m.userData.body.rotation.x = (up ? -0.015 : dn ? 0.02 : 0);
  if (audio) {
    audio.osc.frequency.value = 50 + Math.abs(pl.speed) * 4 + (up ? 12 : 0);
    audio.osc2.frequency.value = 25 + Math.abs(pl.speed) * 2;
    audio.f.frequency.value = 400 + Math.abs(pl.speed) * 14;
  }
}

// ---------- Hauptupdate ----------
const allPos = [];
function update(dt) {
  gtime += dt;
  hour = (hour + dt / 20) % 24;

  if (started && state === 'play') updatePlayer(dt);
  else if (!started) {
    pl.speed = 0; playerMesh.position.set(pl.x, 0, pl.z); playerMesh.rotation.y = pl.h;
  }

  // Positionen für Verkehrs-Blockierung
  allPos.length = 0;
  allPos.push(playerMesh);
  for (const t of traffic) allPos.push(t.mesh);
  const npcPos = traffic.map(t => t.mesh);
  for (const p of police) npcPos.push(p.mesh);

  for (const t of traffic) {
    let target = t.cruise;
    if (t.stun > 0) { t.stun -= dt; target = 0; }
    else if (t.ghost > 0) t.ghost -= dt;
    else if (blockedAhead(t, allPos.concat(police.map(p => p.mesh)))) {
      target = 0; t.wait += dt;
      if (t.wait > 4) { t.ghost = 3; t.wait = 0; }
    } else t.wait = 0;
    t.v += clamp(target - t.v, -30 * dt, 12 * dt);
    advance(t, dt); syncMesh(t, dt, 14);
    if (state === 'play' && started) carCollide(t, false);
  }

  const T = gtime;
  for (const p of police) {
    p.cruise = 26 + heat * 2.5;
    let target = p.cruise;
    if (blockedAhead(p, npcPos)) target = 0;
    p.v += clamp(target - p.v, -40 * dt, 14 * dt);
    advance(p, dt); syncMesh(p, dt, 14);
    const u = p.mesh.userData;
    u.lr.material.color.setHex(Math.floor(T * 8) % 2 ? 0xff2020 : 0x300000);
    u.lb.material.color.setHex(Math.floor(T * 8) % 2 ? 0x101040 : 0x3050ff);
    if (state === 'play' && started) carCollide(p, true);
  }

  for (const p of peds) {
    if (p.down > 0) {
      p.down -= dt;
      if (p.down <= 0) {
        p.mesh.rotation.x = 0;
        Object.assign(p, randomRoadState()); setOff(p); computeTarget(p);
        p.mesh.position.set(p.tx, 0, p.tz);
      }
      continue;
    }
    p.v = p.cruise;
    advance(p, dt); syncMesh(p, dt, 10);
    p.mesh.position.y = Math.abs(Math.sin(T * 7 + p.pos)) * 0.06;
    if (started && state === 'play' && Math.abs(pl.speed) > 4) {
      if (Math.hypot(p.mesh.position.x - pl.x, p.mesh.position.z - pl.z) < 2.2) {
        p.down = 5; p.mesh.rotation.x = -Math.PI / 2; p.mesh.position.y = 0.3;
        addHeat(0.7);
      }
    }
  }

  // Polizei-Verwaltung
  const wanted = Math.ceil(heat - 0.001);
  const desired = heat > 0 ? Math.min(8, 1 + wanted * 1) : 0;
  if (police.length < desired && Math.random() < dt * 0.8) spawnPolice();
  if (heat <= 0) {
    for (let i = police.length - 1; i >= 0; i--) {
      const m = police[i].mesh;
      if (Math.hypot(m.position.x - pl.x, m.position.z - pl.z) > 150) { scene.remove(m); police.splice(i, 1); }
    }
  }
  if (heat > 0 && gtime - lastCrime > 8) {
    const near = police.some(p => Math.hypot(p.mesh.position.x - pl.x, p.mesh.position.z - pl.z) < 60);
    if (!near) heat = Math.max(0, heat - 0.12 * dt);
  }
  // Festnahme
  if (started && state === 'play' && heat > 0 && Math.abs(pl.speed) < 3 &&
      police.some(p => Math.hypot(p.mesh.position.x - pl.x, p.mesh.position.z - pl.z) < 7)) {
    bustT += dt;
    if (bustT > 2.5) {
      const fine = Math.min(money, 300); money -= fine; heat = 0; bustT = 0; clearPolice();
      pl.x = -4; pl.z = -40; pl.h = 0; pl.speed = 0;
      toast('FESTGENOMMEN  −$' + fine);
    }
  } else bustT = 0;

  // Totalschaden
  if (state === 'play' && started && pl.hp <= 0) {
    state = 'wasted'; wastedT = 3; pl.speed = 0;
    $('wasted').style.display = 'flex';
  }
  if (state === 'wasted') {
    wastedT -= dt;
    if (wastedT <= 0) {
      state = 'play'; $('wasted').style.display = 'none';
      money = Math.floor(money * 0.9); heat = 0; clearPolice();
      pl.hp = 100; pl.x = -4; pl.z = -40; pl.h = 0; pl.speed = 0;
    }
  }

  // Mission
  mission.t -= dt;
  marker.children[1].rotation.z += dt;
  if (started && state === 'play' && Math.hypot(pl.x - mission.x, pl.z - mission.z) < 9) {
    const bonus = mission.t > 0 ? Math.round(mission.t * 8) : 0;
    const gain = 300 + bonus; money += gain; pl.hp = Math.min(100, pl.hp + 25); mission.n++;
    toast('Ziel erreicht!  +$' + gain);
    newMission();
  }

  updateCamera(dt);
  updateWorldLight();
  updateHud();
}

// ---------- Kamera ----------
let camH = 0, orbit = 0;
function updateCamera(dt) {
  shake = Math.max(0, shake - dt * 2);
  if (!started) {
    orbit += dt * 0.15;
    camera.position.set(pl.x + Math.sin(orbit) * 16, 6, pl.z + Math.cos(orbit) * 16);
    camera.lookAt(pl.x, 1.5, pl.z); return;
  }
  camH += angDiff(camH, pl.h) * (1 - Math.exp(-dt * 4));
  const back = 11 + Math.abs(pl.speed) * 0.06;
  const tx = pl.x - Math.sin(camH) * back, tz = pl.z - Math.cos(camH) * back;
  const f = 1 - Math.exp(-dt * 8);
  camera.position.x += (tx - camera.position.x) * f;
  camera.position.z += (tz - camera.position.z) * f;
  camera.position.y += (4.8 - camera.position.y) * f;
  camera.position.x += (Math.random() - 0.5) * shake; camera.position.y += (Math.random() - 0.5) * shake;
  camera.lookAt(pl.x + Math.sin(pl.h) * 5, 1.6, pl.z + Math.cos(pl.h) * 5);
  const fov = 62 + Math.abs(pl.speed) * 0.35;
  if (Math.abs(camera.fov - fov) > 0.05) { camera.fov = fov; camera.updateProjectionMatrix(); }
}

// ---------- Licht / Tageszeit ----------
const cDay = new THREE.Color(0x8fc3ee), cNight = new THREE.Color(0x070b1f), cDusk = new THREE.Color(0xff8a50), cTmp = new THREE.Color();
function updateWorldLight() {
  const a = (hour - 6) / 24 * Math.PI * 2, elev = Math.sin(a);
  const day = clamp((elev + 0.15) / 0.45, 0, 1), night = 1 - day;
  cTmp.copy(cNight).lerp(cDay, day);
  const dusk = clamp(1 - Math.abs(elev - 0.07) / 0.22, 0, 1) * 0.7;
  cTmp.lerp(cDusk, dusk);
  scene.background.copy(cTmp); scene.fog.color.copy(cTmp);
  scene.fog.near = lerp(60, 200, day); scene.fog.far = lerp(550, 1000, day);
  sun.intensity = Math.max(0, elev) * 1.1;
  sun.color.setHex(dusk > 0.3 ? 0xffb070 : 0xfff2d0);
  hemi.intensity = 0.22 + 0.6 * day;
  sun.target.position.set(pl.x, 0, pl.z);
  sun.position.set(pl.x + Math.cos(a) * 160, Math.max(elev, 0.12) * 160, pl.z + 60);
  for (const m of winMats) m.emissiveIntensity = night * 0.95;
  spot.intensity = night * 2.2;
}

// ---------- HUD ----------
const mapCv = $('map'), mg = mapCv.getContext('2d');
const starsEl = $('stars');
for (let i = 0; i < 5; i++) starsEl.appendChild(Object.assign(document.createElement('span'), { textContent: '★' }));
function updateHud() {
  $('money').textContent = '$ ' + Math.floor(money).toLocaleString('de-DE');
  const w = Math.ceil(heat - 0.001);
  [...starsEl.children].forEach((s, i) => s.classList.toggle('on', i < w));
  $('speed').textContent = Math.round(Math.abs(pl.speed) * 3);
  const hpf = $('hpfill'); hpf.style.width = pl.hp + '%'; hpf.style.background = pl.hp > 50 ? '#4ade80' : pl.hp > 25 ? '#facc15' : '#ef4444';
  const hh = Math.floor(hour), mm = Math.floor((hour % 1) * 60);
  $('clock').textContent = String(hh).padStart(2, '0') + ':' + String(mm).padStart(2, '0');
  const d = Math.round(Math.hypot(pl.x - mission.x, pl.z - mission.z));
  $('mission').textContent = 'Ziel: ' + d + ' m · ' + (mission.t > 0 ? Math.ceil(mission.t) + ' s' : 'ohne Bonus');
  drawMap();
}
function drawMap() {
  const S = 180, c = S / 2, RM = 140, sc = c / RM;
  mg.clearRect(0, 0, S, S);
  mg.save();
  mg.beginPath(); mg.arc(c, c, c, 0, Math.PI * 2); mg.clip();
  mg.fillStyle = '#1b2530'; mg.fillRect(0, 0, S, S);
  mg.translate(c, c); mg.rotate(pl.h - Math.PI); mg.scale(sc, sc); mg.translate(-pl.x, -pl.z);
  for (const b of blocks) {
    if (Math.abs(b.cx - pl.x) > RM + 40 || Math.abs(b.cz - pl.z) > RM + 40) continue;
    mg.fillStyle = b.park ? '#2f5a3a' : '#33444f';
    mg.fillRect(b.cx - 32, b.cz - 32, 64, 64);
  }
  for (const t of traffic) { mg.fillStyle = '#8895a0'; mg.fillRect(t.mesh.position.x - 2, t.mesh.position.z - 2, 4, 4); }
  const blink = Math.floor(gtime * 4) % 2;
  for (const p of police) {
    mg.fillStyle = blink ? '#ff3030' : '#3060ff';
    mg.beginPath(); mg.arc(p.mesh.position.x, p.mesh.position.z, 4.5, 0, Math.PI * 2); mg.fill();
  }
  let dx = mission.x - pl.x, dz = mission.z - pl.z; const dd = Math.hypot(dx, dz);
  if (dd > RM * 0.88) { dx *= RM * 0.88 / dd; dz *= RM * 0.88 / dd; }
  mg.fillStyle = '#ffd24a'; mg.strokeStyle = '#000'; mg.lineWidth = 1.5;
  mg.beginPath(); mg.arc(pl.x + dx, pl.z + dz, 6, 0, Math.PI * 2); mg.fill(); mg.stroke();
  mg.restore();
  mg.fillStyle = '#fff'; mg.strokeStyle = '#000'; mg.lineWidth = 1.5;
  mg.beginPath(); mg.moveTo(c, c - 8); mg.lineTo(c - 5, c + 6); mg.lineTo(c + 5, c + 6); mg.closePath(); mg.fill(); mg.stroke();
}

// ---------- Start / Loop ----------
$('play').addEventListener('click', () => {
  started = true; $('start').style.display = 'none';
  camH = pl.h; initAudio();
  if (audio && audio.ac.state === 'suspended') audio.ac.resume();
  toast('Fahre zum gelben Marker!', 3000);
});

let last = performance.now();
function frame(now) {
  const dt = Math.min(0.05, (now - last) / 1000); last = now;
  update(dt);
  renderer.render(scene, camera);
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
