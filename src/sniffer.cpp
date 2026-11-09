#include "sniffer.h"

#include "capture.h"
#include "config.h"
#include "line_format.h"
#include "settings.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

#include <cstring>

static TaskHandle_t sniffer_task_handle;
static QueueHandle_t uart_queue;
static volatile bool reconfigure = true;
static bool driver_installed;

static uart_word_length_t word_length(uint8_t bits) {
  switch (bits) {
    case 5:
      return UART_DATA_5_BITS;
    case 6:
      return UART_DATA_6_BITS;
    case 7:
      return UART_DATA_7_BITS;
    default:
      return UART_DATA_8_BITS;
  }
}

static uart_parity_t parity_of(char parity) {
  if (parity == 'E') {
    return UART_PARITY_EVEN;
  }
  if (parity == 'O') {
    return UART_PARITY_ODD;
  }
  return UART_PARITY_DISABLE;
}

static uart_stop_bits_t stop_of(uint8_t stop_code) {
  if (stop_code == 2) {
    return UART_STOP_BITS_2;
  }
  if (stop_code == 15) {
    return UART_STOP_BITS_1_5;
  }
  return UART_STOP_BITS_1;
}

static int64_t unix_ms_now() {
  time_t sec = time(nullptr);
  if (sec < 1700000000) {
    return 0;
  }
  struct timeval tv {};
  gettimeofday(&tv, nullptr);
  return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

static void release_unused_tx_pin() {
  gpio_num_t pin = static_cast<gpio_num_t>(PIN_UART_TX_UNCONNECTED);
  gpio_reset_pin(pin);
  gpio_set_direction(pin, GPIO_MODE_INPUT);
  gpio_set_pull_mode(pin, GPIO_FLOATING);
  // Point the pad at plain GPIO so UART0's TX signal (ROM boot log) is detached.
  esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_direction(pin, GPIO_MODE_INPUT);
}

static void apply_uart(const UartConfig& cfg) {
  if (driver_installed) {
    uart_driver_delete(static_cast<uart_port_t>(UART_PORT_NUM));
    driver_installed = false;
    uart_queue = nullptr;
  }

  uart_config_t uart_config {};
  uart_config.baud_rate = static_cast<int>(cfg.baud);
  uart_config.data_bits = word_length(cfg.data_bits);
  uart_config.parity = parity_of(cfg.parity);
  uart_config.stop_bits = stop_of(cfg.stop_code);
  uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_config.rx_flow_ctrl_thresh = 0;
  uart_config.source_clk = UART_SCLK_DEFAULT;

  esp_err_t err = uart_param_config(static_cast<uart_port_t>(UART_PORT_NUM), &uart_config);
  int rx_buf = UART_RX_BUFFER;
  if (err == ESP_OK) {
    err = uart_driver_install(static_cast<uart_port_t>(UART_PORT_NUM), rx_buf, 0, 32, &uart_queue,
                              ESP_INTR_FLAG_IRAM);
  }
  if (err != ESP_OK) {
    rx_buf = 4096;
    err = uart_driver_install(static_cast<uart_port_t>(UART_PORT_NUM), rx_buf, 0, 16, &uart_queue,
                              ESP_INTR_FLAG_IRAM);
  }
  if (err == ESP_OK) {
    // TX, RTS, and CTS stay unassigned. Nothing is driven onto the RS232 line.
    err = uart_set_pin(static_cast<uart_port_t>(UART_PORT_NUM), UART_PIN_NO_CHANGE, PIN_UART_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  }
  release_unused_tx_pin();
  driver_installed = err == ESP_OK;
  if (driver_installed) {
    uart_flush_input(static_cast<uart_port_t>(UART_PORT_NUM));
  }

  char label[32];
  format_uart_label(label, sizeof(label), cfg.baud, cfg.data_bits, cfg.parity, cfg.stop_code);
  Serial.printf("UART%d listen-only RX GPIO%d, TX not assigned, %s, rx buffer %d, err %d\n",
                UART_PORT_NUM, PIN_UART_RX, label, rx_buf, static_cast<int>(err));
}

static void drain_overflows() {
  if (uart_queue == nullptr) {
    return;
  }
  uart_event_t event;
  while (xQueueReceive(uart_queue, &event, 0) == pdTRUE) {
    if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
      capture_note_overflow(1);
      uart_flush_input(static_cast<uart_port_t>(UART_PORT_NUM));
      xQueueReset(uart_queue);
      Serial.printf("UART overflow (%d); input flushed\n", static_cast<int>(event.type));
      break;
    }
  }
}

static void sniffer_task(void* /*arg*/) {
  uint8_t batch[256];
  uint8_t line[UART_LINE_BYTES];
  size_t line_len = 0;
  uint32_t line_ms = 0;
  int64_t line_unix = 0;
  uint32_t last_byte_ms = 0;
  bool line_open = false;

  auto flush_line = [&]() {
    if (line_len == 0) {
      line_open = false;
      return;
    }
    char text[192];
    int wrote = format_capture_line(text, sizeof(text), line_ms, line_unix, line, line_len);
    if (wrote > 0) {
      size_t n = static_cast<size_t>(wrote);
      if (n >= sizeof(text)) {
        n = sizeof(text) - 1;
      }
      capture_write_line(text, n, line_len);
    }
    line_len = 0;
    line_open = false;
  };

  for (;;) {
    if (reconfigure) {
      reconfigure = false;
      flush_line();
      UartConfig cfg;
      settings_get_uart(&cfg);
      apply_uart(cfg);
    }
    if (!driver_installed) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    drain_overflows();
    int n = uart_read_bytes(static_cast<uart_port_t>(UART_PORT_NUM), batch, sizeof(batch), pdMS_TO_TICKS(15));
    uint32_t now = millis();
    if (n <= 0) {
      if (line_open && static_cast<uint32_t>(now - last_byte_ms) >= UART_GAP_MS) {
        flush_line();
      }
      continue;
    }
    if (line_open && static_cast<uint32_t>(now - last_byte_ms) >= UART_GAP_MS) {
      flush_line();
    }
    int64_t unix_ms = unix_ms_now();
    for (int i = 0; i < n; i++) {
      if (!line_open) {
        line_open = true;
        line_ms = now;
        line_unix = unix_ms;
      }
      line[line_len++] = batch[i];
      last_byte_ms = now;
      if (line_len >= UART_LINE_BYTES || batch[i] == '\n') {
        flush_line();
      }
    }
  }
}

void sniffer_reconfigure() {
  reconfigure = true;
}

void sniffer_start() {
  release_unused_tx_pin();
  xTaskCreate(sniffer_task, "sniffer", 8192, nullptr, 4, &sniffer_task_handle);
}
