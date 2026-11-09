#pragma once

#include <stddef.h>
#include <stdint.h>

// Listen-only sniffer for one side of a zone-buffer serial link.
// GPIO21 is the Super Mini's UART0 TX pad. The ROM can drive it during
// reset, before this firmware runs, so the wire must leave GPIO21 open.
// This firmware never assigns a UART TX pin.

static constexpr int PIN_UART_RX = 20;
static constexpr int PIN_UART_TX_UNCONNECTED = 21;
static constexpr int PIN_LED = 8;  // active-low blue LED on the common Super Mini

static constexpr int UART_PORT_NUM = 1;
static constexpr int UART_RX_BUFFER = 16384;
static constexpr int UART_LINE_BYTES = 16;
static constexpr uint32_t UART_GAP_MS = 20;

static constexpr size_t LIVE_TEXT_CAP = 24 * 1024;
static constexpr size_t REC_RAM_CAP = 32 * 1024;
static constexpr size_t REC_FLASH_CAP = 2 * 1024 * 1024;
static constexpr size_t REC_SEGMENT_MAX = 64 * 1024;
static constexpr size_t SPILL_CHUNK = 8 * 1024;
static constexpr int MAX_SEGMENTS = 48;

static constexpr uint32_t DEFAULT_BAUD = 9600;
static constexpr uint32_t BAUD_MIN = 300;
static constexpr uint32_t BAUD_MAX = 1000000;

// Setup access point. This is not a home-network credential.
// The SSID is AP_SSID_PREFIX plus the last two MAC bytes, so the two
// boards can be told apart while they are both in setup mode.
static constexpr const char* AP_PASSWORD = "sniffsetup";
static constexpr const char* AP_SSID_PREFIX = "zone-sniff-";

static constexpr const char* DEFAULT_HOST_TX = "zone-tx";
static constexpr const char* DEFAULT_HOST_RX = "zone-rx";
static constexpr const char* DEFAULT_HOST_UNSET = "zone-sniffer";
