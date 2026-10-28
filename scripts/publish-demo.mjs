#!/usr/bin/env node
/**
 * Demo publisher: pushes sample bay counts (and optional screen switches)
 * to the local Mosquitto broker so the kiosk UI can be verified.
 *
 * Usage:
 *   node scripts/publish-demo.mjs
 *   node scripts/publish-demo.mjs --once
 *   node scripts/publish-demo.mjs --screen welcome
 *   node scripts/publish-demo.mjs --url mqtt://localhost:1883
 */

import mqtt from "mqtt";

const args = parseArgs(process.argv.slice(2));
const url = args.url || process.env.MQTT_URL || "mqtt://localhost:1883";
const siteId = args.site || "pgs-demo";
const levelId = args.level || "B1";
const displayId = args.display || "pillar-b1-east";
const availabilityTopic = `pgs/${siteId}/levels/${levelId}/availability`;
const screenTopic = `pgs/${siteId}/displays/${displayId}/screen`;
const once = Boolean(args.once);
const intervalMs = Number(args.interval || 2500);

const state = {
  general: 124,
  disabled: 8,
  ev: 2,
  wealth_and_investment: 12,
};

const client = mqtt.connect(url, {
  clientId: `pgs-demo-publisher-${Math.random().toString(16).slice(2, 8)}`,
  clean: true,
});

client.on("connect", () => {
  console.log(`Connected to ${url}`);

  if (args.screen) {
    const payload = JSON.stringify({ screen: args.screen, ts: new Date().toISOString() });
    client.publish(screenTopic, payload, { qos: 1, retain: true }, (err) => {
      if (err) console.error(err);
      else console.log(`Published screen → ${screenTopic}: ${payload}`);
      if (once && !args.counts) shutdown(0);
    });
  }

  publishCounts();

  if (once) {
    setTimeout(() => shutdown(0), 400);
    return;
  }

  console.log(`Publishing to ${availabilityTopic} every ${intervalMs}ms (Ctrl+C to stop)`);
  setInterval(tick, intervalMs);
});

client.on("error", (err) => {
  console.error("MQTT error:", err.message || err);
});

function tick() {
  // Nudge counts so the UI visibly updates
  state.general = wander(state.general, 80, 180, 3);
  state.disabled = wander(state.disabled, 0, 20, 1);
  state.ev = wander(state.ev, 0, 12, 1);
  state.wealth_and_investment = wander(state.wealth_and_investment, 0, 30, 1);
  publishCounts();
}

function publishCounts() {
  const payload = JSON.stringify({
    levelId,
    ts: new Date().toISOString(),
    ...state,
  });
  client.publish(availabilityTopic, payload, { qos: 1, retain: true }, (err) => {
    if (err) console.error(err);
    else console.log(`→ ${payload}`);
  });
}

function wander(value, min, max, step) {
  const delta = Math.floor(Math.random() * (step * 2 + 1)) - step;
  return Math.min(max, Math.max(min, value + delta));
}

function shutdown(code) {
  client.end(false, {}, () => process.exit(code));
}

function parseArgs(argv) {
  const out = {};
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--once") out.once = true;
    else if (a === "--url") out.url = argv[++i];
    else if (a === "--site") out.site = argv[++i];
    else if (a === "--level") out.level = argv[++i];
    else if (a === "--display") out.display = argv[++i];
    else if (a === "--screen") out.screen = argv[++i];
    else if (a === "--interval") out.interval = argv[++i];
    else if (a === "--help" || a === "-h") {
      console.log(`Usage: node scripts/publish-demo.mjs [--url mqtt://localhost:1883] [--once] [--screen bay-counts|welcome|info]`);
      process.exit(0);
    }
  }
  return out;
}

process.on("SIGINT", () => shutdown(0));
process.on("SIGTERM", () => shutdown(0));
