#include "line_format.h"
#include "text_ring.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

static void expect_eq(const std::string& got, const std::string& want, const char* name) {
  if (got != want) {
    std::cerr << name << "\n got: " << got << "\n want: " << want << "\n";
    std::exit(1);
  }
}

static std::string ring_contents(const TextRing& ring) {
  if (ring.len == 0) {
    return "";
  }
  std::string out(ring.len, '\0');
  uint32_t cursor = 0;
  bool truncated = true;
  uint32_t oldest = ring.total - static_cast<uint32_t>(ring.len);
  size_t n = text_ring_read(&ring, oldest, ring.len, &cursor, &truncated, out.data(), out.size());
  assert(!truncated);
  assert(n == ring.len);
  assert(cursor == ring.total);
  out.resize(n);
  return out;
}

static void test_line_format() {
  const uint8_t data[] = {0x02, 'A', ' ', '~', 0x7F, '\n'};
  char line[128];
  int n = format_capture_line(line, sizeof(line), 15320, 0, data, sizeof(data));
  assert(n > 0);
  expect_eq(line, "15320\t-\t02 41 20 7E 7F 0A\t.A ~..\n", "line without clock");

  char label[32];
  format_uart_label(label, sizeof(label), 9600, 8, 'N', 1);
  expect_eq(label, "9600 8N1", "8N1 label");
  format_uart_label(label, sizeof(label), 19200, 7, 'E', 2);
  expect_eq(label, "19200 7E2", "7E2 label");
  format_uart_label(label, sizeof(label), 115200, 8, 'O', 15);
  expect_eq(label, "115200 8O1.5", "8O1.5 label");

  int64_t unix_ms = 1700000000000LL + 123;
  format_capture_line(line, sizeof(line), 9, unix_ms, data, 1);
  assert(std::string(line).find("\t2023-11-14T22:13:20.123Z\t02\t.\n") != std::string::npos);
}

static void test_ring_matches_model() {
  constexpr size_t kCap = 64;
  char storage[kCap];
  TextRing ring;
  text_ring_init(&ring, storage, kCap);
  std::string model;
  uint32_t total = 0;
  const char* pieces[] = {"hello", " ", "world", "\n", "0123456789abcdef", "XY", "tail-of-frame"};
  for (int round = 0; round < 30; round++) {
    for (const char* piece : pieces) {
      size_t n = std::strlen(piece);
      text_ring_append(&ring, piece, n);
      model.append(piece, n);
      total += static_cast<uint32_t>(n);
      if (model.size() > kCap) {
        model.erase(0, model.size() - kCap);
      }
      assert(ring.total == total);
      assert(ring.len == model.size());
      expect_eq(ring_contents(ring), model, "ring contents");
    }
  }

  uint32_t cursor = 0;
  bool truncated = false;
  char buf[16];
  size_t n = text_ring_read(&ring, ring.total, 16, &cursor, &truncated, buf, sizeof(buf));
  assert(n == 0);
  assert(!truncated);
  assert(cursor == ring.total);

  n = text_ring_read(&ring, 0, 8, &cursor, &truncated, buf, sizeof(buf));
  assert(truncated);
  assert(cursor == ring.total);
  assert(n == 8);
  expect_eq(std::string(buf, n), model.substr(model.size() - 8), "truncated tail");
}

static void test_ring_wrap_cursor() {
  char storage[8];
  TextRing ring;
  text_ring_init(&ring, storage, 8);
  ring.total = 0xFFFFFFFDu;
  text_ring_append(&ring, "abcdefghij", 10);
  assert(ring.len == 8);
  assert(ring.total == static_cast<uint32_t>(0xFFFFFFFDu + 10));
  expect_eq(ring_contents(ring), "cdefghij", "wrapped total keeps the tail");

  char buf[8];
  uint32_t cursor = 0;
  bool truncated = false;
  uint32_t since = ring.total - 3;
  size_t n = text_ring_read(&ring, since, 8, &cursor, &truncated, buf, sizeof(buf));
  assert(!truncated);
  assert(n == 3);
  assert(cursor == ring.total);
  expect_eq(std::string(buf, n), "hij", "cursor inside wrapped ring");
}

int main() {
  test_line_format();
  test_ring_matches_model();
  test_ring_wrap_cursor();
  std::cout << "host tests ok\n";
  return 0;
}
