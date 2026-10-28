#!/usr/bin/env node
/**
 * Fallback local MQTT + WebSocket broker for VMs without Docker/Mosquitto.
 * Prefer `docker compose up -d` or `npm run broker:local` when available.
 *
 * Usage: node scripts/dev-broker.mjs
 */

import { createServer } from "node:http";
import { createServer as createNetServer } from "node:net";
import { Aedes } from "aedes";
import { createWebSocketStream, WebSocketServer } from "ws";

const mqttPort = Number(process.env.MQTT_PORT || 1883);
const wsPort = Number(process.env.WS_PORT || 9001);

const broker = await Aedes.createBroker();

const tcpServer = createNetServer(broker.handle);
tcpServer.listen(mqttPort, () => {
  console.log(`MQTT TCP  :${mqttPort}`);
});

const httpServer = createServer();
const wss = new WebSocketServer({ server: httpServer });

wss.on("connection", (ws, req) => {
  const stream = createWebSocketStream(ws);
  broker.handle(stream, req);
});

httpServer.listen(wsPort, () => {
  console.log(`MQTT WS   :${wsPort}  (ws://localhost:${wsPort})`);
});

broker.on("client", (client) => {
  console.log(`client connected: ${client.id}`);
});

broker.on("publish", (packet, client) => {
  if (!client || packet.topic.startsWith("$SYS")) return;
  console.log(`publish ${packet.topic} ← ${client.id}`);
});

function shutdown() {
  broker.close(() => {
    tcpServer.close();
    httpServer.close();
    process.exit(0);
  });
}

process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
