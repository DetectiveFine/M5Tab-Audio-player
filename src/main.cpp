#include <Arduino.h>
#include <M5Unified.h>
#include <SD_MMC.h>
#include <lvgl.h>

#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

#include <cstddef>
#include <cstdint>

#include "audio_player.h"
#include "ui_player.h"

namespace {

constexpr int32_t kScreenWidth = 1280;
constexpr int32_t kScreenHeight = 720;
constexpr int32_t kNativeWidth = 720;
constexpr int32_t kNativeHeight = 1280;
constexpr uint32_t kLvglMaxSleepMs = 16;
constexpr uint32_t kPpaCompletionTimeoutMs = 50;
// The P4 SRM engine works in 18x18 macro blocks. Tiny LVGL invalidation
// rectangles (borders, cursors) are faster and safer in software.
constexpr int32_t kPpaMinBlockDimension = 18;
// About 5.6% of a frame. This is still a small partial buffer, but large
// enough to render the 240x180 demo panel in one chunk while scrolling.
constexpr int32_t kDrawBufferLines = 40;
constexpr size_t kDrawBufferBytes =
    static_cast<size_t>(kScreenWidth) * kDrawBufferLines * sizeof(uint16_t);
constexpr size_t kNativeFramebufferBytes =
    static_cast<size_t>(kNativeWidth) * kNativeHeight * sizeof(uint16_t);
static_assert(kNativeWidth == kScreenHeight &&
                  kNativeHeight == kScreenWidth,
              "Logical and native display dimensions are not rotation-compatible.");
static_assert(kNativeFramebufferBytes % 64 == 0,
              "The DSI framebuffer size must be cache-line aligned.");

uint16_t *g_lvgl_draw_buffer = nullptr;
void *g_lcd_framebuffer = nullptr;
ppa_client_handle_t g_srm_client = nullptr;
SemaphoreHandle_t g_ppa_done_semaphore = nullptr;
esp_lcd_panel_handle_t g_dsi_panel_handle = nullptr;
SemaphoreHandle_t g_dsi_refresh_semaphore = nullptr;
bool g_dsi_refresh_sync_enabled = false;
bool g_flush_sequence_active = false;
bool g_ppa_acceleration_enabled = true;
uint8_t *g_cpu_dirty_begin = nullptr;
uint8_t *g_cpu_dirty_end = nullptr;
AudioPlayer g_audio_player;
UiPlayer g_ui_player;

// M5GFX does not expose Panel_DSI's esp_lcd handle publicly. A protected
// pointer-to-member lets the display driver access it without an unsafe cast.
struct PanelDsiHandleAccess : public lgfx::Panel_DSI {
  static esp_lcd_panel_handle_t get(lgfx::Panel_DSI *panel) {
    auto member = &PanelDsiHandleAccess::_disp_panel_handle;
    return panel->*member;
  }
};

[[noreturn]] void fatalError(const char *message) {
  Serial.printf("FATAL: %s\n", message);
  while (true) {
    delay(1000);
  }
}

bool IRAM_ATTR onDsiRefreshDone(esp_lcd_panel_handle_t,
                                esp_lcd_dpi_panel_event_data_t *, void *) {
  BaseType_t high_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(g_dsi_refresh_semaphore, &high_task_woken);
  return high_task_woken == pdTRUE;
}

bool IRAM_ATTR onPpaTransactionDone(ppa_client_handle_t, ppa_event_data_t *,
                                    void *user_data) {
  auto semaphore = static_cast<SemaphoreHandle_t>(user_data);
  if (semaphore == nullptr) {
    return false;
  }
  BaseType_t high_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(semaphore, &high_task_woken);
  return high_task_woken == pdTRUE;
}

void registerDsiRefreshSync(lgfx::Panel_DSI *panel) {
  g_dsi_refresh_semaphore = xSemaphoreCreateBinary();
  if (g_dsi_refresh_semaphore == nullptr) {
    fatalError("Out of memory for DSI synchronization.");
  }

  esp_lcd_dpi_panel_event_callbacks_t callbacks = {};
  callbacks.on_refresh_done = onDsiRefreshDone;
  g_dsi_panel_handle = PanelDsiHandleAccess::get(panel);
  const esp_err_t result = esp_lcd_dpi_panel_register_event_callbacks(
      g_dsi_panel_handle, &callbacks, nullptr);
  if (result != ESP_OK) {
    Serial.printf("DSI callback registration failed: %s\n",
                  esp_err_to_name(result));
    fatalError("Cannot register the DSI VSYNC callback.");
  }

  g_dsi_refresh_sync_enabled = true;
}

void waitForDsiRefresh() {
  if (!g_dsi_refresh_sync_enabled) {
    return;
  }

  while (xSemaphoreTake(g_dsi_refresh_semaphore, 0) == pdTRUE) {
  }
  (void)xSemaphoreTake(g_dsi_refresh_semaphore, pdMS_TO_TICKS(25));
}

void registerPpaSrmClient() {
  g_ppa_done_semaphore = xSemaphoreCreateBinary();
  if (g_ppa_done_semaphore == nullptr) {
    g_ppa_acceleration_enabled = false;
    Serial.println("PPA completion semaphore unavailable; using CPU rotation.");
    return;
  }

  ppa_client_config_t config = {};
  config.oper_type = PPA_OPERATION_SRM;
  config.max_pending_trans_num = 1;
  config.data_burst_length = PPA_DATA_BURST_LENGTH_128;
  const esp_err_t register_result =
      ppa_register_client(&config, &g_srm_client);
  if (register_result != ESP_OK) {
    g_ppa_acceleration_enabled = false;
    Serial.printf("PPA unavailable (%s); using CPU rotation.\n",
                  esp_err_to_name(register_result));
    vSemaphoreDelete(g_ppa_done_semaphore);
    g_ppa_done_semaphore = nullptr;
    return;
  }

  ppa_event_callbacks_t callbacks = {};
  callbacks.on_trans_done = onPpaTransactionDone;
  const esp_err_t callback_result =
      ppa_client_register_event_callbacks(g_srm_client, &callbacks);
  if (callback_result != ESP_OK) {
    g_ppa_acceleration_enabled = false;
    Serial.printf("PPA callback unavailable (%s); using CPU rotation.\n",
                  esp_err_to_name(callback_result));
    (void)ppa_unregister_client(g_srm_client);
    g_srm_client = nullptr;
    vSemaphoreDelete(g_ppa_done_semaphore);
    g_ppa_done_semaphore = nullptr;
  }
}

esp_err_t rotateAreaIntoFramebufferWithPpa(const void *logical_pixels,
                                            const lv_area_t *area) {
  const int32_t width = lv_area_get_width(area);
  const int32_t height = lv_area_get_height(area);
  // Rotation 1 maps logical (x,y) to native (719-y,x).
  const int32_t native_x = kNativeWidth - 1 - area->y2;
  const int32_t native_y = area->x1;

  ppa_srm_oper_config_t operation = {};
  operation.in.buffer = logical_pixels;
  operation.in.pic_w = width;
  operation.in.pic_h = height;
  operation.in.block_w = width;
  operation.in.block_h = height;
  operation.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  operation.out.buffer = g_lcd_framebuffer;
  operation.out.buffer_size = kNativeFramebufferBytes;
  operation.out.pic_w = kNativeWidth;
  operation.out.pic_h = kNativeHeight;
  operation.out.block_offset_x = native_x;
  operation.out.block_offset_y = native_y;
  operation.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  operation.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;
  operation.scale_x = 1.0f;
  operation.scale_y = 1.0f;
  operation.mode = PPA_TRANS_MODE_NON_BLOCKING;
  operation.user_data = g_ppa_done_semaphore;

  while (xSemaphoreTake(g_ppa_done_semaphore, 0) == pdTRUE) {
  }
  const esp_err_t start_result =
      ppa_do_scale_rotate_mirror(g_srm_client, &operation);
  if (start_result != ESP_OK) {
    return start_result;
  }
  return xSemaphoreTake(g_ppa_done_semaphore,
                        pdMS_TO_TICKS(kPpaCompletionTimeoutMs)) == pdTRUE
             ? ESP_OK
             : ESP_ERR_TIMEOUT;
}

void getNativeDirtySpan(const lv_area_t *area, uint8_t *&begin,
                        uint8_t *&end) {
  const int32_t width = lv_area_get_width(area);
  const int32_t height = lv_area_get_height(area);
  const int32_t native_x = kNativeWidth - 1 - area->y2;
  const size_t first_pixel =
      static_cast<size_t>(area->x1) * kNativeWidth + native_x;
  const size_t span_pixels =
      static_cast<size_t>(width - 1) * kNativeWidth + height;
  begin = static_cast<uint8_t *>(g_lcd_framebuffer) +
          first_pixel * sizeof(uint16_t);
  end = begin + span_pixels * sizeof(uint16_t);
}

void rememberCpuDirtySpan(const lv_area_t *area) {
  uint8_t *begin = nullptr;
  uint8_t *end = nullptr;
  getNativeDirtySpan(area, begin, end);
  if (g_cpu_dirty_begin == nullptr || begin < g_cpu_dirty_begin) {
    g_cpu_dirty_begin = begin;
  }
  if (g_cpu_dirty_end == nullptr || end > g_cpu_dirty_end) {
    g_cpu_dirty_end = end;
  }
}

void syncCpuDirtySpan() {
  if (g_cpu_dirty_begin == nullptr || g_cpu_dirty_end <= g_cpu_dirty_begin) {
    g_cpu_dirty_begin = nullptr;
    g_cpu_dirty_end = nullptr;
    return;
  }

  const esp_err_t result = esp_cache_msync(
      g_cpu_dirty_begin,
      static_cast<size_t>(g_cpu_dirty_end - g_cpu_dirty_begin),
      ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA |
          ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  if (result != ESP_OK) {
    Serial.printf("Framebuffer cache sync warning: %s\n",
                  esp_err_to_name(result));
  }
  g_cpu_dirty_begin = nullptr;
  g_cpu_dirty_end = nullptr;
}

void rotateAreaIntoFramebufferWithCpu(const uint16_t *logical_pixels,
                                      const lv_area_t *area) {
  const int32_t width = lv_area_get_width(area);
  const int32_t height = lv_area_get_height(area);
  auto *framebuffer = static_cast<uint16_t *>(g_lcd_framebuffer);

  // Rotation 1 maps logical (x,y) to native (719-y,x). Iterate in native
  // scan-line order so the fallback writes contiguous framebuffer pixels.
  for (int32_t source_x = 0; source_x < width; ++source_x) {
    uint16_t *destination =
        framebuffer + (area->x1 + source_x) * kNativeWidth +
        (kNativeWidth - 1 - area->y1);
    const uint16_t *source = logical_pixels + source_x;
    for (int32_t source_y = 0; source_y < height; ++source_y) {
      *destination-- = *source;
      source += width;
    }
  }
}

void lvglFlush(lv_display_t *display, const lv_area_t *area,
               uint8_t *pixel_map) {
  const int32_t width = lv_area_get_width(area);
  const int32_t height = lv_area_get_height(area);
  const size_t area_bytes =
      static_cast<size_t>(width) * height * sizeof(uint16_t);
  const bool valid_area = area->x1 >= 0 && area->y1 >= 0 &&
                          area->x2 < kScreenWidth &&
                          area->y2 < kScreenHeight &&
                          width > 0 && height > 0 &&
                          area_bytes <= kDrawBufferBytes;
  if (!valid_area) {
    static uint32_t invalid_area_count = 0;
    if (invalid_area_count < 8) {
      Serial.printf("Skipped invalid LVGL area #%lu: (%ld,%ld)-(%ld,%ld)\n",
                    static_cast<unsigned long>(invalid_area_count + 1),
                    static_cast<long>(area->x1),
                    static_cast<long>(area->y1),
                    static_cast<long>(area->x2),
                    static_cast<long>(area->y2));
    }
    ++invalid_area_count;
    if (lv_display_flush_is_last(display)) {
      syncCpuDirtySpan();
      g_flush_sequence_active = false;
    }
    lv_display_flush_ready(display);
    return;
  }

  // Synchronize only the first dirty rectangle of an LVGL refresh. Waiting
  // for VSYNC before every rectangle would limit a multi-part refresh badly.
  if (!g_flush_sequence_active) {
    // A previous interrupted refresh must never leave CPU-written cache lines
    // pending before a new sequence begins.
    syncCpuDirtySpan();
    waitForDsiRefresh();
    g_flush_sequence_active = true;
  }

  const bool ppa_safe_size = width >= kPpaMinBlockDimension &&
                             height >= kPpaMinBlockDimension;
  if (g_ppa_acceleration_enabled && ppa_safe_size) {
    const esp_err_t ppa_result =
        rotateAreaIntoFramebufferWithPpa(pixel_map, area);
    if (ppa_result != ESP_OK) {
      // The IDF PPA driver can wait forever after an SRM parameter fault.
      // Non-blocking mode plus our finite completion wait guarantees that the
      // LVGL task survives; use software rotation for this and later frames.
      g_ppa_acceleration_enabled = false;
      Serial.printf(
          "PPA disabled after %s at area=(%ld,%ld)-(%ld,%ld); CPU fallback.\n",
          esp_err_to_name(ppa_result), static_cast<long>(area->x1),
          static_cast<long>(area->y1), static_cast<long>(area->x2),
          static_cast<long>(area->y2));
      rotateAreaIntoFramebufferWithCpu(
          reinterpret_cast<const uint16_t *>(pixel_map), area);
      rememberCpuDirtySpan(area);
    }
  } else {
    rotateAreaIntoFramebufferWithCpu(
        reinterpret_cast<const uint16_t *>(pixel_map), area);
    rememberCpuDirtySpan(area);
    if (g_ppa_acceleration_enabled) {
      // A later PPA transaction may invalidate overlapping cache lines, so
      // commit tiny software-rendered rectangles immediately while PPA is on.
      syncCpuDirtySpan();
    }
  }

  if (lv_display_flush_is_last(display)) {
    syncCpuDirtySpan();
    g_flush_sequence_active = false;
  }

  // The PPA completion semaphore (or CPU copy) guarantees pixel_map is no
  // longer in use before LVGL reuses its single draw buffer.
  lv_display_flush_ready(display);
}

void lvglTouchRead(lv_indev_t *, lv_indev_data_t *data) {
  if (M5.Touch.getCount() == 0) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  const auto &touch = M5.Touch.getDetail(0);
  data->point.x = touch.x;
  data->point.y = touch.y;
  data->state = touch.isPressed() ? LV_INDEV_STATE_PRESSED
                                  : LV_INDEV_STATE_RELEASED;
}

uint32_t lvglTickMilliseconds() { return millis(); }

void allocateFramebuffers() {
  // Prefer internal SRAM for CPU rendering; fall back to PSRAM only if the
  // contiguous internal block is unavailable.
  g_lvgl_draw_buffer = static_cast<uint16_t *>(heap_caps_aligned_alloc(
      64, kDrawBufferBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (g_lvgl_draw_buffer == nullptr) {
    g_lvgl_draw_buffer = static_cast<uint16_t *>(heap_caps_aligned_alloc(
        64, kDrawBufferBytes, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM));
  }
  if (g_lvgl_draw_buffer == nullptr) {
    fatalError("Cannot allocate the LVGL partial draw buffer.");
  }
}

void initializeHardware() {
  auto config = M5.config();
  config.clear_display = true;
  config.internal_mic = false;
  config.internal_spk = true;
  M5.begin(config);

  if (M5.getBoard() != m5::board_t::board_M5Tab5) {
    fatalError("The detected board is not an M5Stack Tab5.");
  }

  M5.Display.setRotation(1);
  M5.Display.setBrightness(190);
  if (M5.Display.width() != kScreenWidth ||
      M5.Display.height() != kScreenHeight) {
    fatalError("Unexpected display resolution.");
  }

  auto *panel = static_cast<lgfx::Panel_DSI *>(M5.Display.getPanel());
  const auto &panel_config = panel->config_detail();
  g_lcd_framebuffer = panel_config.buffer;
  if (g_lcd_framebuffer == nullptr ||
      (reinterpret_cast<uintptr_t>(g_lcd_framebuffer) & 63U) != 0) {
    fatalError("The DSI framebuffer is unavailable or incorrectly aligned.");
  }

  allocateFramebuffers();
  registerDsiRefreshSync(panel);
  registerPpaSrmClient();
  M5.Display.startWrite();

  // Keep M5Unified's detected Tab5 I2S pins and ES8388 callback. Only task
  // scheduling is tuned so the codec DMA service preempts PCM generation.
  auto speaker_config = M5.Speaker.config();
  // Preserve left/right channels all the way through the ES8388. Start at the
  // most common music rate; AudioOutput switches this to each track's native
  // rate before its first PCM sample, avoiding linear sample-rate conversion.
  speaker_config.stereo = true;
  speaker_config.sample_rate = 44100;
  speaker_config.task_priority = 6;
  speaker_config.task_pinned_core = 0;
  M5.Speaker.config(speaker_config);
  if (!M5.Speaker.begin()) {
    fatalError("Cannot start the ES8388 / I2S audio output.");
  }
}

bool initializeSdCard() {
  // Tab5 uses ESP32-P4 SDMMC slot 0: CLK 43, CMD 44, D0..D3 39..42,
  // powered by the on-chip LDO channel 4. These are also the selected
  // esp32-p4-evboard Arduino variant defaults, but setting them explicitly
  // makes a mismatched board package fail early instead of silently.
  if (!SD_MMC.setPins(43, 44, 39, 40, 41, 42)) {
    Serial.println("SDMMC pin configuration rejected.");
    return false;
  }
#if defined(SOC_SDMMC_IO_POWER_EXTERNAL)
  if (!SD_MMC.setPowerChannel(4)) {
    Serial.println("SDMMC LDO configuration rejected.");
    return false;
  }
#endif
  if (!SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_HIGHSPEED, 16)) {
    Serial.println("SD card mount failed.");
    return false;
  }
  return true;
}

void initializeLvgl() {
  lv_init();
  lv_tick_set_cb(lvglTickMilliseconds);

  lv_display_t *display = lv_display_create(kScreenWidth, kScreenHeight);
  if (display == nullptr) {
    fatalError("Cannot create the LVGL display.");
  }
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, g_lvgl_draw_buffer, nullptr,
                         kDrawBufferBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, lvglFlush);

  lv_indev_t *touch = lv_indev_create();
  if (touch == nullptr) {
    fatalError("Cannot create the LVGL touch input device.");
  }
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, lvglTouchRead);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  initializeHardware();
  initializeLvgl();
  const bool sd_mounted = initializeSdCard();
  if (!g_audio_player.begin(&SD_MMC, sd_mounted)) {
    fatalError("Cannot start the audio player task.");
  }
  g_ui_player.begin(g_audio_player);
}

void loop() {
  M5.update();
  uint32_t wait_ms = lv_timer_handler();
  if (wait_ms == 0) {
    wait_ms = 1;
  } else if (wait_ms > kLvglMaxSleepMs) {
    wait_ms = kLvglMaxSleepMs;
  }
  delay(wait_ms);
}
