#include "level_meter.h"

#include <algorithm>
#include <cmath>

#include "ui_theme.h"

namespace {

constexpr std::array<float, StereoLevelMeter::kSegmentCount> kThresholds = {
    -48.0f, -42.0f, -36.0f, -30.0f, -24.0f, -20.0f, -16.0f,
    -12.0f, -10.0f, -8.0f,  -6.0f,  -5.0f,  -4.0f,  -3.0f,
    -2.0f,  -1.5f,  -1.0f,  -0.5f,  -0.1f,  0.0f};

constexpr size_t kRedStart = 17;

}  // namespace

lv_obj_t *StereoLevelMeter::create(lv_obj_t *parent, int32_t width,
                                   int32_t height) {
  root_ = lv_obj_create(parent);
  lv_obj_set_size(root_, width, height);
  UiTheme::panel(root_);
  lv_obj_set_style_pad_all(root_, 10, 0);
  lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *caption = lv_label_create(root_);
  lv_label_set_text(caption, "STEREO LEVEL");
  lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
  UiTheme::technicalLabel(caption);
  lv_obj_set_pos(caption, 0, 0);

  static constexpr const char *kScaleText[] = {"-40", "-20", "-10", "-5",
                                                "-3",  "-1",  "0"};
  static constexpr int32_t kScaleSegment[] = {1, 5, 8, 11, 13, 16, 19};

  segment_start_x_ = 22;
  const int32_t usable = width - 20 - segment_start_x_;
  segment_width_ =
      (usable - static_cast<int32_t>(kSegmentCount - 1) * segment_gap_) /
      static_cast<int32_t>(kSegmentCount);
  bar_width_ = static_cast<int32_t>(kSegmentCount) * segment_width_ +
               static_cast<int32_t>(kSegmentCount - 1) * segment_gap_;
  row_y_[0] = 53;
  row_y_[1] = 101;

  for (size_t i = 0; i < std::size(kScaleText); ++i) {
    lv_obj_t *label = lv_label_create(root_);
    lv_label_set_text(label, kScaleText[i]);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, UiTheme::dim(), 0);
    const int32_t x = segment_start_x_ +
                      kScaleSegment[i] * (segment_width_ + segment_gap_);
    lv_obj_set_pos(label, x - 7, 24);
  }

  static constexpr const char *kChannelNames[] = {"L", "R"};
  for (size_t channel = 0; channel < 2; ++channel) {
    lv_obj_t *channel_label = lv_label_create(root_);
    lv_label_set_text(channel_label, kChannelNames[channel]);
    lv_obj_set_style_text_font(channel_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(channel_label, UiTheme::white(), 0);
    lv_obj_set_pos(channel_label, 0, row_y_[channel] - 4);

    bar_contexts_[channel] = {this, channel};
    bar_rows_[channel] = lv_obj_create(root_);
    lv_obj_set_pos(bar_rows_[channel], segment_start_x_, row_y_[channel] - 5);
    lv_obj_set_size(bar_rows_[channel], bar_width_, 23);
    UiTheme::transparent(bar_rows_[channel]);
    lv_obj_set_style_pad_all(bar_rows_[channel], 0, 0);
    lv_obj_remove_flag(bar_rows_[channel], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(bar_rows_[channel], barDrawCallback,
                        LV_EVENT_DRAW_MAIN, &bar_contexts_[channel]);

  }
  return root_;
}

void StereoLevelMeter::barDrawCallback(lv_event_t *event) {
  auto *context =
      static_cast<BarContext *>(lv_event_get_user_data(event));
  if (context != nullptr && context->owner != nullptr) {
    context->owner->drawBarRow(event, context->channel);
  }
}

void StereoLevelMeter::drawBarRow(lv_event_t *event, size_t channel) const {
  lv_layer_t *layer = lv_event_get_layer(event);
  lv_obj_t *object = lv_event_get_target_obj(event);
  if (layer == nullptr || object == nullptr || channel >= channels_.size()) {
    return;
  }

  lv_area_t area;
  lv_obj_get_coords(object, &area);
  const ChannelState &state = channels_[channel];

  lv_draw_rect_dsc_t rect;
  lv_draw_rect_dsc_init(&rect);
  rect.bg_opa = LV_OPA_COVER;
  rect.border_opa = LV_OPA_TRANSP;
  rect.radius = 0;

  for (size_t segment = 0; segment < kSegmentCount; ++segment) {
    const bool active = static_cast<int32_t>(segment) < state.lit_segments;
    const bool overload = segment >= kRedStart;
    rect.bg_color = active ? (overload ? UiTheme::red() : UiTheme::white())
                           : (overload ? UiTheme::darkRed()
                                       : UiTheme::dark());
    const int32_t x = area.x1 +
                      static_cast<int32_t>(segment) *
                          (segment_width_ + segment_gap_);
    const lv_area_t segment_area = {x, area.y1 + 5,
                                    x + segment_width_ - 1, area.y1 + 22};
    lv_draw_rect(layer, &rect, &segment_area);
  }

  if (state.peak_segment >= 0) {
    const int32_t x =
        area.x1 + state.peak_segment * (segment_width_ + segment_gap_);
    rect.bg_color = state.peak_segment >= static_cast<int32_t>(kRedStart)
                        ? UiTheme::red()
                        : UiTheme::white();
    const lv_area_t peak_area = {x, area.y1 + 5, x + segment_width_ - 1,
                                 area.y1 + 22};
    lv_draw_rect(layer, &rect, &peak_area);
  }
}

void StereoLevelMeter::update(uint16_t left_peak, uint16_t right_peak,
                              uint32_t elapsed_ms, bool playing) {
  elapsed_ms = std::min<uint32_t>(elapsed_ms, 100);
  updateChannel(0, left_peak, elapsed_ms, playing);
  updateChannel(1, right_peak, elapsed_ms, playing);
}

float StereoLevelMeter::peakToDb(uint16_t peak) {
  if (peak == 0) {
    return -60.0f;
  }
  return std::max(-60.0f,
                  20.0f * std::log10(static_cast<float>(peak) / 32768.0f));
}

int32_t StereoLevelMeter::segmentForDb(float db) {
  int32_t count = 0;
  for (float threshold : kThresholds) {
    if (db >= threshold) {
      ++count;
    }
  }
  return count;
}

void StereoLevelMeter::updateChannel(size_t channel, uint16_t sample_peak,
                                     uint32_t elapsed_ms, bool playing) {
  ChannelState &state = channels_[channel];
  bool bars_changed = false;
  const float input_db = peakToDb(sample_peak);
  const bool clipped = sample_peak >= 32760;
  const float decay_per_ms = playing ? 0.024f : 0.060f;
  state.displayed_db =
      std::max(input_db, state.displayed_db - decay_per_ms * elapsed_ms);

  if (input_db >= state.peak_db) {
    state.peak_db = input_db;
    state.peak_hold_ms = 650;
  } else if (state.peak_hold_ms > elapsed_ms) {
    state.peak_hold_ms -= elapsed_ms;
  } else {
    state.peak_hold_ms = 0;
    state.peak_db = std::max(state.displayed_db,
                             state.peak_db - 0.010f * elapsed_ms);
  }

  int32_t lit = segmentForDb(state.displayed_db);
  if (clipped) {
    lit = static_cast<int32_t>(kSegmentCount);
  }
  if (lit != state.lit_segments) {
    state.lit_segments = lit;
    bars_changed = true;
  }

  const int32_t peak_segment = segmentForDb(state.peak_db) - 1;
  if (peak_segment != state.peak_segment) {
    state.peak_segment = peak_segment;
    bars_changed = true;
  }

  if (bars_changed) {
    lv_obj_invalidate(bar_rows_[channel]);
  }

}
