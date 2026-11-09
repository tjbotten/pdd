#include "wifi_mgr.h"

#include "config.h"
#include "settings.h"

#include <ESPmDNS.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstring>

namespace {

enum class Phase { Boot, StaWait, ApOnly, Online };

Phase phase = Phase::Boot;
uint32_t phase_since = 0;
uint32_t next_sta_try = 0;
uint32_t disconnect_since = 0;
bool ap_on = false;
bool dns_on = false;
bool mdns_on = false;
bool reload_requested = false;
char ap_ssid[24] = "zone-sniff";
DNSServer dns;

const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kMask(255, 255, 255, 0);

void stop_dns() {
  if (dns_on) {
    dns.stop();
    dns_on = false;
  }
}

void stop_ap() {
  stop_dns();
  if (ap_on) {
    WiFi.softAPdisconnect(true);
    ap_on = false;
  }
}

void start_dns() {
  if (!dns_on) {
    dns.start(53, "*", kApIp);
    dns_on = true;
  }
}

bool bring_up_ap(int channel, bool with_dns) {
  if (channel < 1 || channel > 13) {
    channel = 6;
  }
  if (ap_on) {
    if (with_dns) {
      start_dns();
    } else {
      stop_dns();
    }
    return true;
  }
  char host[32];
  settings_effective_hostname(host, sizeof(host));
  WiFi.softAPConfig(kApIp, kApIp, kMask);
  WiFi.softAPsetHostname(host);
  bool ok = WiFi.softAP(ap_ssid, AP_PASSWORD, channel, 0, 4);
  ap_on = ok;
  if (ok && with_dns) {
    start_dns();
  } else {
    stop_dns();
  }
  Serial.printf("setup AP \"%s\" channel %d %s, password %s, http://192.168.4.1\n",
                ap_ssid, channel, ok ? "up" : "FAILED", AP_PASSWORD);
  return ok;
}

void start_mdns() {
  char host[32];
  settings_effective_hostname(host, sizeof(host));
  if (mdns_on) {
    MDNS.end();
    mdns_on = false;
  }
  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    mdns_on = true;
    Serial.printf("web UI http://%s.local\n", host);
  } else {
    Serial.println("mDNS start failed");
  }
}

void begin_sta() {
  DeviceSettings settings;
  settings_get(&settings);
  char host[32];
  settings_effective_hostname(host, sizeof(host));
  WiFi.setHostname(host);
  WiFi.setSleep(false);
  if (settings.password[0] == '\0') {
    WiFi.begin(settings.ssid);
  } else {
    WiFi.begin(settings.ssid, settings.password);
  }
  phase = Phase::StaWait;
  phase_since = millis();
  Serial.printf("joining \"%s\" as %s\n", settings.ssid, host);
}

void enter_ap_only() {
  stop_ap();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setSleep(false);
  bring_up_ap(6, true);
  start_mdns();
  phase = Phase::ApOnly;
  next_sta_try = millis() + 60000;
}

void enter_online() {
  phase = Phase::Online;
  disconnect_since = millis();
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  int channel = WiFi.channel();
  WiFi.mode(WIFI_AP_STA);
  if (ap_on) {
    WiFi.softAPdisconnect(true);
    ap_on = false;
  }
  bring_up_ap(channel, false);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  start_mdns();
  Serial.printf("STA %s rssi %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

}  // namespace

void wifi_init() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  uint8_t mac[6] = {};
  for (int i = 0; i < 20; i++) {
    WiFi.macAddress(mac);
    bool any = false;
    for (uint8_t b : mac) {
      if (b != 0) {
        any = true;
      }
    }
    if (any) {
      break;
    }
    delay(50);
  }
  snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
  Serial.printf("MAC %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setSleep(false);
  reload_requested = true;
}

void wifi_reload() {
  reload_requested = true;
}

void wifi_apply_identity() {
  char host[32];
  settings_effective_hostname(host, sizeof(host));
  WiFi.setHostname(host);
  if (ap_on) {
    WiFi.softAPsetHostname(host);
  }
  if (WiFi.status() == WL_CONNECTED || (WiFi.getMode() & WIFI_AP)) {
    start_mdns();
  }
  Serial.printf("hostname %s\n", host);
}

void wifi_loop() {
  if (reload_requested) {
    reload_requested = false;
    if (mdns_on) {
      MDNS.end();
      mdns_on = false;
    }
    stop_ap();
    WiFi.disconnect(true, false);
    if (!settings_has_wifi()) {
      enter_ap_only();
    } else {
      WiFi.mode(WIFI_STA);
      esp_wifi_set_ps(WIFI_PS_NONE);
      begin_sta();
    }
    return;
  }

  bool connected = WiFi.status() == WL_CONNECTED;
  if (phase == Phase::StaWait) {
    if (connected) {
      enter_online();
    } else if (millis() - phase_since > 20000) {
      Serial.println("STA join failed; falling back to the setup AP");
      enter_ap_only();
    }
  } else if (phase == Phase::Online) {
    if (connected) {
      disconnect_since = millis();
      if (!mdns_on) {
        start_mdns();
      }
      if (!ap_on) {
        WiFi.mode(WIFI_AP_STA);
        bring_up_ap(WiFi.channel(), false);
      }
    } else if (millis() - disconnect_since > 15000) {
      Serial.println("STA dropped; setup AP stays up while retrying");
      if (!ap_on) {
        WiFi.mode(WIFI_AP);
        bring_up_ap(6, true);
      } else {
        start_dns();
      }
      WiFi.mode(WIFI_AP_STA);
      begin_sta();
    }
  } else if (phase == Phase::ApOnly) {
    if (connected) {
      enter_online();
    } else if (settings_has_wifi() && static_cast<int32_t>(millis() - next_sta_try) >= 0) {
      Serial.println("retrying home Wi-Fi with the setup AP paused");
      stop_ap();
      WiFi.mode(WIFI_STA);
      esp_wifi_set_ps(WIFI_PS_NONE);
      begin_sta();
    }
  }

  if (dns_on) {
    dns.processNextRequest();
  }
}

bool wifi_sta_connected() {
  return WiFi.status() == WL_CONNECTED;
}

bool wifi_portal_active() {
  return dns_on;
}

void wifi_fill_status(WifiStatus* out) {
  memset(out, 0, sizeof(*out));
  out->sta_connected = WiFi.status() == WL_CONNECTED;
  out->ap_on = ap_on;
  out->joining = phase == Phase::StaWait;
  out->portal = dns_on;
  out->rssi = out->sta_connected ? WiFi.RSSI() : 0;
  strncpy(out->ap_ssid, ap_ssid, sizeof(out->ap_ssid) - 1);
  if (ap_on) {
    strncpy(out->ap_ip, "192.168.4.1", sizeof(out->ap_ip) - 1);
  }
  if (out->sta_connected && ap_on) {
    strncpy(out->mode, "sta+ap", sizeof(out->mode) - 1);
  } else if (out->sta_connected) {
    strncpy(out->mode, "sta", sizeof(out->mode) - 1);
  } else if (out->joining) {
    strncpy(out->mode, "joining", sizeof(out->mode) - 1);
  } else if (ap_on) {
    strncpy(out->mode, "ap", sizeof(out->mode) - 1);
  } else {
    strncpy(out->mode, "down", sizeof(out->mode) - 1);
  }

  DeviceSettings settings;
  settings_get(&settings);
  out->has_password = settings.password[0] != '\0';
  if (out->sta_connected) {
    strncpy(out->ssid, WiFi.SSID().c_str(), sizeof(out->ssid) - 1);
    strncpy(out->ip, WiFi.localIP().toString().c_str(), sizeof(out->ip) - 1);
  } else {
    strncpy(out->ssid, settings.ssid, sizeof(out->ssid) - 1);
  }

  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  snprintf(out->mac, sizeof(out->mac), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
