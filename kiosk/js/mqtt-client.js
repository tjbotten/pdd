/**
 * Thin MQTT-over-WebSockets wrapper around mqtt.js (global `mqtt`).
 */

export function connectMqtt(config, handlers = {}) {
  const brokerUrl = config.broker?.url;
  if (!brokerUrl) {
    throw new Error("Missing broker.url in display config");
  }
  if (typeof mqtt === "undefined") {
    throw new Error("mqtt.js failed to load");
  }

  const clientId = `${config.broker.clientIdPrefix || "pgs-kiosk"}-${config.displayId || "anon"}-${Math.random().toString(16).slice(2, 8)}`;

  const options = {
    clientId,
    clean: true,
    reconnectPeriod: 3000,
    connectTimeout: 8000,
    protocolVersion: 4,
  };

  if (config.broker.username) {
    options.username = config.broker.username;
    options.password = config.broker.password || "";
  }

  const client = mqtt.connect(brokerUrl, options);
  let intentionalClose = false;

  client.on("connect", () => {
    handlers.onStatus?.(true);
    const topics = [
      config.topics?.availability,
      config.topics?.screen,
    ].filter(Boolean);
    for (const topic of topics) {
      client.subscribe(topic, { qos: 1 }, (err) => {
        if (err) console.warn("Subscribe failed", topic, err);
      });
    }
  });

  client.on("reconnect", () => handlers.onStatus?.(false));
  client.on("close", () => {
    if (!intentionalClose) handlers.onStatus?.(false);
  });
  client.on("offline", () => handlers.onStatus?.(false));
  client.on("error", (err) => {
    console.warn("MQTT error", err?.message || err);
    handlers.onStatus?.(false);
  });

  client.on("message", (topic, payload) => {
    const text = payload.toString();
    let data = text;
    try {
      data = JSON.parse(text);
    } catch {
      // keep raw string
    }
    handlers.onMessage?.(topic, data, text);
  });

  return {
    client,
    disconnect() {
      intentionalClose = true;
      client.end(true);
    },
  };
}
