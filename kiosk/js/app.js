import { connectMqtt } from "./mqtt-client.js";
import { renderBayCounts, updateBayCounts } from "./screens/bay-counts.js";
import { renderStubScreen } from "./screens/stub-welcome.js";

const STORAGE_KEY = "pgs-display-last-counts";
const params = new URLSearchParams(window.location.search);
const configUrl = params.get("config") || "../config/display-b1.json";

if (params.has("cursor")) {
  document.body.classList.add("debug-cursor");
}

const screenRoot = document.getElementById("screen-root");
const offlineEl = document.getElementById("offline-indicator");

let config = null;
let counts = {};
let activeScreen = "bay-counts";
let mqttHandle = null;

async function main() {
  config = await loadConfig(configUrl);
  activeScreen = resolveInitialScreen(config);
  counts = loadLastCounts(config);
  renderActiveScreen();
  setOffline(true);

  try {
    mqttHandle = connectMqtt(config, {
      onStatus: (online) => setOffline(!online),
      onMessage: handleMessage,
    });
  } catch (err) {
    console.error(err);
    setOffline(true);
  }
}

function resolveInitialScreen(cfg) {
  const fromQuery = params.get("screen");
  if (fromQuery) return fromQuery;
  return cfg.screen || "bay-counts";
}

async function loadConfig(url) {
  const res = await fetch(url, { cache: "no-store" });
  if (!res.ok) throw new Error(`Failed to load config: ${url} (${res.status})`);
  return res.json();
}

function loadLastCounts(cfg) {
  const defaults = { ...(cfg.defaults || {}) };
  try {
    const raw = localStorage.getItem(storageKey(cfg));
    if (!raw) return defaults;
    const parsed = JSON.parse(raw);
    return { ...defaults, ...parsed };
  } catch {
    return defaults;
  }
}

function persistCounts(cfg, next) {
  try {
    localStorage.setItem(storageKey(cfg), JSON.stringify(next));
  } catch {
    // ignore quota / private mode
  }
}

function storageKey(cfg) {
  return `${STORAGE_KEY}:${cfg.displayId || cfg.levelId || "default"}`;
}

function setOffline(isOffline) {
  if (!offlineEl) return;
  offlineEl.hidden = !isOffline;
}

function handleMessage(topic, data) {
  if (!config) return;

  if (topic === config.topics?.availability) {
    applyAvailability(data);
    return;
  }

  if (topic === config.topics?.screen) {
    applyScreenCommand(data);
  }
}

function applyAvailability(data) {
  if (!data || typeof data !== "object" || Array.isArray(data)) {
    console.warn("Ignoring non-object availability payload", data);
    return;
  }

  const changed = new Set();
  const next = { ...counts };

  for (const bay of config.bayTypes || []) {
    if (Object.prototype.hasOwnProperty.call(data, bay.id)) {
      const n = Number(data[bay.id]);
      if (!Number.isFinite(n)) continue;
      const value = Math.max(0, Math.floor(n));
      if (next[bay.id] !== value) {
        next[bay.id] = value;
        changed.add(bay.id);
      }
    }
  }

  // Allow nested { counts: { ... } } for forward compatibility
  if (data.counts && typeof data.counts === "object") {
    for (const bay of config.bayTypes || []) {
      if (Object.prototype.hasOwnProperty.call(data.counts, bay.id)) {
        const n = Number(data.counts[bay.id]);
        if (!Number.isFinite(n)) continue;
        const value = Math.max(0, Math.floor(n));
        if (next[bay.id] !== value) {
          next[bay.id] = value;
          changed.add(bay.id);
        }
      }
    }
  }

  counts = next;
  persistCounts(config, counts);

  if (activeScreen === "bay-counts") {
    updateBayCounts(screenRoot, config, counts, changed);
  }
}

function applyScreenCommand(data) {
  let screen = null;
  if (typeof data === "string") {
    screen = data;
  } else if (data && typeof data === "object") {
    screen = data.screen || data.id || null;
  }
  if (!screen) return;
  switchScreen(screen);
}

function switchScreen(screenId) {
  if (screenId === activeScreen && screenRoot.childElementCount) {
    if (screenId === "bay-counts") {
      updateBayCounts(screenRoot, config, counts);
    }
    return;
  }
  activeScreen = screenId;
  renderActiveScreen();
}

function renderActiveScreen() {
  switch (activeScreen) {
    case "bay-counts":
      renderBayCounts(screenRoot, config, counts);
      break;
    case "welcome":
      renderStubScreen(screenRoot, {
        title: "Welcome",
        body: "Welcome / info layouts will be designed case by case. This stub proves screen switching via config or MQTT.",
      });
      break;
    case "info":
      renderStubScreen(screenRoot, {
        title: "Information",
        body: "Stub info screen. Publish to the display screen topic to switch back to bay-counts.",
      });
      break;
    default:
      renderStubScreen(screenRoot, {
        title: `Unknown screen: ${activeScreen}`,
        body: "Falling back stub. Use screen id bay-counts, welcome, or info.",
      });
  }
}

window.PGSDisplay = {
  switchScreen,
  getCounts: () => ({ ...counts }),
  getConfig: () => config,
};

main().catch((err) => {
  console.error(err);
  screenRoot.innerHTML = `<section class="stub-screen"><h1>Display error</h1><p>${String(err.message || err)}</p></section>`;
});
