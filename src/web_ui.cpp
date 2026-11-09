#include "web_ui.h"

#include "capture.h"
#include "config.h"
#include "line_format.h"
#include "settings.h"
#include "sniffer.h"
#include "web_page.h"
#include "wifi_mgr.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <ctime>
#include <cstring>

static WebServer server(80);
static const char* kHeaders[] = {"Host"};

static void json_escape(const char* in, char* out, size_t out_n) {
  size_t j = 0;
  if (out_n == 0) {
    return;
  }
  for (size_t i = 0; in != nullptr && in[i] != '\0' && j + 2 < out_n; i++) {
    unsigned char c = static_cast<unsigned char>(in[i]);
    if (c == '"' || c == '\\') {
      if (j + 3 >= out_n) {
        break;
      }
      out[j++] = '\\';
      out[j++] = static_cast<char>(c);
    } else if (c < 0x20) {
      continue;
    } else {
      out[j++] = static_cast<char>(c);
    }
  }
  out[j] = '\0';
}

static void send_error(const char* message) {
  char buf[160];
  snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", message);
  server.send(400, "application/json", buf);
}

static void send_ok() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"ok\":true}");
}

static bool host_is_ours(const String& host_in) {
  String host = host_in;
  host.toLowerCase();
  int colon = host.indexOf(':');
  if (colon >= 0) {
    host.remove(colon);
  }
  if (host == "192.168.4.1" || host == "zone-sniffer.local") {
    return true;
  }
  char name[32];
  settings_effective_hostname(name, sizeof(name));
  String mdns = String(name) + ".local";
  mdns.toLowerCase();
  if (host == mdns) {
    return true;
  }
  if (WiFi.status() == WL_CONNECTED && host == WiFi.localIP().toString()) {
    return true;
  }
  return false;
}

static void redirect_ui() {
  String loc = "http://";
  if (wifi_sta_connected() && !wifi_portal_active()) {
    loc += WiFi.localIP().toString();
  } else {
    loc += "192.168.4.1";
  }
  loc += "/";
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Location", loc, true);
  String body = "<html><body><a href=\"" + loc + "\">zone sniffer</a></body></html>";
  server.send(302, "text/html", body);
}

static void handle_root() {
  if (wifi_portal_active() && (!server.hasHeader("Host") || !host_is_ours(server.header("Host")))) {
    redirect_ui();
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", PAGE);
}

static const char* stop_text(uint8_t stop_code) {
  if (stop_code == 2) {
    return "2";
  }
  if (stop_code == 15) {
    return "1.5";
  }
  return "1";
}

static void handle_status() {
  DeviceSettings settings;
  settings_get(&settings);
  CaptureStatus cap;
  capture_status(&cap);
  WifiStatus wifi;
  wifi_fill_status(&wifi);
  char host[32];
  settings_effective_hostname(host, sizeof(host));
  char uart[32];
  format_uart_label(uart, sizeof(uart), settings.uart.baud, settings.uart.data_bits, settings.uart.parity,
                    settings.uart.stop_code);
  char ssid[80];
  char ap_ssid[48];
  json_escape(wifi.ssid, ssid, sizeof(ssid));
  json_escape(wifi.ap_ssid, ap_ssid, sizeof(ap_ssid));

  char body[1400];
  snprintf(body, sizeof(body),
           "{\"role\":\"%s\",\"hostname\":\"%s\",\"mac\":\"%s\",\"mode\":\"%s\",\"joining\":%s,"
           "\"sta_connected\":%s,\"ssid\":\"%s\",\"has_password\":%s,\"ip\":\"%s\",\"rssi\":%d,"
           "\"ap_on\":%s,\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\",\"ap_password\":\"%s\","
           "\"uart\":\"%s\",\"baud\":%lu,\"data_bits\":%u,\"parity\":\"%c\",\"stop\":\"%s\","
           "\"recording\":%s,\"bytes_captured\":%lu,\"bytes_recorded\":%lu,"
           "\"record_dropped_bytes\":%lu,\"uart_overflows\":%lu,\"segments_discarded\":%lu,"
           "\"spill_failures\":%lu,\"flash_used\":%u,\"flash_limit\":%u,\"ram_used\":%u,"
           "\"ram_limit\":%u,\"live_limit\":%u,\"flash_ok\":%s,\"ntp\":%s,\"uptime_ms\":%lu}",
           settings.role, host, wifi.mac, wifi.mode, wifi.joining ? "true" : "false",
           wifi.sta_connected ? "true" : "false", ssid, wifi.has_password ? "true" : "false", wifi.ip, wifi.rssi,
           wifi.ap_on ? "true" : "false", ap_ssid, wifi.ap_ip, AP_PASSWORD, uart,
           static_cast<unsigned long>(settings.uart.baud), settings.uart.data_bits, settings.uart.parity,
           stop_text(settings.uart.stop_code), cap.recording ? "true" : "false",
           static_cast<unsigned long>(cap.bytes_captured), static_cast<unsigned long>(cap.bytes_recorded),
           static_cast<unsigned long>(cap.record_dropped_bytes), static_cast<unsigned long>(cap.uart_overflows),
           static_cast<unsigned long>(cap.segments_discarded), static_cast<unsigned long>(cap.spill_failures),
           static_cast<unsigned>(cap.flash_used), static_cast<unsigned>(cap.flash_limit),
           static_cast<unsigned>(cap.ram_used), static_cast<unsigned>(cap.ram_limit),
           static_cast<unsigned>(cap.live_limit), cap.flash_ok ? "true" : "false",
           time(nullptr) > 1700000000 ? "true" : "false", static_cast<unsigned long>(millis()));
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

static void handle_live() {
  uint32_t since = 0;
  if (server.hasArg("cursor")) {
    since = static_cast<uint32_t>(strtoul(server.arg("cursor").c_str(), nullptr, 10));
  }
  static char chunk[4096];
  uint32_t cursor = 0;
  bool truncated = false;
  size_t n = capture_live_read(since, &cursor, &truncated, chunk, sizeof(chunk) - 1);
  chunk[n] = '\0';
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Cursor", String(cursor));
  server.sendHeader("X-Truncated", truncated ? "1" : "0");
  server.send(200, "text/plain; charset=utf-8", chunk);
}

static void handle_recording() {
  String state = server.arg("state");
  if (state == "start") {
    capture_set_recording(true);
  } else if (state == "stop") {
    capture_flush();
    capture_set_recording(false);
  } else {
    send_error("state must be start or stop");
    return;
  }
  send_ok();
}

static void handle_clear() {
  capture_clear();
  send_ok();
}

static void handle_device() {
  if (!settings_set_device(server.arg("role").c_str(), server.arg("hostname").c_str())) {
    send_error("Role must be tx or rx, and the hostname must be letters, digits, and hyphens");
    return;
  }
  wifi_apply_identity();
  send_ok();
}

static void handle_wifi() {
  bool open_network = server.arg("open") == "1" || server.arg("open") == "on";
  bool password_provided = server.arg("password").length() > 0;
  if (!settings_set_wifi(server.arg("ssid").c_str(), server.arg("password").c_str(), password_provided, open_network)) {
    send_error("Wi-Fi name or password is too long");
    return;
  }
  send_ok();
  delay(250);
  wifi_reload();
}

static void handle_uart() {
  UartConfig uart;
  if (!settings_uart_from_strings(server.arg("baud").c_str(), server.arg("data_bits").c_str(),
                                  server.arg("parity").c_str(), server.arg("stop").c_str(), &uart) ||
      !settings_set_uart(&uart)) {
    send_error("Baud, data bits, parity, or stop bits were rejected");
    return;
  }
  sniffer_reconfigure();
  send_ok();
}

static bool write_all(Client& client, const uint8_t* data, size_t n) {
  size_t off = 0;
  while (off < n) {
    if (!client.connected()) {
      return false;
    }
    size_t chunk = n - off;
    if (chunk > 1024) {
      chunk = 1024;
    }
    size_t wrote = client.write(data + off, chunk);
    if (wrote == 0) {
      return false;
    }
    off += wrote;
    esp_task_wdt_reset();
    delay(0);
  }
  return true;
}

static size_t build_header(char* dst, size_t dst_n, bool ram_missing) {
  DeviceSettings settings;
  settings_get(&settings);
  CaptureStatus cap;
  capture_status(&cap);
  WifiStatus wifi;
  wifi_fill_status(&wifi);
  char host[32];
  settings_effective_hostname(host, sizeof(host));
  char uart[32];
  format_uart_label(uart, sizeof(uart), settings.uart.baud, settings.uart.data_bits, settings.uart.parity,
                    settings.uart.stop_code);
  char generated[40];
  char started[40];
  time_t now = time(nullptr);
  format_utc_ms(generated, sizeof(generated), now > 1700000000 ? static_cast<int64_t>(now) * 1000 : 0);
  format_utc_ms(started, sizeof(started), cap.last_start_unix_ms);

  int wrote = snprintf(
      dst, dst_n,
      "# zone-buffer serial capture\n"
      "# role: %s\n"
      "# hostname: %s\n"
      "# mac: %s\n"
      "# uart: %s\n"
      "# rx_gpio: %d\n"
      "# tx_gpio: not-driven (GPIO%d left disconnected)\n"
      "# gap_ms: %lu\n"
      "# line_bytes: %d\n"
      "# ntp: %s\n"
      "# generated_utc: %s\n"
      "# generated_boot_ms: %lu\n"
      "# last_start_boot_ms: %s%lu\n"
      "# last_start_utc: %s\n"
      "# bytes_captured: %lu\n"
      "# bytes_recorded: %lu\n"
      "# record_dropped_bytes: %lu\n"
      "# uart_overflows: %lu\n"
      "# segments_discarded: %lu\n"
      "# flash_limit_bytes: %u\n"
      "# recording_ram_bytes: %u\n"
      "# live_view_bytes: %u\n"
      "# columns: boot_ms<TAB>utc<TAB>hex<TAB>ascii\n"
      "# utc is '-' until NTP sets the clock. boot_ms is milliseconds since boot and wraps at 2^32.\n"
      "# bytes_recorded counts UART bytes accepted since Clear. Discarded ring space is not in the body.\n"
      "# Stop recording before downloading if you want a closed snapshot.\n"
      "%s"
      "#\n",
      settings.role[0] ? settings.role : "unset", host, wifi.mac, uart, PIN_UART_RX, PIN_UART_TX_UNCONNECTED,
      static_cast<unsigned long>(UART_GAP_MS), UART_LINE_BYTES, time(nullptr) > 1700000000 ? "synced" : "not-synced",
      generated, static_cast<unsigned long>(millis()), cap.last_start_valid ? "" : "unknown ",
      static_cast<unsigned long>(cap.last_start_ms), started, static_cast<unsigned long>(cap.bytes_captured),
      static_cast<unsigned long>(cap.bytes_recorded), static_cast<unsigned long>(cap.record_dropped_bytes),
      static_cast<unsigned long>(cap.uart_overflows), static_cast<unsigned long>(cap.segments_discarded),
      static_cast<unsigned>(cap.flash_limit), static_cast<unsigned>(cap.ram_limit),
      static_cast<unsigned>(cap.live_limit),
      ram_missing ? "# warning: RAM tail could not be copied into this file\n" : "");
  if (wrote < 0) {
    dst[0] = '\0';
    return 0;
  }
  if (static_cast<size_t>(wrote) >= dst_n) {
    return dst_n - 1;
  }
  return static_cast<size_t>(wrote);
}

static void handle_download() {
  capture_flush();
  CaptureSnapshot snap;
  if (!capture_snapshot(&snap)) {
    server.send(500, "text/plain", "snapshot failed");
    return;
  }
  static char header[2048];
  size_t header_len = build_header(header, sizeof(header), snap.ram_missing);

  size_t total = header_len + snap.ram_len;
  for (size_t i = 0; i < snap.segment_count; i++) {
    total += snap.segments[i].size;
  }

  char host[32];
  settings_effective_hostname(host, sizeof(host));
  char filename[48];
  snprintf(filename, sizeof(filename), "%s-capture.txt", host);

  // Headers come from WebServer so the status line matches this core.
  // The body is written next, and its length is the snapshot taken above.
  server.sendHeader("Content-Disposition", String("attachment; filename=\"") + filename + "\"");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(total);
  server.send(200, "text/plain; charset=utf-8", "");

  NetworkClient& client = server.client();
  client.setTimeout(15000);
  client.setNoDelay(true);
  bool ok = write_all(client, reinterpret_cast<const uint8_t*>(header), header_len);
  uint8_t buf[1024];
  for (size_t i = 0; ok && i < snap.segment_count; i++) {
    char path[32];
    capture_segment_path(path, sizeof(path), snap.segments[i].id);
    File file = LittleFS.open(path, "r");
    size_t left = snap.segments[i].size;
    if (!file) {
      ok = false;
      break;
    }
    while (ok && left > 0) {
      size_t want = left > sizeof(buf) ? sizeof(buf) : left;
      int got = file.read(buf, want);
      if (got <= 0) {
        ok = false;
        break;
      }
      ok = write_all(client, buf, static_cast<size_t>(got));
      left -= static_cast<size_t>(got);
    }
    file.close();
    capture_note_streamed(snap.segments[i].id);
    capture_flush();
  }
  if (ok && snap.ram != nullptr && snap.ram_len > 0) {
    ok = write_all(client, reinterpret_cast<const uint8_t*>(snap.ram), snap.ram_len);
  }
  if (!ok) {
    Serial.println("download ended early");
  }
  client.flush();
  capture_snapshot_release(&snap);
}

static void handle_probe() {
  redirect_ui();
}

static void handle_not_found() {
  if (!server.hasHeader("Host") || !host_is_ours(server.header("Host"))) {
    redirect_ui();
    return;
  }
  server.send(404, "text/plain", "not found");
}

void web_start() {
  server.collectHeaders(kHeaders, 1);
  server.on("/", HTTP_GET, handle_root);
  server.on("/api/status", HTTP_GET, handle_status);
  server.on("/api/live", HTTP_GET, handle_live);
  server.on("/api/recording", HTTP_POST, handle_recording);
  server.on("/api/clear", HTTP_POST, handle_clear);
  server.on("/api/device", HTTP_POST, handle_device);
  server.on("/api/wifi", HTTP_POST, handle_wifi);
  server.on("/api/uart", HTTP_POST, handle_uart);
  server.on("/download", HTTP_GET, handle_download);
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204, "text/plain", ""); });
  server.on("/generate_204", HTTP_ANY, handle_probe);
  server.on("/gen_204", HTTP_ANY, handle_probe);
  server.on("/hotspot-detect.html", HTTP_ANY, handle_probe);
  server.on("/library/test/success.html", HTTP_ANY, handle_probe);
  server.on("/connecttest.txt", HTTP_ANY, handle_probe);
  server.on("/ncsi.txt", HTTP_ANY, handle_probe);
  server.on("/fwlink", HTTP_ANY, handle_probe);
  server.on("/redirect", HTTP_ANY, handle_probe);
  server.on("/canonical.html", HTTP_ANY, handle_probe);
  server.onNotFound(handle_not_found);
  server.begin();
}

void web_loop() {
  server.handleClient();
}
