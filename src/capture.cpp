#include "capture.h"

#include "text_ring.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>
#include <stdlib.h>

static SemaphoreHandle_t cap_mu;
static TextRing live;
static char live_buf[LIVE_TEXT_CAP];
static char rec_ram[REC_RAM_CAP];
static size_t rec_ram_len;
static char spill_buf[SPILL_CHUNK];

static bool recording;
static bool flash_ok;
static bool freeze_trim;
static uint32_t keep_from_id;
static uint32_t streamed_through_id;
static uint32_t bytes_captured;
static uint32_t bytes_recorded;
static uint32_t record_dropped_bytes;
static uint32_t uart_overflows;
static uint32_t segments_discarded;
static uint32_t spill_failures;
static uint32_t last_dirty_ms;
static uint32_t spill_backoff_until;
static bool last_start_valid;
static uint32_t last_start_ms;
static int64_t last_start_unix_ms;

static CaptureSegment segs[MAX_SEGMENTS];
static size_t seg_count;
static uint32_t next_id;
static size_t flash_used;
static size_t flash_limit = REC_FLASH_CAP;

static void lock() {
  xSemaphoreTake(cap_mu, portMAX_DELAY);
}

static void unlock() {
  xSemaphoreGive(cap_mu);
}

void capture_segment_path(char* out, size_t out_n, uint32_t id) {
  snprintf(out, out_n, "/r/%06lu.txt", static_cast<unsigned long>(id));
}

static bool parse_id(const char* name, uint32_t* id_out) {
  const char* base = strrchr(name, '/');
  base = base ? base + 1 : name;
  char* end = nullptr;
  unsigned long id = strtoul(base, &end, 10);
  if (end == base || strcmp(end, ".txt") != 0 || id == 0) {
    return false;
  }
  *id_out = static_cast<uint32_t>(id);
  return true;
}

static void sort_segs() {
  for (size_t i = 1; i < seg_count; i++) {
    CaptureSegment key = segs[i];
    size_t j = i;
    while (j > 0 && segs[j - 1].id > key.id) {
      segs[j] = segs[j - 1];
      j--;
    }
    segs[j] = key;
  }
}

static void delete_segment_at(size_t index) {
  char path[32];
  capture_segment_path(path, sizeof(path), segs[index].id);
  LittleFS.remove(path);
  if (flash_used >= segs[index].size) {
    flash_used -= segs[index].size;
  } else {
    flash_used = 0;
  }
  for (size_t i = index + 1; i < seg_count; i++) {
    segs[i - 1] = segs[i];
  }
  seg_count--;
  segments_discarded++;
}

static bool segment_deletable(uint32_t id) {
  if (!freeze_trim) {
    return true;
  }
  // A download is in progress. Keep segments that have not been sent yet,
  // and keep anything written after the snapshot. Already-sent ids can go
  // so a full flash ring still has room for bytes that arrive mid-download.
  if (keep_from_id != 0 && id >= keep_from_id) {
    return false;
  }
  return id <= streamed_through_id;
}

static bool trim_to_fit(size_t incoming) {
  while (flash_used + incoming > flash_limit && seg_count > 1 && segment_deletable(segs[0].id)) {
    delete_segment_at(0);
  }
  return flash_used + incoming <= flash_limit;
}

static bool start_new_segment() {
  if (seg_count >= MAX_SEGMENTS) {
    if (seg_count < 2 || !segment_deletable(segs[0].id)) {
      return false;
    }
    delete_segment_at(0);
  }
  if (next_id == 0) {
    next_id = 1;
  }
  char path[32];
  capture_segment_path(path, sizeof(path), next_id);
  File file = LittleFS.open(path, "w");
  if (!file) {
    return false;
  }
  file.close();
  segs[seg_count].id = next_id;
  segs[seg_count].size = 0;
  seg_count++;
  next_id++;
  return true;
}

static bool append_chunk(const char* data, size_t n) {
  if (seg_count == 0) {
    return false;
  }
  char path[32];
  capture_segment_path(path, sizeof(path), segs[seg_count - 1].id);
  File file = LittleFS.open(path, "a");
  if (!file) {
    return false;
  }
  size_t wrote = file.write(reinterpret_cast<const uint8_t*>(data), n);
  file.close();
  if (wrote != n) {
    return false;
  }
  segs[seg_count - 1].size += wrote;
  flash_used += wrote;
  return true;
}

static bool flash_write_all(const char* data, size_t n) {
  size_t off = 0;
  while (off < n) {
    // After a download snapshot, never append to a segment the response is
    // still reading. New bytes go to a new file so that file stays stable.
    bool need_new = seg_count == 0 || segs[seg_count - 1].size >= REC_SEGMENT_MAX;
    if (!need_new && freeze_trim && keep_from_id != 0 && segs[seg_count - 1].id < keep_from_id) {
      need_new = true;
    }
    if (need_new) {
      if (!start_new_segment()) {
        return false;
      }
    }
    size_t room = REC_SEGMENT_MAX - segs[seg_count - 1].size;
    size_t chunk = n - off;
    if (chunk > room) {
      chunk = room;
    }
    if (!trim_to_fit(chunk)) {
      return false;
    }
    if (!append_chunk(data + off, chunk)) {
      return false;
    }
    off += chunk;
  }
  return true;
}

static void scan_flash() {
  seg_count = 0;
  flash_used = 0;
  next_id = 1;
  if (!LittleFS.exists("/r")) {
    LittleFS.mkdir("/r");
  }
  File dir = LittleFS.open("/r");
  if (!dir || !dir.isDirectory()) {
    return;
  }
  File file = dir.openNextFile();
  while (file) {
    uint32_t id = 0;
    if (parse_id(file.name(), &id) && seg_count < MAX_SEGMENTS) {
      segs[seg_count].id = id;
      segs[seg_count].size = file.size();
      flash_used += file.size();
      seg_count++;
      if (id >= next_id) {
        next_id = id + 1;
      }
    } else if (parse_id(file.name(), &id)) {
      char path[32];
      capture_segment_path(path, sizeof(path), id);
      file.close();
      LittleFS.remove(path);
      file = dir.openNextFile();
      continue;
    }
    file.close();
    file = dir.openNextFile();
  }
  dir.close();
  sort_segs();
  while (seg_count > 1 && flash_used > flash_limit) {
    delete_segment_at(0);
  }
}

static bool rec_store_unlocked(const char* text, size_t text_len, size_t raw_bytes) {
  if (text_len > REC_RAM_CAP) {
    record_dropped_bytes += static_cast<uint32_t>(raw_bytes);
    return false;
  }
  if (rec_ram_len + text_len > REC_RAM_CAP) {
    if (!flash_ok) {
      size_t need = rec_ram_len + text_len - REC_RAM_CAP;
      size_t drop = need;
      for (size_t i = need; i < rec_ram_len; i++) {
        if (rec_ram[i] == '\n') {
          drop = i + 1;
          break;
        }
      }
      if (drop > rec_ram_len) {
        drop = rec_ram_len;
      }
      memmove(rec_ram, rec_ram + drop, rec_ram_len - drop);
      rec_ram_len -= drop;
      segments_discarded++;
    } else {
      record_dropped_bytes += static_cast<uint32_t>(raw_bytes);
      return false;
    }
  }
  if (rec_ram_len == 0) {
    last_dirty_ms = millis();
  }
  memcpy(rec_ram + rec_ram_len, text, text_len);
  rec_ram_len += text_len;
  bytes_recorded += static_cast<uint32_t>(raw_bytes);
  return true;
}

void capture_init() {
  cap_mu = xSemaphoreCreateMutex();
  text_ring_init(&live, live_buf, LIVE_TEXT_CAP);
  flash_ok = LittleFS.begin(true, "/littlefs", 10, "spiffs");
  flash_limit = REC_FLASH_CAP;
  if (flash_ok) {
    size_t total = LittleFS.totalBytes();
    if (total > 96 * 1024) {
      size_t usable = total - 64 * 1024;
      if (usable < flash_limit) {
        flash_limit = usable;
      }
    }
    scan_flash();
    Serial.printf("capture flash %u bytes, limit %u, used %u, segments %u\n",
                  static_cast<unsigned>(LittleFS.totalBytes()),
                  static_cast<unsigned>(flash_limit),
                  static_cast<unsigned>(flash_used),
                  static_cast<unsigned>(seg_count));
  } else {
    Serial.println("capture LittleFS unavailable; recording stays in the 32 KiB RAM ring");
  }
}

void capture_write_line(const char* text, size_t text_len, size_t raw_bytes) {
  if (text == nullptr || text_len == 0) {
    return;
  }
  lock();
  bytes_captured += static_cast<uint32_t>(raw_bytes);
  text_ring_append(&live, text, text_len);
  if (recording) {
    rec_store_unlocked(text, text_len, raw_bytes);
  }
  unlock();
}

void capture_note_overflow(uint32_t count) {
  lock();
  uart_overflows += count;
  unlock();
}

void capture_set_recording(bool on) {
  lock();
  if (on && !recording) {
    last_start_valid = true;
    last_start_ms = millis();
    time_t now = time(nullptr);
    last_start_unix_ms = now > 1700000000 ? static_cast<int64_t>(now) * 1000 : 0;
    if (rec_ram_len == 0) {
      last_dirty_ms = millis();
    }
  }
  recording = on;
  unlock();
  Serial.printf("recording %s\n", on ? "on" : "off");
}

bool capture_recording() {
  lock();
  bool on = recording;
  unlock();
  return on;
}

void capture_clear() {
  uint32_t ids[MAX_SEGMENTS];
  size_t n = 0;
  lock();
  text_ring_clear(&live);
  rec_ram_len = 0;
  bytes_captured = 0;
  bytes_recorded = 0;
  record_dropped_bytes = 0;
  uart_overflows = 0;
  segments_discarded = 0;
  spill_failures = 0;
  last_start_valid = false;
  last_start_ms = 0;
  last_start_unix_ms = 0;
  for (size_t i = 0; i < seg_count && n < MAX_SEGMENTS; i++) {
    ids[n++] = segs[i].id;
  }
  seg_count = 0;
  flash_used = 0;
  unlock();
  for (size_t i = 0; i < n; i++) {
    char path[32];
    capture_segment_path(path, sizeof(path), ids[i]);
    LittleFS.remove(path);
  }
  Serial.println("capture cleared");
}

void capture_status(CaptureStatus* out) {
  lock();
  out->bytes_captured = bytes_captured;
  out->bytes_recorded = bytes_recorded;
  out->record_dropped_bytes = record_dropped_bytes;
  out->uart_overflows = uart_overflows;
  out->segments_discarded = segments_discarded;
  out->spill_failures = spill_failures;
  out->flash_used = flash_used;
  out->flash_limit = flash_limit;
  out->ram_used = rec_ram_len;
  out->ram_limit = REC_RAM_CAP;
  out->live_limit = LIVE_TEXT_CAP;
  out->recording = recording;
  out->flash_ok = flash_ok;
  out->last_start_valid = last_start_valid;
  out->last_start_ms = last_start_ms;
  out->last_start_unix_ms = last_start_unix_ms;
  unlock();
}

size_t capture_live_read(uint32_t since, uint32_t* cursor, bool* truncated, char* dst, size_t dst_cap) {
  lock();
  size_t n = text_ring_read(&live, since, 4096, cursor, truncated, dst, dst_cap);
  unlock();
  return n;
}

static bool spill_once(bool ignore_backoff) {
  if (!flash_ok) {
    return false;
  }
  uint32_t now = millis();
  if (!ignore_backoff && now < spill_backoff_until) {
    return false;
  }
  size_t n = 0;
  lock();
  n = rec_ram_len;
  if (n > sizeof(spill_buf)) {
    n = sizeof(spill_buf);
    for (size_t i = n; i > 0; i--) {
      if (rec_ram[i - 1] == '\n') {
        n = i;
        break;
      }
    }
  }
  if (n > 0) {
    memcpy(spill_buf, rec_ram, n);
  }
  unlock();
  if (n == 0) {
    return false;
  }
  if (!flash_write_all(spill_buf, n)) {
    spill_failures++;
    spill_backoff_until = millis() + 2000;
    return false;
  }
  lock();
  if (rec_ram_len >= n && memcmp(rec_ram, spill_buf, n) == 0) {
    memmove(rec_ram, rec_ram + n, rec_ram_len - n);
    rec_ram_len -= n;
    last_dirty_ms = millis();
  }
  unlock();
  return true;
}

void capture_poll() {
  if (!flash_ok) {
    return;
  }
  uint32_t now = millis();
  bool due = false;
  bool big = false;
  lock();
  due = rec_ram_len > 0 && static_cast<uint32_t>(now - last_dirty_ms) > 5000;
  big = rec_ram_len >= SPILL_CHUNK;
  unlock();
  if (due || big) {
    spill_once(false);
  }
}

void capture_flush() {
  for (int i = 0; i < 8; i++) {
    size_t pending = 0;
    lock();
    pending = rec_ram_len;
    unlock();
    if (pending == 0) {
      return;
    }
    if (!spill_once(true)) {
      return;
    }
  }
}

void capture_note_streamed(uint32_t id) {
  if (id > streamed_through_id) {
    streamed_through_id = id;
  }
}

bool capture_snapshot(CaptureSnapshot* snap) {
  if (snap == nullptr) {
    return false;
  }
  memset(snap, 0, sizeof(*snap));
  lock();
  freeze_trim = true;
  keep_from_id = next_id == 0 ? 1 : next_id;
  streamed_through_id = 0;
  snap->segment_count = seg_count;
  for (size_t i = 0; i < seg_count; i++) {
    snap->segments[i] = segs[i];
  }
  snap->ram_len = rec_ram_len;
  snap->ram_missing = false;
  if (rec_ram_len > 0) {
    snap->ram = static_cast<char*>(malloc(rec_ram_len));
    if (snap->ram != nullptr) {
      memcpy(snap->ram, rec_ram, rec_ram_len);
    } else {
      snap->ram_len = 0;
      snap->ram_missing = true;
    }
  }
  unlock();
  return true;
}

void capture_snapshot_release(CaptureSnapshot* snap) {
  if (snap == nullptr) {
    return;
  }
  free(snap->ram);
  snap->ram = nullptr;
  snap->ram_len = 0;
  lock();
  freeze_trim = false;
  keep_from_id = 0;
  streamed_through_id = 0;
  unlock();
}
