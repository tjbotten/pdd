#pragma once

#include <ctime>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

// One recorded line:
//   boot_ms <TAB> utc-or-dash <TAB> hex <TAB> ascii <NL>
// Non-printable bytes (and DEL) are '.' in the ASCII column.
// `unix_ms` <= 0 means the clock is not set; the UTC column is "-".

inline void format_utc_ms(char* dst, size_t dst_n, int64_t unix_ms) {
  if (dst_n == 0) {
    return;
  }
  if (unix_ms <= 0) {
    snprintf(dst, dst_n, "-");
    return;
  }
  time_t sec = static_cast<time_t>(unix_ms / 1000);
  int ms = static_cast<int>(unix_ms % 1000);
  if (ms < 0) {
    ms = 0;
  }
  struct tm tm_utc {};
  if (gmtime_r(&sec, &tm_utc) == nullptr) {
    snprintf(dst, dst_n, "-");
    return;
  }
  snprintf(dst, dst_n, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
           tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
           tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, ms);
}

inline void format_uart_label(char* dst, size_t dst_n, uint32_t baud, uint8_t data_bits,
                              char parity, uint8_t stop_code) {
  const char* stop = "1";
  if (stop_code == 2) {
    stop = "2";
  } else if (stop_code == 15) {
    stop = "1.5";
  }
  snprintf(dst, dst_n, "%lu %u%c%s", static_cast<unsigned long>(baud), data_bits, parity, stop);
}

// Returns the number of characters that would have been written, excluding
// the terminating NUL, matching snprintf. The line is always NUL-terminated
// when dst_n > 0.
inline int format_capture_line(char* dst, size_t dst_n, uint32_t boot_ms, int64_t unix_ms,
                               const uint8_t* data, size_t len) {
  if (dst_n == 0) {
    return 0;
  }
  char utc[40];
  format_utc_ms(utc, sizeof(utc), unix_ms);

  char hex[16 * 3 + 1];
  char ascii[17];
  size_t shown = len;
  if (shown > 16) {
    shown = 16;
  }
  size_t hex_used = 0;
  hex[0] = '\0';
  for (size_t i = 0; i < shown; i++) {
    int wrote = snprintf(hex + hex_used, sizeof(hex) - hex_used, i ? " %02X" : "%02X", data[i]);
    if (wrote < 0) {
      break;
    }
    hex_used += static_cast<size_t>(wrote);
    uint8_t b = data[i];
    ascii[i] = (b >= 32 && b < 127) ? static_cast<char>(b) : '.';
  }
  ascii[shown] = '\0';
  return snprintf(dst, dst_n, "%lu\t%s\t%s\t%s\n",
                  static_cast<unsigned long>(boot_ms), utc, hex, ascii);
}
