#pragma once

#include "config.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct CaptureStatus {
  uint32_t bytes_captured;
  uint32_t bytes_recorded;
  uint32_t record_dropped_bytes;
  uint32_t uart_overflows;
  uint32_t segments_discarded;
  uint32_t spill_failures;
  size_t flash_used;
  size_t flash_limit;
  size_t ram_used;
  size_t ram_limit;
  size_t live_limit;
  bool recording;
  bool flash_ok;
  bool last_start_valid;
  uint32_t last_start_ms;
  int64_t last_start_unix_ms;
};

struct CaptureSegment {
  uint32_t id;
  size_t size;
};

struct CaptureSnapshot {
  CaptureSegment segments[MAX_SEGMENTS];
  size_t segment_count;
  char* ram;
  size_t ram_len;
  bool ram_missing;
};

void capture_init();
void capture_poll();
void capture_flush();
void capture_note_streamed(uint32_t id);
void capture_write_line(const char* text, size_t text_len, size_t raw_bytes);
void capture_note_overflow(uint32_t count);
void capture_set_recording(bool on);
bool capture_recording();
void capture_clear();
void capture_status(CaptureStatus* out);

size_t capture_live_read(uint32_t since, uint32_t* cursor, bool* truncated, char* dst, size_t dst_cap);

void capture_segment_path(char* out, size_t out_n, uint32_t id);
bool capture_snapshot(CaptureSnapshot* snap);
void capture_snapshot_release(CaptureSnapshot* snap);
