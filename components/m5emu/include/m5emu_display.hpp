// Generic M5GFX display driver for esp-emulator: an LGFX panel whose framebuffer
// IS the m5emu shared framebuffer. Any code that draws through M5GFX/LovyanGFX
// (including an LVGL flush_cb that calls writePixels) ends up on the web page.
// Use it in place of a real panel class (e.g. CO5300 / Panel_AMOLED).
#pragma once
#include "m5emu.h"
#include "esp_heap_caps.h"
#include <lgfx/v1/panel/Panel_FrameBufferBase.hpp>

namespace m5emu {

class Panel_M5Emu : public lgfx::Panel_FrameBufferBase {
public:
  Panel_M5Emu() {
    _cfg.memory_width = _cfg.panel_width = W;
    _cfg.memory_height = _cfg.panel_height = H;
    _write_depth = lgfx::color_depth_t::rgb565_2Byte;
    _read_depth  = lgfx::color_depth_t::rgb565_2Byte;
  }
  bool init(bool use_reset) override {
    if (!_lines) {
      _lines = (uint8_t**)heap_caps_malloc(H * sizeof(void*), MALLOC_CAP_INTERNAL);
      uint8_t* fb = (uint8_t*)m5emu::display.getBuffer();
      if (!_lines || !fb) return false;
      for (int i = 0; i < H; i++) _lines[i] = fb + i * W * 2;
    }
    _lines_buffer = _lines;
    bool ok = Panel_FrameBufferBase::init(false);
    _auto_display = true;   // endWrite() -> display() -> present()
    return ok;
  }
  void display(uint_fast16_t, uint_fast16_t, uint_fast16_t, uint_fast16_t) override { m5emu::present(); }
  void setBrightness(uint8_t b) override { m5emu::shared.brightness = b; }
private:
  uint8_t** _lines = nullptr;
};

class M5EmuDisplay : public M5GFX {
  Panel_M5Emu _panel;
public:
  bool init_impl(bool, bool use_clear) override {
    setPanel(&_panel);
    if (!LGFX_Device::init_impl(false, use_clear)) return false;
    return true;
  }
  void setBrightness(uint8_t b) { _panel.setBrightness(b); }
};

}  // namespace m5emu
