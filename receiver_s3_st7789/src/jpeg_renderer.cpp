// =============================================================================
//  jpeg_renderer.cpp  -  LovyanGFX ST7789 device definition + JPEG blit
// -----------------------------------------------------------------------------
//  Why LovyanGFX (over TFT_eSPI + TJpg_Decoder)?
//    * Panel pins are configured IN CODE here - no editing of a global library
//      User_Setup.h, so the build is self-contained and reproducible.
//    * It has a fast built-in JPEG decoder (drawJpg) that writes through a DMA
//      SPI bus, which is exactly our hot path (full-screen frame every cycle).
//    * Sprites/DMA are first-class, which the overlay uses for flicker-free UI.
//  All ST7789 pins come from pins.h and are the mandatory, invariant set.
// =============================================================================
#include "jpeg_renderer.h"
#include "display.h"
#include "config.h"
#include "pins.h"
#include "app_state.h"
#include <Arduino.h>

// ---- Concrete LovyanGFX device for our exact panel --------------------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
public:
  LGFX() {
    {  // SPI bus
      auto c = _bus.config();
      c.spi_host    = SPI2_HOST;          // ESP32-S3 general purpose SPI
      c.spi_mode    = 0;
      c.freq_write  = DISPLAY_SPI_HZ;
      c.freq_read   = 16000000;
      c.spi_3wire   = true;
      c.use_lock    = true;
      c.dma_channel = SPI_DMA_CH_AUTO;    // enable DMA for fast pushImage
      c.pin_sclk    = TFT_SCLK;
      c.pin_mosi    = TFT_MOSI;
      c.pin_miso    = TFT_MISO;           // -1 (ST7789 has no MISO)
      c.pin_dc      = TFT_DC;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {  // Panel
      auto c = _panel.config();
      c.pin_cs          = TFT_CS;
      c.pin_rst         = TFT_RST;
      c.pin_busy        = -1;
      c.panel_width     = DISPLAY_W;
      c.panel_height    = DISPLAY_H;
      c.offset_x        = 0;
      c.offset_y        = 0;
      c.offset_rotation = 0;
      c.readable        = false;          // 1-wire, no readback needed
      c.invert          = true;           // ST7789 panels need colour inversion
      c.rgb_order       = false;
      c.dlen_16bit      = false;
      c.bus_shared      = false;
      _panel.config(c);
    }
    {  // Backlight (PWM so we could dim later if desired)
      auto c = _light.config();
      c.pin_bl      = TFT_BL;
      c.invert      = false;
      c.freq        = 12000;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

static LGFX s_lcd;
LGFX_Device& display() { return s_lcd; }

// -----------------------------------------------------------------------------
void display_begin() {
  s_lcd.init();
  s_lcd.setRotation(DISPLAY_ROTATION);
  s_lcd.setColorDepth(16);
  // Paint black BEFORE enabling the backlight so the user never sees the random
  // power-on noise of the panel RAM.
  s_lcd.fillScreen(0x0000);
  display_backlight(true);
  LOGI("ST7789 init OK (%dx%d rot=%d)", DISPLAY_W, DISPLAY_H, DISPLAY_ROTATION);
}

void display_backlight(bool on) {
  s_lcd.setBrightness(on ? 255 : 0);
}

// -----------------------------------------------------------------------------
//  Decode + blit. Native 240x240 draws 1:1; QVGA (320x240) is centre-cropped
//  by telling drawJpg to offset into the source image. drawJpg streams blocks
//  straight to the panel via DMA, so there is no full-frame RAM buffer.
// -----------------------------------------------------------------------------
bool jpeg_draw(const uint8_t* data, uint32_t len) {
  if (!data || len < 4) return false;

  // Centre-crop offset derived from the configured source size (config.h).
  // For the default native 240x240 source both offsets are 0 (no crop).
  int32_t offX = (VIDEO_SRC_W > DISPLAY_W) ? (VIDEO_SRC_W - DISPLAY_W) / 2 : 0;
  int32_t offY = (VIDEO_SRC_H > DISPLAY_H) ? (VIDEO_SRC_H - DISPLAY_H) / 2 : 0;

  bool ok = s_lcd.drawJpg(data, len,
                          /*x*/0, /*y*/0,
                          /*maxWidth*/DISPLAY_W, /*maxHeight*/DISPLAY_H,
                          /*offX*/offX, /*offY*/offY);
  if (!ok) {
    LOGV("drawJpg failed (corrupt frame)");
    g_rx.frames_dropped++;
  }
  return ok;
}
