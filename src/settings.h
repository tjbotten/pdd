#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct UartConfig {
  uint32_t baud;
  uint8_t data_bits;  // 5..8
  char parity;        // 'N', 'E', 'O'
  uint8_t stop_code;  // 1, 15 (1.5), 2
};

struct DeviceSettings {
  char role[4];  // "", "tx", "rx"
  char hostname[32];
  char ssid[33];
  char password[65];
  UartConfig uart;
};

void settings_init();
void settings_get(DeviceSettings* out);
void settings_get_uart(UartConfig* out);

// Empty hostname selects zone-tx / zone-rx from the role.
// Returns false and leaves storage unchanged when the values are rejected.
bool settings_set_device(const char* role, const char* hostname);

// password_provided false keeps the stored password.
// open_network stores an empty password (for an open SSID).
// An empty SSID forgets the network.
bool settings_set_wifi(const char* ssid, const char* password, bool password_provided, bool open_network);

bool settings_set_uart(const UartConfig* uart);

void settings_effective_hostname(char* out, size_t out_n);
bool settings_has_wifi();
bool settings_uart_from_strings(const char* baud, const char* data_bits, const char* parity,
                                const char* stop, UartConfig* out);
