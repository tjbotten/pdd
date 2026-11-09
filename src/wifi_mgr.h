#pragma once

#include <stdbool.h>

struct WifiStatus {
  bool sta_connected;
  bool ap_on;
  bool joining;
  bool portal;
  char mode[12];
  char ssid[33];
  char ip[16];
  char ap_ssid[24];
  char ap_ip[16];
  char mac[18];
  int rssi;
  bool has_password;
};

void wifi_init();
void wifi_loop();
void wifi_reload();
void wifi_apply_identity();
void wifi_fill_status(WifiStatus* out);
bool wifi_sta_connected();
bool wifi_portal_active();
