#pragma once

// Setup-AP password shown here must match AP_PASSWORD in include/config.h.
static const char PAGE[] = R"page(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Zone sniffer</title>
<style>
  :root { color-scheme: dark; --bg:#121417; --card:#1c2128; --line:#313842; --text:#e8e2d6; --muted:#a39b8c; --ok:#8fce6a; --warn:#e0b15a; --bad:#e07a6a; --tx:#e0b15a; --rx:#8eb4e8; }
  * { box-sizing: border-box; }
  body { margin:0; font:16px/1.45 ui-sans-serif, system-ui, sans-serif; background:var(--bg); color:var(--text); }
  main { max-width:920px; margin:0 auto; padding:20px 16px 48px; }
  h1 { font-size:1.35rem; margin:0 0 4px; }
  h2 { font-size:1rem; margin:0 0 10px; }
  p { margin:8px 0; }
  a { color:var(--rx); }
  .sub { color:var(--muted); margin:0; }
  .banner { background:#3a2f14; border:1px solid var(--warn); padding:10px 12px; border-radius:8px; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(180px,1fr)); gap:8px; margin:14px 0; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:8px; padding:10px 12px; }
  .card span { display:block; color:var(--muted); font-size:.78rem; letter-spacing:.03em; text-transform:uppercase; }
  .card strong { font-size:1.05rem; word-break:break-word; }
  .role-tx { color:var(--tx); }
  .role-rx { color:var(--rx); }
  .bad { color:var(--bad); }
  .row { display:flex; flex-wrap:wrap; gap:8px; align-items:center; margin:10px 0; }
  button, .btn { background:#2a313b; color:var(--text); border:1px solid var(--line); border-radius:8px; padding:8px 12px; font:inherit; text-decoration:none; cursor:pointer; }
  button.primary { background:#3d4f32; border-color:#5d7a4c; }
  button.danger { background:#4a3030; border-color:#8a5048; }
  pre { background:#0c0e11; border:1px solid var(--line); border-radius:8px; min-height:220px; max-height:52vh; overflow:auto; padding:10px; font:13px/1.4 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; white-space:pre; }
  details { margin-top:18px; background:var(--card); border:1px solid var(--line); border-radius:8px; padding:10px 12px; }
  form { display:grid; gap:8px; margin:12px 0 4px; }
  label { display:grid; gap:4px; color:var(--muted); font-size:.9rem; }
  input, select { font:inherit; color:var(--text); background:#0c0e11; border:1px solid var(--line); border-radius:6px; padding:8px; }
  .split { display:grid; grid-template-columns:repeat(auto-fit,minmax(140px,1fr)); gap:8px; }
  .check { display:flex; align-items:center; gap:8px; color:var(--text); }
  #msg { min-height:1.2em; color:var(--warn); }
  footer { color:var(--muted); font-size:.85rem; margin-top:18px; }
</style>
</head>
<body>
<main>
  <h1>Zone sniffer</h1>
  <p class="sub" id="roleline">…</p>
  <p class="banner" id="banner">Choose TX or RX for this board. Both sniffers run this same firmware; the role is the only difference. Defaults are zone-tx and zone-rx.</p>
  <section class="grid" id="stats"></section>
  <p id="net" class="sub"></p>
  <div class="row">
    <button class="primary" id="start" type="button">Start recording</button>
    <button id="stop" type="button">Stop</button>
    <button class="danger" id="clear" type="button">Clear</button>
    <a class="btn" id="download" href="/download">Download recording</a>
  </div>
  <p class="sub" id="limits"></p>
  <pre id="live">Waiting for bytes.</pre>
  <details id="settings">
    <summary>Role, Wi-Fi, and UART</summary>
    <form id="device">
      <h2>This board</h2>
      <div class="split">
        <label>Role
          <select name="role" id="role">
            <option value="">Choose…</option>
            <option value="tx">TX side</option>
            <option value="rx">RX side</option>
          </select>
        </label>
        <label>Hostname
          <input name="hostname" id="hostname" maxlength="31" autocomplete="off" spellcheck="false" placeholder="zone-tx">
        </label>
      </div>
      <button type="submit">Save role</button>
    </form>
    <form id="wifi">
      <h2>Home Wi-Fi</h2>
      <p class="sub">Saved on the board. Nothing is built into the firmware. Leave the password blank to keep the one already stored. If joining fails, this setup AP comes back.</p>
      <label>Network name
        <input name="ssid" id="ssid" maxlength="32" autocomplete="off">
      </label>
      <label>Password
        <input name="password" id="password" type="password" maxlength="64" autocomplete="off">
      </label>
      <label class="check"><input name="open" id="open" type="checkbox"> Open network, no password</label>
      <div class="row">
        <button type="submit">Save Wi-Fi</button>
        <button type="button" id="forget">Forget Wi-Fi</button>
      </div>
    </form>
    <form id="uart">
      <h2>Serial</h2>
      <p class="sub">Listen-only. Default 9600 8N1. A new line is started after a 20 ms idle gap, a newline, or 16 bytes.</p>
      <div class="split">
        <label>Baud
          <input name="baud" id="baud" type="number" min="300" max="1000000" list="bauds">
        </label>
        <label>Data bits
          <select name="data_bits" id="data_bits">
            <option>5</option><option>6</option><option>7</option><option selected>8</option>
          </select>
        </label>
        <label>Parity
          <select name="parity" id="parity">
            <option value="N">None</option>
            <option value="E">Even</option>
            <option value="O">Odd</option>
          </select>
        </label>
        <label>Stop bits
          <select name="stop" id="stop">
            <option value="1">1</option>
            <option value="1.5">1.5</option>
            <option value="2">2</option>
          </select>
        </label>
      </div>
      <datalist id="bauds">
        <option value="1200"></option><option value="2400"></option><option value="4800"></option>
        <option value="9600"></option><option value="19200"></option><option value="38400"></option>
        <option value="57600"></option><option value="115200"></option>
      </datalist>
      <button type="submit">Apply UART</button>
    </form>
  </details>
  <p id="msg"></p>
  <footer>Wiring: adapter TTL TX → GPIO20. Do not connect GPIO21, and do not connect the adapter TTL RX. The blue LED blinks fast while recording, slowly in setup-AP mode, and blips when joined to Wi-Fi.</footer>
</main>
<script>
const live = document.getElementById('live');
const msg = document.getElementById('msg');
let cursor = 0;
let formReady = false;
let liveStarted = false;

function kib(n) {
  n = Number(n) || 0;
  if (n >= 1048576) return (n / 1048576).toFixed(2) + ' MiB';
  if (n >= 1024) return (n / 1024).toFixed(1) + ' KiB';
  return n + ' B';
}

function card(label, value, cls) {
  return '<div class="card"><span>' + label + '</span><strong class="' + (cls || '') + '">' + value + '</strong></div>';
}

function paint(s) {
  const role = s.role ? s.role.toUpperCase() : 'UNSET';
  document.getElementById('roleline').textContent = role + ' · ' + s.hostname + ' · ' + s.mac;
  document.getElementById('banner').hidden = !!s.role;
  const roleCls = s.role === 'tx' ? 'role-tx' : (s.role === 'rx' ? 'role-rx' : '');
  const ovfCls = Number(s.uart_overflows) ? 'bad' : '';
  document.getElementById('stats').innerHTML =
    card('Link', s.mode, '') +
    card('Role', role, roleCls) +
    card('UART', s.uart, '') +
    card('Captured', s.bytes_captured + ' bytes', '') +
    card('Recorded', s.bytes_recorded + ' bytes', '') +
    card('Recording', s.recording ? 'on' : 'off', s.recording ? 'role-tx' : '') +
    card('NTP', s.ntp ? 'synced (UTC)' : 'boot ms only', '') +
    card('UART overflows', String(s.uart_overflows), ovfCls);
  let net = '';
  if (s.sta_connected) {
    net = 'Joined ' + s.ssid + ' at http://' + s.ip + '/ (' + s.rssi + ' dBm). Also http://' + s.hostname + '.local/';
    if (s.ap_on) net += '. Setup AP ' + s.ap_ssid + ' is up at http://' + s.ap_ip + '/';
  } else if (s.joining) {
    net = 'Joining ' + (s.ssid || 'the saved network') + '. If this fails, the setup AP ' + s.ap_ssid + ' (password ' + s.ap_password + ') returns at http://192.168.4.1/';
  } else {
    net = 'Setup AP ' + s.ap_ssid + ', password ' + s.ap_password + '. Open http://192.168.4.1/';
  }
  document.getElementById('net').textContent = net;
  const flashNote = s.flash_ok ? (kib(s.flash_used) + ' of ' + kib(s.flash_limit) + ' flash') : 'flash unavailable';
  let limits = 'Live view keeps the latest ' + kib(s.live_limit) + ' of text. Recording keeps ' + kib(s.ram_used) + ' of ' + kib(s.ram_limit) + ' in RAM, then a flash ring (' + flashNote + '). When that ring is full the oldest recording is discarded.';
  if (Number(s.segments_discarded)) limits += ' Oldest chunks discarded: ' + s.segments_discarded + '.';
  if (Number(s.record_dropped_bytes)) limits += ' Bytes not stored: ' + s.record_dropped_bytes + '.';
  document.getElementById('limits').textContent = limits;
  if (!formReady) {
    document.getElementById('role').value = s.role || '';
    document.getElementById('hostname').value = s.hostname || '';
    document.getElementById('ssid').value = s.ssid || '';
    document.getElementById('baud').value = s.baud;
    document.getElementById('data_bits').value = String(s.data_bits);
    document.getElementById('parity').value = s.parity;
    document.getElementById('stop').value = s.stop;
    document.getElementById('open').checked = !!s.ssid && !s.has_password;
    formReady = true;
    if (!s.role || !s.sta_connected) document.getElementById('settings').open = true;
  }
}

async function post(url, fields) {
  const body = new URLSearchParams(fields);
  const res = await fetch(url, {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body, cache:'no-store'});
  let js = {};
  try { js = await res.json(); } catch (e) {}
  if (!res.ok || js.ok === false) throw new Error(js.error || ('Request failed (' + res.status + ')'));
  return js;
}

async function pollStatus() {
  const res = await fetch('/api/status', {cache:'no-store'});
  if (!res.ok) throw new Error('status ' + res.status);
  paint(await res.json());
}

async function pollLive() {
  const res = await fetch('/api/live?cursor=' + cursor, {cache:'no-store'});
  if (!res.ok) throw new Error('live ' + res.status);
  const next = parseInt(res.headers.get('X-Cursor') || String(cursor), 10);
  const truncated = res.headers.get('X-Truncated') === '1';
  const text = await res.text();
  if (!liveStarted && !text) return;
  const stick = live.scrollHeight - live.scrollTop - live.clientHeight < 48;
  if (truncated || !liveStarted) {
    live.textContent = text || 'Waiting for bytes.';
    liveStarted = true;
  } else if (text) {
    live.textContent += text;
    if (live.textContent.length > 40000) live.textContent = live.textContent.slice(-30000);
  }
  cursor = Number.isFinite(next) ? next : cursor;
  if (stick) live.scrollTop = live.scrollHeight;
}

document.getElementById('role').addEventListener('change', () => {
  const role = document.getElementById('role').value;
  const host = document.getElementById('hostname');
  const cur = host.value.trim();
  if (!cur || cur === 'zone-tx' || cur === 'zone-rx' || cur === 'zone-sniffer') {
    host.value = role === 'rx' ? 'zone-rx' : (role === 'tx' ? 'zone-tx' : cur);
  }
});

document.getElementById('start').onclick = async () => {
  msg.textContent = '';
  try { await post('/api/recording', {state:'start'}); } catch (e) { msg.textContent = e.message; }
};
document.getElementById('stop').onclick = async () => {
  msg.textContent = '';
  try { await post('/api/recording', {state:'stop'}); } catch (e) { msg.textContent = e.message; }
};
document.getElementById('clear').onclick = async () => {
  if (!confirm('Clear the live view and the recording on this board?')) return;
  msg.textContent = '';
  try {
    await post('/api/clear', {});
    live.textContent = 'Waiting for bytes.';
    liveStarted = false;
    cursor = 0;
  } catch (e) { msg.textContent = e.message; }
};
document.getElementById('device').onsubmit = async (ev) => {
  ev.preventDefault();
  const fd = new FormData(ev.target);
  msg.textContent = 'Saving…';
  try {
    await post('/api/device', {role: fd.get('role') || '', hostname: fd.get('hostname') || ''});
    formReady = false;
    msg.textContent = 'Role saved.';
  } catch (e) { msg.textContent = e.message; }
};
document.getElementById('wifi').onsubmit = async (ev) => {
  ev.preventDefault();
  const fd = new FormData(ev.target);
  msg.textContent = 'Saving Wi-Fi. If this page stops responding, join the setup AP and open http://192.168.4.1/';
  try {
    await post('/api/wifi', {ssid: fd.get('ssid') || '', password: fd.get('password') || '', open: fd.get('open') ? '1' : ''});
    msg.textContent = 'Saved. The board is switching networks.';
  } catch (e) { msg.textContent = e.message; }
};
document.getElementById('forget').onclick = async () => {
  msg.textContent = 'Forgetting Wi-Fi. Rejoin the setup AP if the page drops.';
  try { await post('/api/wifi', {ssid:'', password:'', open:'1'}); msg.textContent = 'Wi-Fi forgotten. Setup AP is coming back.'; }
  catch (e) { msg.textContent = e.message; }
};
document.getElementById('uart').onsubmit = async (ev) => {
  ev.preventDefault();
  const fd = new FormData(ev.target);
  msg.textContent = 'Applying UART…';
  try {
    await post('/api/uart', {baud: fd.get('baud'), data_bits: fd.get('data_bits'), parity: fd.get('parity'), stop: fd.get('stop')});
    msg.textContent = 'UART updated.';
  } catch (e) { msg.textContent = e.message; }
};

async function tick() {
  try { await pollStatus(); await pollLive(); }
  catch (e) { document.getElementById('net').textContent = 'Link to the board dropped. Rejoin its Wi-Fi or the setup AP.'; }
}
tick();
setInterval(tick, 700);
</script>
</body>
</html>
)page";
