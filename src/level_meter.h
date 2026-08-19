#pragma once

#include <lvgl.h>

#include <array>
#include <cstddef>
#include <cstdint>

class StereoLevelMeter {
 public:
  static constexpr size_t kSegmentCount = 20;

  lv_obj_t *create(lv_obj_t *parent, int32_t width, int32_t height);
  void update(uint16_t left_peak, uint16_t right_peak, uint32_t elapsed_ms,
              bool playing);

 private:
  struct ChannelState {
    float displayed_db = -60.0f;
    float peak_db = -60.0f;
    uint32_t peak_hold_ms = 0;
    int32_t lit_segments = 0;
    int32_t peak_segment = -1;
  };

  struct BarContext {
    StereoLevelMeter *owner = nullptr;
    size_t channel = 0;
  };

  static void barDrawCallback(lv_event_t *event);
  static float peakToDb(uint16_t peak);
  static int32_t segmentForDb(float db);
  void drawBarRow(lv_event_t *event, size_t channel) const;
  void updateChannel(size_t channel, uint16_t sample_peak,
                     uint32_t elapsed_ms, bool playing);

  lv_obj_t *root_ = nullptr;
  std::array<lv_obj_t *, 2> bar_rows_{};
  std::array<BarContext, 2> bar_contexts_{};
  std::array<ChannelState, 2> channels_{};
  int32_t segment_start_x_ = 0;
  int32_t segment_width_ = 0;
  int32_t segment_gap_ = 3;
  int32_t bar_width_ = 0;
  std::array<int32_t, 2> row_y_{};
};
