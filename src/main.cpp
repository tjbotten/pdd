#include "capture.h"
#include "config.h"
#include "settings.h"
#include "sniffer.h"
#include "web_ui.h"
#include "wifi_mgr.h"

#include <Arduino.h>

static void led_off() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);  // active-low LED
}

static void led_poll() {
  bool recording = capture_recording();
  bool sta = wifi_sta_connected();
  uint32_t now = millis();
  bool on;
  if (recording) {
    on = ((now / 120) % 2) == 0;
  } else if (sta) {
    on = (now % 2000) < 70;
  } else {
    on = ((now / 400) % 2) == 0;
  }
  digitalWrite(PIN_LED, on ? LOW : HIGH);
}

void setup() {
  Serial.begin(115200);
  delay(400);
  led_off();
  Serial.println();
  Serial.println("zone-buffer serial sniffer (listen-only)");
  Serial.printf("RX GPIO%d. GPIO%d is not driven and must stay unconnected.\n",
                PIN_UART_RX, PIN_UART_TX_UNCONNECTED);
  settings_init();
  capture_init();
  sniffer_start();
  wifi_init();
  web_start();
  Serial.println("web server on port 80");
}

void loop() {
  web_loop();
  wifi_loop();
  capture_poll();
  led_poll();
  delay(2);
}
