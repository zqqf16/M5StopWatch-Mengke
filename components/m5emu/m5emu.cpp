#include "m5emu.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace m5emu {

Shared shared;
M5Canvas display;
Button BtnA{2}, BtnB{1}, BtnPWR{-1};
ImuData imu; Touch touch; Power power;

static int16_t spk_ring[SPK_LEN];
static int16_t *mic_ring;
static volatile uint32_t mic_wr = 0, mic_rd = 0;
static volatile bool pwr_down = false;
static int64_t rtc_offset_us = 0;  // epoch - boot

int64_t Button::heldMs() const { return pressed ? (esp_timer_get_time() - t_down) / 1000 : 0; }

static int hexv(char c){ return c<='9'? c-'0' : (c|32)-'a'+10; }

static void handle_line(char* l) {
  if (l[0] != '@') return;
  char* a = l + 1;
  if (!strncmp(a, "imu ", 4)) {
    sscanf(a + 4, "%f %f %f %f %f %f", &imu.ax, &imu.ay, &imu.az, &imu.gx, &imu.gy, &imu.gz);
  } else if (!strncmp(a, "tp ", 3)) {
    int d = 0; sscanf(a + 3, "%d %d %d", &touch.x, &touch.y, &d); touch.down = d;
  } else if (!strncmp(a, "bat ", 4)) {
    int c = 0, v = 0; sscanf(a + 4, "%d %d %d %d", &power.level, &power.mv, &c, &v);
    power.charging = c; power.vbus = v;
  } else if (!strncmp(a, "rtc ", 4)) {
    long long e = atoll(a + 4); rtc_offset_us = e * 1000000LL - esp_timer_get_time();
    struct timeval tv = { (time_t)e, 0 }; settimeofday(&tv, NULL);  // also drive the system clock (RX8130 model)
  } else if (!strncmp(a, "pwr ", 4)) {
    pwr_down = atoi(a + 4);
  } else if (!strncmp(a, "mic ", 4)) {   // hex-encoded int8 samples @ MIC_RATE
    for (char* p = a + 4; p[0] && p[1]; p += 2) {
      int8_t s = (int8_t)((hexv(p[0]) << 4) | hexv(p[1]));
      mic_ring[mic_wr % MIC_LEN] = (int16_t)(s * 256);
      mic_wr = mic_wr + 1;
    }
    if (mic_wr - mic_rd > MIC_LEN) mic_rd = mic_wr - MIC_LEN;
  }
}

static void uart_task(void*) {
  static char line[2048]; int n = 0; uint8_t buf[256];
  for (;;) {
    int r = uart_read_bytes(UART_NUM_0, buf, sizeof(buf), pdMS_TO_TICKS(20));
    for (int i = 0; i < r; i++) {
      char c = buf[i];
      if (c == '\n' || c == '\r') { line[n] = 0; if (n) handle_line(line); n = 0; }
      else if (n < (int)sizeof(line) - 1) line[n++] = c;
    }
  }
}

void begin() {
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << 1) | (1ULL << 2);
  io.mode = GPIO_MODE_INPUT; io.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io);

  display.setPsram(true);
  display.setColorDepth(16);
  display.createSprite(W, H);
  display.fillScreen(TFT_BLACK);

  mic_ring = (int16_t*)heap_caps_malloc(MIC_LEN * 2, MALLOC_CAP_SPIRAM);
  memset(spk_ring, 0, sizeof(spk_ring));

  shared.width = W; shared.height = H;
  shared.fb_addr = (uint32_t)display.getBuffer();
  shared.spk_addr = (uint32_t)spk_ring; shared.spk_len = SPK_LEN; shared.spk_rate = SPK_RATE;
  shared.spk_wr = 0; shared.vib = 0; shared.spk_en = 1; shared.brightness = 255;
  shared.version = 1;
  shared.magic = MAGIC;

  uart_driver_install(UART_NUM_0, 8192, 0, 0, NULL, 0);
  xTaskCreatePinnedToCore(uart_task, "m5emu_rx", 4096, NULL, 5, NULL, 0);
  printf("\nM5EMU:SHARED=0x%08lx FB=0x%08lx\n", (unsigned long)&shared, (unsigned long)shared.fb_addr);
}

void update() {
  int64_t now = esp_timer_get_time();
  Button* bs[] = {&BtnA, &BtnB, &BtnPWR};
  for (auto b : bs) {
    b->prev = b->pressed;
    b->pressed = b->gpio >= 0 ? gpio_get_level((gpio_num_t)b->gpio) == 0 : pwr_down;
    if (b->pressed && !b->prev) b->t_down = now;
  }
}

void present() { shared.frame_seq = shared.frame_seq + 1; }

time_t rtcNow() { return (time_t)((esp_timer_get_time() + rtc_offset_us) / 1000000LL); }

void playPCM(const int16_t* s, size_t n, uint32_t rate) {
  // naive resample to SPK_RATE
  uint32_t out_n = (uint64_t)n * SPK_RATE / rate;
  uint32_t w = shared.spk_wr;
  for (uint32_t i = 0; i < out_n; i++) spk_ring[(w + i) % SPK_LEN] = s[(uint64_t)i * rate / SPK_RATE];
  shared.spk_wr = w + out_n;
}

void tone(float hz, int ms, float vol) {
  uint32_t n = SPK_RATE * ms / 1000, w = shared.spk_wr;
  uint32_t period = (uint32_t)(SPK_RATE / hz); if (!period) period = 1;
  int16_t amp = (int16_t)(vol * 32767);
  for (uint32_t i = 0; i < n; i++) spk_ring[(w + i) % SPK_LEN] = ((i % period) < period / 2) ? amp : -amp;
  shared.spk_wr = w + n;
}

void vibrate(int s) { shared.vib = s; }

size_t micAvailable() { return mic_wr - mic_rd; }
size_t micRead(int16_t* dst, size_t max) {
  size_t n = 0;
  while (n < max && mic_rd != mic_wr) { dst[n++] = mic_ring[mic_rd % MIC_LEN]; mic_rd = mic_rd + 1; }
  return n;
}
}
