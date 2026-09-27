#pragma once
// RGB panel driven by ESP-IDF's esp_lcd, in one of two modes.
//
// Full: three 800x480 frame buffers in PSRAM. The panel scans out one while
// the next frame is drawn into another; buffers switch at vertical blank, so
// frames are never copied and never tear. Small bounce buffers in internal RAM
// feed the panel, so rendering traffic can't starve it.
//
// Half: three 400x240 buffers and no full-size frame buffer at all. The driver
// asks for each strip of lines just before sending it, and fillBounce scales
// the current 400x240 frame up 2x straight into that strip. That's a quarter
// of the pixels to draw and a quarter of the scanout traffic, so the panel can
// also refresh at 60 Hz.
//
// LovyanGFX is only used to draw text and JPEGs into the buffer being drawn.

#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

static constexpr int kPanelW = 800;
static constexpr int kPanelH = 480;
static constexpr int kFrameBuffers = 3;
static constexpr int kBacklightPin = 44;
// Refresh rate = pixel clock / (820 * 500): 16 MHz gives 39 Hz, 25 MHz gives 61 Hz.
static constexpr uint32_t kDefaultPclkHz = 16000000;
static constexpr uint32_t kHalfPclkHz = 25000000;
static constexpr uint32_t kMinPclkHz = 8000000;
static constexpr uint32_t kMaxPclkHz = 30000000;
static constexpr int kHSync = 4, kHBack = 8, kHFront = 8;
static constexpr int kVSync = 4, kVBack = 8, kVFront = 8;
static constexpr int kBounceLines = 10;

static esp_lcd_panel_handle_t panel = nullptr;
static bool displayHalf = false;
static int displayW = kPanelW;
static int displayH = kPanelH;
static uint16_t *frameBuffers[kFrameBuffers];
static uint16_t *halfBuffers[kFrameBuffers];
static int backIndex = 0;  // buffer being drawn now
static uint32_t pclkHz = kDefaultPclkHz;

// Full mode: vsyncCount when each buffer was handed to the panel.
static volatile uint32_t vsyncCount = 0;
static uint32_t presentedAt[kFrameBuffers];
// Half mode: buffer on screen, and the one waiting for the next frame to start.
static volatile int halfFront = 0;
static volatile int halfPending = -1;
// CPU cycles spent filling bounce buffers, for measuring its cost.
static volatile uint32_t bounceCycles = 0;

static bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  vsyncCount = vsyncCount + 1;
  return false;
}

static bool IRAM_ATTR fillBounce(esp_lcd_panel_handle_t, void *bounce, int pos_px, int len_bytes, void *) {
  if (pos_px == 0) {
    // A new frame starts: switch to the newest finished buffer.
    int pending = halfPending;
    if (pending >= 0) {
      halfFront = pending;
      halfPending = -1;
    }
    vsyncCount = vsyncCount + 1;
  }
  uint32_t start = esp_cpu_get_cycle_count();
  const uint16_t *low = halfBuffers[halfFront];
  int row = pos_px / kPanelW;
  int rows = len_bytes / (kPanelW * 2);
  // Rows come in pairs (the strip height is even), and both rows of a pair
  // show the same source row: read it once, two pixels at a time.
  uint32_t *out = (uint32_t *)bounce;
  for (int r = 0; r < rows; r += 2) {
    const uint32_t *src = (const uint32_t *)(low + ((row + r) >> 1) * (kPanelW / 2));
    uint32_t *o0 = out + r * (kPanelW / 2);
    uint32_t *o1 = o0 + kPanelW / 2;
    for (int x = 0; x < kPanelW / 4; ++x) {
      uint32_t two = src[x];
      uint32_t a = (two & 0xFFFF) * 0x10001u;
      uint32_t b = (two >> 16) * 0x10001u;
      o0[2 * x] = a;
      o0[2 * x + 1] = b;
      o1[2 * x] = a;
      o1[2 * x + 1] = b;
    }
  }
  bounceCycles += esp_cpu_get_cycle_count() - start;
  return false;
}

static float refreshRate() {
  return (float)pclkHz / ((kPanelW + kHSync + kHBack + kHFront) * (kPanelH + kVSync + kVBack + kVFront));
}

static bool startDisplay(bool half, uint32_t pclk) {
  if (panel) {
    esp_lcd_panel_del(panel);
    panel = nullptr;
  }
  displayHalf = half;
  displayW = half ? kPanelW / 2 : kPanelW;
  displayH = half ? kPanelH / 2 : kPanelH;
  pclkHz = pclk;
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_PLL240M;
  cfg.timings.pclk_hz = pclk;
  cfg.timings.h_res = kPanelW;
  cfg.timings.v_res = kPanelH;
  cfg.timings.hsync_pulse_width = kHSync;
  cfg.timings.hsync_back_porch = kHBack;
  cfg.timings.hsync_front_porch = kHFront;
  cfg.timings.vsync_pulse_width = kVSync;
  cfg.timings.vsync_back_porch = kVBack;
  cfg.timings.vsync_front_porch = kVFront;
  // Same signal polarities LovyanGFX used for this panel.
  cfg.timings.flags.hsync_idle_low = 1;
  cfg.timings.flags.vsync_idle_low = 1;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = half ? 0 : kFrameBuffers;
  cfg.bounce_buffer_size_px = kPanelW * kBounceLines;
  cfg.dma_burst_size = 64;
  cfg.hsync_gpio_num = 39;
  cfg.vsync_gpio_num = 41;
  cfg.de_gpio_num = 40;
  cfg.pclk_gpio_num = 42;
  cfg.disp_gpio_num = -1;
  // B0-B4, G0-G5, R0-R4.
  const int pins[16] = {8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14};
  for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = pins[i];
  cfg.flags.fb_in_psram = 1;
  cfg.flags.no_fb = half ? 1 : 0;
  if (esp_lcd_new_rgb_panel(&cfg, &panel) != ESP_OK) return false;
  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  if (half) cbs.on_bounce_empty = fillBounce;
  else cbs.on_vsync = onVsync;
  esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr);

  for (int i = 0; i < kFrameBuffers; ++i) presentedAt[i] = 0;
  if (half) {
    for (int i = 0; i < kFrameBuffers; ++i) {
      if (!halfBuffers[i]) {
        halfBuffers[i] = (uint16_t *)heap_caps_malloc(kPanelW / 2 * kPanelH / 2 * 2, MALLOC_CAP_SPIRAM);
        if (!halfBuffers[i]) return false;
      }
      memset(halfBuffers[i], 0, kPanelW / 2 * kPanelH / 2 * 2);
    }
    halfFront = 0;
    halfPending = -1;
  }
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  if (!half) {
    void *fbs[kFrameBuffers];
    esp_lcd_rgb_panel_get_frame_buffer(panel, kFrameBuffers, &fbs[0], &fbs[1], &fbs[2]);
    for (int i = 0; i < kFrameBuffers; ++i) {
      frameBuffers[i] = (uint16_t *)fbs[i];
      memset(frameBuffers[i], 0, kPanelW * kPanelH * sizeof(uint16_t));
    }
    esp_lcd_panel_draw_bitmap(panel, 0, 0, kPanelW, kPanelH, frameBuffers[0]);
  }
  backIndex = 1;
  pinMode(kBacklightPin, OUTPUT);
  digitalWrite(kBacklightPin, HIGH);
  return true;
}

static bool setPixelClock(uint32_t hz) {
  if (hz < kMinPclkHz || hz > kMaxPclkHz) return false;
  if (esp_lcd_rgb_panel_set_pclk(panel, hz) != ESP_OK) return false;
  pclkHz = hz;
  return true;
}

// Waits until the back buffer is no longer on screen or queued, and returns it.
static uint16_t *beginFrame() {
  uint32_t deadline = millis() + 100;
  if (displayHalf) {
    while ((backIndex == halfFront || backIndex == halfPending) && (int32_t)(deadline - millis()) > 0) vTaskDelay(1);
    return halfBuffers[backIndex];
  }
  // In full mode the back buffer was on screen until the buffer after it was
  // presented and a vsync passed.
  int next = (backIndex + 1) % kFrameBuffers;
  while ((int32_t)(vsyncCount - presentedAt[next]) <= 0 && (int32_t)(deadline - millis()) > 0) vTaskDelay(1);
  return frameBuffers[backIndex];
}

// Hands the back buffer to the panel; it goes on screen at the next frame.
static void presentFrame() {
  if (displayHalf) {
    // Wait for the previous frame to reach the screen, so frames are never skipped.
    uint32_t deadline = millis() + 100;
    while (halfPending >= 0 && (int32_t)(deadline - millis()) > 0) vTaskDelay(1);
    halfPending = backIndex;
  } else {
    presentedAt[backIndex] = vsyncCount;
    esp_lcd_panel_draw_bitmap(panel, 0, 0, kPanelW, kPanelH, frameBuffers[backIndex]);
  }
  backIndex = (backIndex + 1) % kFrameBuffers;
}
