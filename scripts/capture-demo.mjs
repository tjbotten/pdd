#!/usr/bin/env node
/**
 * Capture portrait kiosk screenshots for demo evidence.
 */
import puppeteer from "puppeteer";
import { mkdir } from "node:fs/promises";
import path from "node:path";

const outDir = process.argv[2] || "/cursor/stores/self/media";
const base = process.env.KIOSK_URL || "http://127.0.0.1:8080/kiosk/?cursor=1";

await mkdir(outDir, { recursive: true });

const browser = await puppeteer.launch({
  headless: true,
  executablePath: process.env.CHROME_PATH || "/usr/local/bin/google-chrome",
  args: ["--no-sandbox", "--disable-setuid-sandbox", "--disable-dev-shm-usage"],
});

async function shot(name, url, waitMs = 2500) {
  const page = await browser.newPage();
  await page.setViewport({ width: 540, height: 960, deviceScaleFactor: 2 });
  await page.goto(url, { waitUntil: "networkidle2", timeout: 30000 });
  await page.waitForSelector(".bay-count, .stub-screen", { timeout: 15000 });
  await new Promise((r) => setTimeout(r, waitMs));
  const file = path.join(outDir, name);
  await page.screenshot({ path: file, fullPage: true, type: "png" });
  console.log("wrote", file);
  await page.close();
  return file;
}

try {
  await shot("kiosk-portrait-initial.png", base, 2000);
  await shot("kiosk-portrait-live-mqtt.png", base, 4500);
  await shot(
    "kiosk-portrait-offline.png",
    "http://127.0.0.1:8080/kiosk/?cursor=1&config=../config/display-b1-offline-demo.json",
    3500
  );
  await shot(
    "kiosk-stub-welcome.png",
    "http://127.0.0.1:8080/kiosk/?cursor=1&screen=welcome",
    1200
  );
} finally {
  await browser.close();
}
