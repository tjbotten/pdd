#include "settings.h"

#include "config.h"
#include "line_format.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cstdlib>
#include <cstring>

static portMUX_TYPE settings_mux = portMUX_INITIALIZER_UNLOCKED;
static DeviceSettings g_settings;
static Preferences prefs;

static void copy_str(char* dst, size_t n, const char* src) {
  if (n == 0) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

static bool valid_role(const char* role) {
  return role != nullptr && (strcmp(role, "tx") == 0 || strcmp(role, "rx") == 0);
}

static bool sanitize_hostname(const char* in, char* out, size_t out_n) {
  if (out_n < 2) {
    return false;
  }
  size_t j = 0;
  for (size_t i = 0; in != nullptr && in[i] != '\0'; i++) {
    char c = in[i];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) {
      return false;
    }
    if (j + 1 >= out_n || j >= 31) {
      return false;
    }
    out[j++] = c;
  }
  out[j] = '\0';
  if (j == 0) {
    return false;
  }
  if (out[0] == '-' || out[j - 1] == '-' || (out[0] >= '0' && out[0] <= '9')) {
    return false;
  }
  return true;
}

static void publish(const DeviceSettings* next) {
  portENTER_CRITICAL(&settings_mux);
  g_settings = *next;
  portEXIT_CRITICAL(&settings_mux);
}

static void load_uart(UartConfig* uart) {
  uart->baud = prefs.getUInt("baud", DEFAULT_BAUD);
  uart->data_bits = prefs.getUChar("bits", 8);
  uart->parity = static_cast<char>(prefs.getUChar("par", 'N'));
  uart->stop_code = prefs.getUChar("stop", 1);
  if (uart->baud < BAUD_MIN || uart->baud > BAUD_MAX) {
    uart->baud = DEFAULT_BAUD;
  }
  if (uart->data_bits < 5 || uart->data_bits > 8) {
    uart->data_bits = 8;
  }
  if (uart->parity != 'N' && uart->parity != 'E' && uart->parity != 'O') {
    uart->parity = 'N';
  }
  if (uart->stop_code != 1 && uart->stop_code != 15 && uart->stop_code != 2) {
    uart->stop_code = 1;
  }
}

static void save_all(const DeviceSettings* s) {
  prefs.begin("zsniff", false);
  prefs.putString("role", s->role);
  prefs.putString("host", s->hostname);
  prefs.putString("ssid", s->ssid);
  prefs.putString("psk", s->password);
  prefs.putUInt("baud", s->uart.baud);
  prefs.putUChar("bits", s->uart.data_bits);
  prefs.putUChar("par", static_cast<uint8_t>(s->uart.parity));
  prefs.putUChar("stop", s->uart.stop_code);
  prefs.end();
}

void settings_init() {
  DeviceSettings loaded{};
  loaded.uart.baud = DEFAULT_BAUD;
  loaded.uart.data_bits = 8;
  loaded.uart.parity = 'N';
  loaded.uart.stop_code = 1;

  prefs.begin("zsniff", true);
  String role = prefs.getString("role", "");
  String host = prefs.getString("host", "");
  String ssid = prefs.getString("ssid", "");
  String psk = prefs.getString("psk", "");
  load_uart(&loaded.uart);
  prefs.end();

  copy_str(loaded.role, sizeof(loaded.role), role.c_str());
  if (!valid_role(loaded.role)) {
    loaded.role[0] = '\0';
  }
  copy_str(loaded.hostname, sizeof(loaded.hostname), host.c_str());
  copy_str(loaded.ssid, sizeof(loaded.ssid), ssid.c_str());
  copy_str(loaded.password, sizeof(loaded.password), psk.c_str());
  publish(&loaded);

  char name[32];
  char label[32];
  settings_effective_hostname(name, sizeof(name));
  format_uart_label(label, sizeof(label), loaded.uart.baud, loaded.uart.data_bits,
                    loaded.uart.parity, loaded.uart.stop_code);
  Serial.printf("settings role=%s hostname=%s wifi=%s uart=%s\n",
                loaded.role[0] ? loaded.role : "(unset)", name,
                loaded.ssid[0] ? "saved" : "unset", label);
}

void settings_get(DeviceSettings* out) {
  portENTER_CRITICAL(&settings_mux);
  *out = g_settings;
  portEXIT_CRITICAL(&settings_mux);
}

void settings_get_uart(UartConfig* out) {
  portENTER_CRITICAL(&settings_mux);
  *out = g_settings.uart;
  portEXIT_CRITICAL(&settings_mux);
}

void settings_effective_hostname(char* out, size_t out_n) {
  DeviceSettings s;
  settings_get(&s);
  if (s.hostname[0] != '\0') {
    copy_str(out, out_n, s.hostname);
    return;
  }
  if (strcmp(s.role, "rx") == 0) {
    copy_str(out, out_n, DEFAULT_HOST_RX);
  } else if (strcmp(s.role, "tx") == 0) {
    copy_str(out, out_n, DEFAULT_HOST_TX);
  } else {
    copy_str(out, out_n, DEFAULT_HOST_UNSET);
  }
}

bool settings_has_wifi() {
  DeviceSettings s;
  settings_get(&s);
  return s.ssid[0] != '\0';
}

bool settings_set_device(const char* role, const char* hostname) {
  if (!valid_role(role)) {
    return false;
  }
  char host[32];
  if (hostname == nullptr || hostname[0] == '\0') {
    copy_str(host, sizeof(host), strcmp(role, "rx") == 0 ? DEFAULT_HOST_RX : DEFAULT_HOST_TX);
  } else if (!sanitize_hostname(hostname, host, sizeof(host))) {
    return false;
  }

  DeviceSettings next;
  settings_get(&next);
  copy_str(next.role, sizeof(next.role), role);
  copy_str(next.hostname, sizeof(next.hostname), host);
  save_all(&next);
  publish(&next);
  Serial.printf("role=%s hostname=%s\n", next.role, next.hostname);
  return true;
}

bool settings_set_wifi(const char* ssid, const char* password, bool password_provided, bool open_network) {
  if (ssid == nullptr) {
    ssid = "";
  }
  if (strlen(ssid) > 32) {
    return false;
  }
  if (password_provided && password != nullptr && strlen(password) > 64) {
    return false;
  }

  DeviceSettings next;
  settings_get(&next);
  copy_str(next.ssid, sizeof(next.ssid), ssid);
  if (next.ssid[0] == '\0' || open_network) {
    next.password[0] = '\0';
  } else if (password_provided && password != nullptr) {
    copy_str(next.password, sizeof(next.password), password);
  }
  save_all(&next);
  publish(&next);
  Serial.printf("wifi ssid %s (%s)\n", next.ssid[0] ? next.ssid : "(forgotten)",
                next.password[0] ? "with password" : "open");
  return true;
}

bool settings_uart_from_strings(const char* baud, const char* data_bits, const char* parity,
                                const char* stop, UartConfig* out) {
  if (baud == nullptr || data_bits == nullptr || parity == nullptr || stop == nullptr || out == nullptr) {
    return false;
  }
  char* end = nullptr;
  unsigned long baud_ul = strtoul(baud, &end, 10);
  if (end == baud || *end != '\0' || baud_ul < BAUD_MIN || baud_ul > BAUD_MAX) {
    return false;
  }
  end = nullptr;
  unsigned long bits = strtoul(data_bits, &end, 10);
  if (end == data_bits || *end != '\0' || bits < 5 || bits > 8) {
    return false;
  }
  char par = parity[0];
  if (par >= 'a' && par <= 'z') {
    par = static_cast<char>(par - 'a' + 'A');
  }
  if ((par != 'N' && par != 'E' && par != 'O') || parity[1] != '\0') {
    return false;
  }
  uint8_t stop_code = 1;
  if (strcmp(stop, "1") == 0) {
    stop_code = 1;
  } else if (strcmp(stop, "1.5") == 0) {
    stop_code = 15;
  } else if (strcmp(stop, "2") == 0) {
    stop_code = 2;
  } else {
    return false;
  }
  out->baud = static_cast<uint32_t>(baud_ul);
  out->data_bits = static_cast<uint8_t>(bits);
  out->parity = par;
  out->stop_code = stop_code;
  return true;
}

bool settings_set_uart(const UartConfig* uart) {
  if (uart == nullptr || uart->baud < BAUD_MIN || uart->baud > BAUD_MAX) {
    return false;
  }
  if (uart->data_bits < 5 || uart->data_bits > 8) {
    return false;
  }
  if (uart->parity != 'N' && uart->parity != 'E' && uart->parity != 'O') {
    return false;
  }
  if (uart->stop_code != 1 && uart->stop_code != 15 && uart->stop_code != 2) {
    return false;
  }
  DeviceSettings next;
  settings_get(&next);
  next.uart = *uart;
  save_all(&next);
  publish(&next);
  char label[32];
  format_uart_label(label, sizeof(label), uart->baud, uart->data_bits, uart->parity, uart->stop_code);
  Serial.printf("uart %s\n", label);
  return true;
}
