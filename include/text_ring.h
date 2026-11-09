#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Byte ring that remembers the last `cap` bytes of a stream.
// `total` is the count of bytes ever appended (mod 2^32) and is the
// cursor a client passes back to read only what it has not seen yet.

struct TextRing {
  char* buf;
  size_t cap;
  size_t pos;
  size_t len;
  uint32_t total;
};

inline void text_ring_init(TextRing* ring, char* storage, size_t cap) {
  ring->buf = storage;
  ring->cap = cap;
  ring->pos = 0;
  ring->len = 0;
  ring->total = 0;
}

inline void text_ring_clear(TextRing* ring) {
  ring->pos = 0;
  ring->len = 0;
  // `total` is intentionally kept. Readers hold an absolute cursor;
  // resetting it would make a stale cursor look like it is still ahead.
}

inline void text_ring_copy(const TextRing* ring, size_t offset_from_oldest, char* dst, size_t n) {
  if (n == 0 || ring->cap == 0) {
    return;
  }
  size_t idx = (ring->pos + offset_from_oldest) % ring->cap;
  size_t first = n;
  if (idx + first > ring->cap) {
    first = ring->cap - idx;
  }
  memcpy(dst, ring->buf + idx, first);
  if (n > first) {
    memcpy(dst + first, ring->buf, n - first);
  }
}

inline void text_ring_append(TextRing* ring, const char* data, size_t n) {
  if (n == 0 || ring->cap == 0 || data == nullptr) {
    return;
  }
  const uint32_t add = static_cast<uint32_t>(n);
  if (n >= ring->cap) {
    data += n - ring->cap;
    n = ring->cap;
    memcpy(ring->buf, data, n);
    ring->pos = 0;
    ring->len = n;
    ring->total += add;
    return;
  }
  if (ring->len + n > ring->cap) {
    size_t drop = ring->len + n - ring->cap;
    ring->pos = (ring->pos + drop) % ring->cap;
    ring->len -= drop;
  }
  size_t end = (ring->pos + ring->len) % ring->cap;
  size_t first = n;
  if (end + first > ring->cap) {
    first = ring->cap - end;
  }
  memcpy(ring->buf + end, data, first);
  if (n > first) {
    memcpy(ring->buf, data + first, n - first);
  }
  ring->len += n;
  ring->total += add;
}

// Copy the next unseen bytes into `dst`.
// `truncated` is set when `since` has already fallen out of the ring;
// in that case the newest tail is returned and the cursor jumps forward.
inline size_t text_ring_read(const TextRing* ring, uint32_t since, size_t max_chunk,
                             uint32_t* cursor, bool* truncated, char* dst, size_t dst_cap) {
  if (cursor) {
    *cursor = ring->total;
  }
  if (truncated) {
    *truncated = false;
  }
  if (max_chunk == 0 || dst_cap == 0 || ring->len == 0) {
    return 0;
  }
  uint32_t behind = ring->total - since;
  if (behind == 0) {
    return 0;
  }
  if (behind > ring->len) {
    size_t tail = ring->len;
    if (tail > max_chunk) {
      tail = max_chunk;
    }
    if (tail > dst_cap) {
      tail = dst_cap;
    }
    text_ring_copy(ring, ring->len - tail, dst, tail);
    if (truncated) {
      *truncated = true;
    }
    if (cursor) {
      *cursor = ring->total;
    }
    return tail;
  }
  size_t n = behind;
  if (n > max_chunk) {
    n = max_chunk;
  }
  if (n > dst_cap) {
    n = dst_cap;
  }
  size_t offset = ring->len - static_cast<size_t>(behind);
  text_ring_copy(ring, offset, dst, n);
  if (cursor) {
    *cursor = since + static_cast<uint32_t>(n);
  }
  return n;
}
