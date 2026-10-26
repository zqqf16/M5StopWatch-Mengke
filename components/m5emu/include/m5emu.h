// m5emu: tiny HAL that lets StopWatch firmware run in esp-emulator (WASM/native).
// esp-emulator has no CO5300/QSPI panel, no I2S, and no BMI270/RX8130/M5PM1/CST820
// models, and its JS API cannot observe SPI/I2C/I2S traffic. So:
//   * Display  -> 466x466 RGB565 framebuffer in PSRAM, read by the web page via read_memory()
//   * Speaker  -> int16 PCM ring buffer in RAM, read by the page and played with WebAudio
//   * Buttons  -> REAL GPIO1 (KEYB) / GPIO2 (KEYA), driven by the page via gpio_set_input()
//   * IMU/RTC/battery/touch/power-key/mic -> values pushed by the page over UART0 RX ("@..." lines)
// The page finds the shared block from the boot line "M5EMU:SHARED=0x........".
#pragma once
#include <stdint.h>
#include <M5GFX.h>

namespace m5emu {

static constexpr uint32_t MAGIC = 0x4D354555; // 'UE5M'
static constexpr int W = 466, H = 466;
static constexpr uint32_t SPK_RATE = 16000, SPK_LEN = 32768;   // samples
static constexpr uint32_t MIC_RATE = 8000,  MIC_LEN = 32768;

struct Shared {               // layout is read by web/emu-worker.js - keep in sync
  uint32_t magic;             // 0
  uint32_t version;           // 4
  uint32_t width, height;     // 8,12
  uint32_t fb_addr;           // 16  RGB565 big-endian (LGFX sprite native)
  volatile uint32_t frame_seq;// 20
  uint32_t spk_addr;          // 24  int16 mono ring
  uint32_t spk_len;           // 28
  uint32_t spk_rate;          // 32
  volatile uint32_t spk_wr;   // 36  total samples ever written
  volatile uint32_t vib;      // 40  vibration motor 0..255
  volatile uint32_t spk_en;   // 44  AW8737A enable
  volatile uint32_t brightness;//48
};
extern Shared shared;

struct Button {
  int gpio; bool pressed = false, prev = false; int64_t t_down = 0;
  bool wasPressed() const { return pressed && !prev; }
  bool wasReleased() const { return !pressed && prev; }
  bool isPressed() const { return pressed; }
  int64_t heldMs() const;
};
struct ImuData { float ax=0, ay=0, az=1, gx=0, gy=0, gz=0; };
struct Touch { int x=0, y=0; bool down=false; };
struct Power { int level=80; int mv=3900; bool charging=false; bool vbus=true; };

extern M5Canvas display;     // draw here, then call present()
extern Button BtnA, BtnB, BtnPWR;
extern ImuData imu;
extern Touch touch;
extern Power power;

void begin();
void update();                       // poll buttons / process UART input
void present();                      // publish a frame
time_t rtcNow();                     // RX8130CE model (set from the host clock)
void tone(float hz, int ms, float vol = 0.4f);
void playPCM(const int16_t* s, size_t n, uint32_t rate);
void vibrate(int strength);
size_t micRead(int16_t* dst, size_t max);  // pull mic samples (MIC_RATE)
size_t micAvailable();
}
