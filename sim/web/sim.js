// SPDX-License-Identifier: GPL-3.0-or-later
// Arrocco simulator — the browser side: shows what arrocco-sim presents as an e-ink
// panel would show it, and turns the mouse/trackpad into the finger.
//
// The e-ink model, per refresh kind (times come from the host, which blocks for the
// same duration, so the app and the picture stay in step):
//   partial  only pixels that change are driven. Black->white leaves a grey residue
//            (ghost) that grows with every further partial on that pixel and stays
//            until a full refresh; white->black comes out slightly washed.
//   full     one black flash, then the new image; all ghosting is gone.
//   deep     several black/white flashes and a negative image; all ghosting is gone.
(() => {
  'use strict';

  const W = 800;
  const H = 480;
  const N = W * H;
  const STRIDE = W / 8;                 // bytes per row in the 1-bit buffer

  const PAPER = [0xd9, 0xd7, 0xcf];     // e-paper "white"
  const INK = [0x2b, 0x2b, 0x2b];       // e-paper "black"
  const GHOST_STEP = 14;                // luminance lost per black->white partial (of 255)
  const GHOST_MAX = 42;
  const BLACK_LIFT = 10;                // black written by a partial is not fully black
  const GRAIN = 3;                      // +/- luminance of the paper texture
  const PAD = 8;                        // head-room in the colour table for the grain

  const FULL_KEYS = [[0, 'from'], [0.38, 'black'], [0.52, 'black'], [1, 'new']];
  const DEEP_KEYS = [[0, 'from'], [0.11, 'black'], [0.25, 'white'], [0.39, 'black'],
                     [0.53, 'white'], [0.68, 'negative'], [0.8, 'negative'], [1, 'new']];

  const $ = (id) => document.getElementById(id);
  const canvas = $('panel');
  const ctx = canvas.getContext('2d', { alpha: false });
  const image = ctx.createImageData(W, H);
  const pixels = new Uint32Array(image.data.buffer);

  // ---- panel state ------------------------------------------------------------------
  let target = new Uint8Array(N).fill(1);   // the logical image, 1 = white
  const lum = new Uint8Array(N).fill(255);  // what is on the glass, 255 = clean paper
  const residue = new Uint8Array(N);        // ghost a pixel shows while it is white
  const BLACK = new Uint8Array(N);
  const WHITE = new Uint8Array(N).fill(255);

  const grainOn = new Int8Array(N);
  const grainOff = new Int8Array(N);
  {
    // Fixed seed: the paper texture must not crawl between frames or screenshots.
    let seed = 0x2545f491;
    for (let i = 0; i < N; i++) {
      seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5;
      grainOn[i] = ((seed >>> 0) % (2 * GRAIN + 1)) - GRAIN;
    }
  }

  const lut = new Uint32Array(256 + 2 * PAD);
  for (let i = 0; i < lut.length; i++) {
    const v = Math.min(255, Math.max(0, i - PAD)) / 255;
    const r = Math.round(INK[0] + (PAPER[0] - INK[0]) * v);
    const g = Math.round(INK[1] + (PAPER[1] - INK[1]) * v);
    const b = Math.round(INK[2] + (PAPER[2] - INK[2]) * v);
    lut[i] = (0xff000000 | (b << 16) | (g << 8) | r) >>> 0;   // little-endian RGBA
  }

  const prefs = { ghosting: true, grain: true, sound: true };
  try {
    Object.assign(prefs, JSON.parse(localStorage.getItem('arrocco-sim-prefs') || '{}'));
  } catch (_) { /* private mode: keep the defaults */ }
  const savePrefs = () => {
    try { localStorage.setItem('arrocco-sim-prefs', JSON.stringify(prefs)); } catch (_) { /* ignore */ }
  };

  function paintAll() {
    const grain = prefs.grain ? grainOn : grainOff;
    for (let i = 0; i < N; i++) pixels[i] = lut[lum[i] + grain[i] + PAD];
    ctx.putImageData(image, 0, 0);
  }

  function decodeBits(base64) {
    const raw = atob(base64);
    const bits = new Uint8Array(N);
    let i = 0;
    for (let byteIndex = 0; byteIndex < STRIDE * H; byteIndex++) {
      const byte = raw.charCodeAt(byteIndex);
      for (let bit = 7; bit >= 0; bit--) bits[i++] = (byte >> bit) & 1;
    }
    return bits;
  }

  const ease = (u) => (u < 0.5 ? 2 * u * u : 1 - 2 * (1 - u) * (1 - u));

  // ---- refresh animations ----------------------------------------------------------

  // Partial: work out which pixels change and where each one ends up.
  function planPartial(bits) {
    const changed = [];
    let minX = W, minY = H, maxX = -1, maxY = -1;
    for (let i = 0; i < N; i++) {
      if (bits[i] === target[i]) continue;
      changed.push(i);
      const x = i % W;
      const y = (i - x) / W;
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
    }
    const list = Uint32Array.from(changed);
    const from = new Uint8Array(list.length);
    const to = new Uint8Array(list.length);
    for (let k = 0; k < list.length; k++) {
      const i = list[k];
      from[k] = lum[i];
      if (bits[i]) {
        residue[i] = prefs.ghosting ? Math.min(GHOST_MAX, residue[i] + GHOST_STEP) : 0;
        to[k] = 255 - residue[i];
      } else {
        to[k] = prefs.ghosting ? BLACK_LIFT : 0;
      }
    }
    const grain = () => (prefs.grain ? grainOn : grainOff);
    return {
      step(u) {
        const s = ease(u);
        const g = grain();
        for (let k = 0; k < list.length; k++) {
          const i = list[k];
          const v = from[k] + (to[k] - from[k]) * s;
          lum[i] = v;
          pixels[i] = lut[(v | 0) + g[i] + PAD];
        }
        if (maxX >= 0) {
          ctx.putImageData(image, 0, 0, minX, minY, maxX - minX + 1, maxY - minY + 1);
        }
      },
      finish() {
        this.step(1);
        target = bits;
      },
    };
  }

  // Full and Deep: the whole glass runs through a list of key images.
  function planFlash(bits, keys) {
    const from = Uint8Array.from(lum);
    const clean = new Uint8Array(N);
    const negative = new Uint8Array(N);
    for (let i = 0; i < N; i++) {
      clean[i] = bits[i] ? 255 : 0;
      negative[i] = bits[i] ? 0 : 255;
    }
    const images = { from, black: BLACK, white: WHITE, negative, new: clean };
    return {
      step(u) {
        let k = 1;
        while (k < keys.length - 1 && u > keys[k][0]) k++;
        const [t0, nameA] = keys[k - 1];
        const [t1, nameB] = keys[k];
        const a = images[nameA];
        const b = images[nameB];
        const s = ease(Math.min(1, Math.max(0, (u - t0) / (t1 - t0))));
        if (a === b) {
          lum.set(a);
        } else {
          for (let i = 0; i < N; i++) lum[i] = a[i] + (b[i] - a[i]) * s;
        }
        paintAll();
      },
      finish() {
        lum.set(clean);
        residue.fill(0);
        target = bits;
        paintAll();
      },
    };
  }

  function plan(frame) {
    const bits = decodeBits(frame.data);
    if (frame.kind === 'partial') return planPartial(bits);
    return planFlash(bits, frame.kind === 'deep' ? DEEP_KEYS : FULL_KEYS);
  }

  // Put an image on the glass with no history at all (page load, reconnect).
  function showClean(frame) {
    target = decodeBits(frame.data);
    residue.fill(0);
    for (let i = 0; i < N; i++) lum[i] = target[i] ? 255 : 0;
    paintAll();
  }

  const pending = [];
  let running = null;      // { plan, start, duration }

  function setBusy(on) {
    canvas.classList.toggle('busy', on);
    $('busy').classList.toggle('on', on);
    $('busy').textContent = on ? 'refreshing' : 'idle';
  }

  function pump() {
    while (!running && pending.length) {
      const frame = pending.shift();
      const p = plan(frame);
      // The host is already blocking since the frame left it: end when it ends,
      // even if this page picked the frame up a little late.
      const remaining = frame.arrived + frame.block_ms - performance.now();
      // Behind (hidden tab, backlog) or latency "none": no animation, but the ghosting
      // bookkeeping in plan() has still happened, as it would on the glass.
      if (document.hidden || pending.length > 0 || remaining < 30) {
        p.finish();
        continue;
      }
      running = { plan: p, start: performance.now(), duration: remaining };
      setBusy(true);
      requestAnimationFrame(animate);
    }
    if (!running) setBusy(false);
  }

  function animate(now) {
    if (!running) return;
    const u = (now - running.start) / running.duration;
    if (u >= 1) {
      running.plan.finish();
      running = null;
      pump();
      return;
    }
    running.plan.step(Math.max(0, u));
    requestAnimationFrame(animate);
  }

  document.addEventListener('visibilitychange', () => {
    // A hidden tab gets no animation frames: finish now so frames cannot pile up.
    if (document.hidden && running) {
      running.plan.finish();
      running = null;
      pump();
    }
  });

  // ---- read-outs ---------------------------------------------------------------------

  let seenFrameId = -1;

  function showCounts(frame) {
    $('nPartial').textContent = frame.counts.partial;
    $('nFull').textContent = frame.counts.full;
    $('nDeep').textContent = frame.counts.deep;
    $('sinceFull').textContent = frame.since_full;
    let text = `${frame.kind} ${frame.block_ms} ms`;
    if (frame.block_ms !== frame.nominal_ms) text += ` (device ${frame.nominal_ms} ms)`;
    if (frame.power_on) text += ' incl. HV on';
    $('last').textContent = text;
  }

  function setStatus(text, bad) {
    $('status').textContent = text;
    $('status').classList.toggle('bad', Boolean(bad));
  }

  function showControls(controls) {
    if (!controls) return;
    const noGauge = controls.battery < 0;
    $('noGauge').checked = noGauge;
    $('battery').disabled = noGauge;
    if (!noGauge) $('battery').value = controls.battery;
    $('batteryValue').textContent = noGauge ? 'n/a' : `${controls.battery}%`;
    $('usb').checked = Boolean(controls.usb);
    let best = null;
    for (const radio of document.querySelectorAll('input[name="scale"]')) {
      if (best === null || Math.abs(radio.value - controls.scale) < Math.abs(best.value - controls.scale)) {
        best = radio;
      }
    }
    if (best) best.checked = true;
  }

  // ---- events from the server ---------------------------------------------------------

  let audio = null;

  function beep(hz, ms) {
    if (!prefs.sound || !audio || audio.state !== 'running') return;
    const osc = audio.createOscillator();
    const gain = audio.createGain();
    osc.type = 'square';              // a passive buzzer on a GPIO is a square wave
    osc.frequency.value = hz;
    gain.gain.value = 0.05;
    osc.connect(gain).connect(audio.destination);
    osc.start();
    osc.stop(audio.currentTime + ms / 1000);
  }

  function onEvent(ev) {
    switch (ev.ev) {
      case 'snapshot':
        showControls(ev.controls);
        $('hv').textContent = ev.panel_on ? 'on' : 'off';
        $('ignored').textContent = ev.ignored_touches;
        if (ev.frame && ev.frame.id !== seenFrameId) {
          // Joining mid-way: what the glass went through before is unknown, show it clean.
          pending.length = 0;
          running = null;
          seenFrameId = ev.frame.id;
          showClean(ev.frame);
          showCounts(ev.frame);
          setBusy(false);
        }
        if (ev.alive) {
          setStatus(`running: ${ev.hello ? ev.hello.app : '?'} app`);
        } else {
          setStatus(`app stopped (exit code ${ev.exit_code}) - press "Restart app"`, true);
        }
        break;
      case 'hello':
        setStatus(`running: ${ev.app} app`);
        $('ignored').textContent = '0';
        break;
      case 'frame':
        if (ev.resend || ev.id === seenFrameId) break;
        seenFrameId = ev.id;
        ev.arrived = performance.now();
        showCounts(ev);
        pending.push(ev);
        pump();
        break;
      case 'beep':
        beep(ev.hz, ev.ms);
        break;
      case 'panel':
        $('hv').textContent = ev.on ? 'on' : 'off';
        break;
      case 'touch_ignored':
        $('ignored').textContent = ev.count;
        break;
      case 'state':
        showControls(ev);
        break;
      case 'exit':
        setStatus(`app stopped (exit code ${ev.code}) - press "Restart app"`, true);
        break;
      case 'error':
        console.warn('arrocco-sim:', ev.msg, ev.detail);
        break;
      default:
        break;
    }
  }

  function connect() {
    const source = new EventSource('/events');
    source.onmessage = (message) => onEvent(JSON.parse(message.data));
    source.onerror = () => setStatus('no connection - is sim/run.sh still running?', true);
  }

  // ---- input to the server --------------------------------------------------------------

  const outbox = [];
  let sending = false;

  async function flushOutbox() {
    if (sending) return;
    sending = true;
    while (outbox.length) {
      const body = outbox.splice(0, outbox.length).join('\n');
      try {
        const response = await fetch('/input', { method: 'POST', body, headers: { 'Content-Type': 'text/plain' } });
        if (response.status === 409) setStatus('app stopped - press "Restart app"', true);
      } catch (_) {
        setStatus('no connection - is sim/run.sh still running?', true);
      }
    }
    sending = false;
  }

  function send(line) {
    outbox.push(line);
    flushOutbox();
  }

  // ---- the finger -------------------------------------------------------------------------

  const finger = $('finger');
  let fingerDown = false;
  let lastPoint = { x: 0, y: 0 };
  let lastMoveSent = 0;

  function pointOf(event) {
    const rect = canvas.getBoundingClientRect();
    return {
      x: Math.floor((event.clientX - rect.left) * W / rect.width),
      y: Math.floor((event.clientY - rect.top) * H / rect.height),
    };
  }
  const onGlass = (p) => p.x >= 0 && p.x < W && p.y >= 0 && p.y < H;

  function placeFinger(p) {
    finger.style.left = `${p.x}px`;
    finger.style.top = `${p.y}px`;
  }

  function liftFinger() {
    if (!fingerDown) return;
    fingerDown = false;
    finger.hidden = true;
    send(`touch up ${lastPoint.x} ${lastPoint.y}`);
  }

  canvas.addEventListener('pointerdown', (event) => {
    if (event.button !== 0 || fingerDown) return;
    event.preventDefault();
    if (prefs.sound) {
      // Browsers only let audio start from inside a user gesture.
      if (!audio) audio = new (window.AudioContext || window.webkitAudioContext)();
      if (audio.state === 'suspended') audio.resume();
    }
    canvas.setPointerCapture(event.pointerId);
    fingerDown = true;
    lastPoint = pointOf(event);
    lastMoveSent = performance.now();
    // The host decides what is ignored; this is only the visual hint.
    finger.classList.toggle('ignored', Boolean(running));
    finger.hidden = false;
    placeFinger(lastPoint);
    send(`touch down ${lastPoint.x} ${lastPoint.y}`);
  });

  canvas.addEventListener('pointermove', (event) => {
    if (!fingerDown) return;
    const p = pointOf(event);
    if (!onGlass(p)) {
      liftFinger();               // slid off the glass: the touch controller reports a release
      return;
    }
    lastPoint = p;
    placeFinger(p);
    const now = performance.now();
    if (now - lastMoveSent >= 16) {   // the GT911 samples at about 100 Hz at best
      lastMoveSent = now;
      send(`touch move ${p.x} ${p.y}`);
    }
  });

  canvas.addEventListener('pointerup', (event) => {
    const p = pointOf(event);
    if (onGlass(p)) lastPoint = p;
    liftFinger();
  });
  canvas.addEventListener('pointercancel', liftFinger);
  canvas.addEventListener('contextmenu', (event) => event.preventDefault());

  // ---- the control strip ---------------------------------------------------------------------

  for (const radio of document.querySelectorAll('input[name="scale"]')) {
    radio.addEventListener('change', () => { if (radio.checked) send(`set scale ${radio.value}`); });
  }

  $('ghosting').checked = prefs.ghosting;
  $('ghosting').addEventListener('change', (event) => {
    prefs.ghosting = event.target.checked;
    savePrefs();
    if (!prefs.ghosting && !running) {
      residue.fill(0);
      for (let i = 0; i < N; i++) lum[i] = target[i] ? 255 : 0;
      paintAll();
    }
  });

  $('grain').checked = prefs.grain;
  $('grain').addEventListener('change', (event) => {
    prefs.grain = event.target.checked;
    savePrefs();
    if (!running) paintAll();
  });

  $('sound').checked = prefs.sound;
  $('sound').addEventListener('change', (event) => {
    prefs.sound = event.target.checked;
    savePrefs();
  });

  $('battery').addEventListener('input', (event) => {
    $('batteryValue').textContent = `${event.target.value}%`;
  });
  $('battery').addEventListener('change', (event) => send(`set battery ${event.target.value}`));
  $('noGauge').addEventListener('change', (event) => {
    send(`set battery ${event.target.checked ? -1 : $('battery').value}`);
  });
  $('usb').addEventListener('change', (event) => send(`set usb ${event.target.checked ? 1 : 0}`));

  $('screenshot').addEventListener('click', () => {
    canvas.toBlob((blob) => {
      if (!blob) return;
      const stamp = new Date().toISOString().replace(/[-:]/g, '').replace('T', '-').slice(0, 15);
      const link = document.createElement('a');
      link.href = URL.createObjectURL(blob);
      link.download = `arrocco-${stamp}.png`;
      link.click();
      setTimeout(() => URL.revokeObjectURL(link.href), 1000);
    }, 'image/png');
  });

  $('restart').addEventListener('click', () => {
    fetch('/restart', { method: 'POST' }).catch(() => setStatus('no connection - is sim/run.sh still running?', true));
  });

  paintAll();
  connect();
})();
