#include "cassette_widget.h"

#include <algorithm>
#include <cmath>

#include "ui_theme.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

void drawLine(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2,
              int32_t y2, int32_t width = 2,
              lv_color_t color = UiTheme::white()) {
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.p1 = {x1, y1};
  line.p2 = {x2, y2};
  line.width = width;
  line.color = color;
  line.opa = LV_OPA_COVER;
  line.round_start = 1;
  line.round_end = 1;
  lv_draw_line(layer, &line);
}

void drawArc(lv_layer_t *layer, int32_t cx, int32_t cy, uint16_t radius,
             int32_t width = 2, lv_color_t color = UiTheme::white(),
             float start = 0.0f, float end = 359.0f) {
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.center = {cx, cy};
  arc.radius = radius;
  arc.width = width;
  arc.start_angle = start;
  arc.end_angle = end;
  arc.color = color;
  arc.opa = LV_OPA_COVER;
  arc.rounded = 0;
  lv_draw_arc(layer, &arc);
}

void markerEndpoints(int32_t cx, int32_t cy, uint16_t outer,
                     float base_angle, int marker, lv_point_t &inner,
                     lv_point_t &outer_point) {
  const float angle =
      (base_angle + marker * 120.0f) * kPi / 180.0f;
  inner.x = cx + std::cos(angle) * (outer - 22);
  inner.y = cy + std::sin(angle) * (outer - 22);
  outer_point.x = cx + std::cos(angle) * (outer - 6);
  outer_point.y = cy + std::sin(angle) * (outer - 6);
}

void drawReelBase(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t size) {
  const uint16_t outer = static_cast<uint16_t>(size / 2 - 5);
  const uint16_t middle = static_cast<uint16_t>(outer - 8);
  const uint16_t hub =
      static_cast<uint16_t>(std::max<int32_t>(10, size / 15));
  drawArc(layer, cx, cy, outer, 3);
  drawArc(layer, cx, cy, middle, 1);
  drawArc(layer, cx, cy, hub, 3);
  drawArc(layer, cx, cy, 2, 2);
}

}  // namespace

lv_obj_t *CassetteWidget::create(lv_obj_t *parent, int32_t width,
                                 int32_t height) {
  width_ = width;
  height_ = height;
  root_ = lv_obj_create(parent);
  lv_obj_set_size(root_, width, height);
  UiTheme::transparent(root_);
  lv_obj_set_style_pad_all(root_, 0, 0);
  lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

  body_ = lv_obj_create(root_);
  lv_obj_set_size(body_, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(body_, 0, 0);
  UiTheme::transparent(body_);
  lv_obj_remove_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(body_, bodyDrawCallback, LV_EVENT_DRAW_MAIN, this);

  const int32_t reel_size = std::min<int32_t>(height * 58 / 100, width / 4);
  const int32_t reel_y = height * 35 / 100;
  const int32_t reel_margin = width * 8 / 100;

  left_context_ = {this, false};
  left_reel_ = lv_obj_create(root_);
  lv_obj_set_size(left_reel_, reel_size, reel_size);
  lv_obj_set_pos(left_reel_, reel_margin, reel_y);
  UiTheme::transparent(left_reel_);
  lv_obj_remove_flag(left_reel_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(left_reel_, reelDrawCallback, LV_EVENT_DRAW_MAIN,
                      &left_context_);

  right_context_ = {this, true};
  right_reel_ = lv_obj_create(root_);
  lv_obj_set_size(right_reel_, reel_size, reel_size);
  lv_obj_set_pos(right_reel_, width - reel_margin - reel_size, reel_y);
  UiTheme::transparent(right_reel_);
  lv_obj_remove_flag(right_reel_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(right_reel_, reelDrawCallback, LV_EVENT_DRAW_MAIN,
                      &right_context_);
  return root_;
}

void CassetteWidget::update(bool playing, bool stopped, uint32_t elapsed_ms,
                            uint32_t position_ms, uint32_t duration_ms) {
  if (playing) {
    const float progress =
        duration_ms > 0
            ? std::min(1.0f, static_cast<float>(position_ms) / duration_ms)
            : 0.5f;
    // A full left reel starts faster while the right reel starts slower.
    // Their speeds cross at the middle and exchange by the end of the track.
    constexpr float kSlowMultiplier = 0.55f;
    constexpr float kFastMultiplier = 1.45f;
    const float left_multiplier =
        kFastMultiplier - (kFastMultiplier - kSlowMultiplier) * progress;
    const float right_multiplier =
        kSlowMultiplier + (kFastMultiplier - kSlowMultiplier) * progress;
    const float advance = std::min<uint32_t>(elapsed_ms, 100) * 0.105f;
    const float old_left = left_angle_;
    const float old_right = right_angle_;
    left_angle_ = std::fmod(
        old_left - advance * left_multiplier + 360.0f, 360.0f);
    right_angle_ = std::fmod(
        old_right - advance * right_multiplier + 360.0f, 360.0f);
    invalidateReelMarkers(left_reel_, old_left, left_angle_);
    invalidateReelMarkers(right_reel_, old_right, right_angle_);
  } else if (stopped && (left_angle_ != 0.0f || right_angle_ != 0.0f)) {
    const float old_left = left_angle_;
    const float old_right = right_angle_;
    left_angle_ = 0.0f;
    right_angle_ = 0.0f;
    invalidateReelMarkers(left_reel_, old_left, left_angle_);
    invalidateReelMarkers(right_reel_, old_right, right_angle_);
  }
}

void CassetteWidget::invalidateReelMarkers(lv_obj_t *reel, float old_angle,
                                           float new_angle) const {
  if (reel == nullptr) {
    return;
  }

  lv_area_t area;
  lv_obj_get_coords(reel, &area);
  const int32_t size =
      std::min(lv_area_get_width(&area), lv_area_get_height(&area));
  const int32_t cx = area.x1 + lv_area_get_width(&area) / 2;
  const int32_t cy = area.y1 + lv_area_get_height(&area) / 2;
  const uint16_t outer = static_cast<uint16_t>(size / 2 - 5);
  constexpr int32_t kLineMargin = 4;

  for (int marker = 0; marker < 3; ++marker) {
    lv_point_t old_inner;
    lv_point_t old_outer;
    lv_point_t new_inner;
    lv_point_t new_outer;
    markerEndpoints(cx, cy, outer, old_angle, marker, old_inner, old_outer);
    markerEndpoints(cx, cy, outer, new_angle, marker, new_inner, new_outer);
    const lv_area_t marker_area = {
        std::min({old_inner.x, old_outer.x, new_inner.x, new_outer.x}) -
            kLineMargin,
        std::min({old_inner.y, old_outer.y, new_inner.y, new_outer.y}) -
            kLineMargin,
        std::max({old_inner.x, old_outer.x, new_inner.x, new_outer.x}) +
            kLineMargin,
        std::max({old_inner.y, old_outer.y, new_inner.y, new_outer.y}) +
            kLineMargin};
    lv_obj_invalidate_area(reel, &marker_area);
  }
}

void CassetteWidget::bodyDrawCallback(lv_event_t *event) {
  auto *widget = static_cast<CassetteWidget *>(lv_event_get_user_data(event));
  if (widget != nullptr) {
    widget->drawBody(event);
  }
}

void CassetteWidget::reelDrawCallback(lv_event_t *event) {
  auto *context = static_cast<ReelContext *>(lv_event_get_user_data(event));
  if (context != nullptr && context->owner != nullptr) {
    context->owner->drawReel(event, context->right);
  }
}

void CassetteWidget::drawBody(lv_event_t *event) const {
  lv_layer_t *layer = lv_event_get_layer(event);
  lv_obj_t *object = lv_event_get_target_obj(event);
  if (layer == nullptr || object == nullptr) {
    return;
  }

  lv_area_t area;
  lv_obj_get_coords(object, &area);
  const int32_t x = area.x1;
  const int32_t y = area.y1;
  const int32_t w = lv_area_get_width(&area);
  const int32_t h = lv_area_get_height(&area);

  const int32_t center = x + w / 2;

  // The rings and hubs never move, so keep them in the static cassette body.
  // Only the three short rim marks are invalidated during playback.
  const int32_t reel_size = std::min<int32_t>(h * 58 / 100, w / 4);
  const int32_t reel_top = y + h * 35 / 100;
  const int32_t reel_margin = w * 8 / 100;
  const int32_t reel_center_y = reel_top + reel_size / 2;
  const int32_t left_reel_x = x + reel_margin + reel_size / 2;
  const int32_t right_reel_x =
      x + w - reel_margin - reel_size / 2;
  const int32_t reel_outer = reel_size / 2 - 5;
  const int32_t left_tangent_x = left_reel_x - reel_outer;
  const int32_t right_tangent_x = right_reel_x + reel_outer;

  // The white contour is the tape path, not a cassette shell. It travels
  // through the upper guide rollers, dips under the open head and descends
  // tangentially to the two reels.
  const int32_t guide_y = y + h * 18 / 100;
  const int32_t upper_left_x = x + w * 24 / 100;
  const int32_t upper_right_x = x + w * 76 / 100;
  const int32_t upper_path_y = y + h * 10 / 100;
  const int32_t head_path_y = y + h * 14 / 100;
  const int32_t upper_head_half_width = 35;

  drawLine(layer, left_tangent_x, guide_y, upper_left_x, upper_path_y, 2);
  drawLine(layer, upper_left_x, upper_path_y,
           center - upper_head_half_width, head_path_y, 2);
  drawLine(layer, center - upper_head_half_width, head_path_y,
           center - 18, head_path_y + 6, 2);
  drawLine(layer, center - 18, head_path_y + 6, center,
           head_path_y + 8, 2);
  drawLine(layer, center, head_path_y + 8, center + 18,
           head_path_y + 6, 2);
  drawLine(layer, center + 18, head_path_y + 6,
           center + upper_head_half_width, head_path_y, 2);
  drawLine(layer, center + upper_head_half_width, head_path_y,
           upper_right_x, upper_path_y, 2);
  drawLine(layer, upper_right_x, upper_path_y, right_tangent_x, guide_y, 2);
  drawLine(layer, left_tangent_x, guide_y, left_tangent_x, reel_center_y, 2);
  drawLine(layer, right_tangent_x, guide_y, right_tangent_x, reel_center_y,
           2);

  // The upper head is open at the top, just as on the reference drawing.
  drawLine(layer, center - upper_head_half_width, y + 2,
           center - upper_head_half_width, head_path_y, 2);
  drawLine(layer, center + upper_head_half_width, y + 2,
           center + upper_head_half_width, head_path_y, 2);

  drawReelBase(layer, left_reel_x, reel_center_y, reel_size);
  drawReelBase(layer, right_reel_x, reel_center_y, reel_size);

  // Narrow, open-bottom playback head matching the reference geometry.
  const int32_t head_half_width = std::max<int32_t>(6, w / 120);
  const int32_t head_top = y + h * 51 / 100;
  const int32_t head_bottom = y + h * 70 / 100;
  drawLine(layer, center - head_half_width, head_bottom,
           center - head_half_width, head_top, 2);
  drawLine(layer, center - head_half_width, head_top,
           center + head_half_width, head_top, 2);
  drawLine(layer, center + head_half_width, head_top,
           center + head_half_width, head_bottom, 2);

  const int32_t tick_y = head_bottom;
  for (int tick = -7; tick <= 7; ++tick) {
    const int32_t tx = center + tick * 17;
    const int32_t length = tick == 0 ? 18 : ((tick & 1) ? 9 : 13);
    drawLine(layer, tx, tick_y - length / 2, tx, tick_y + length / 2, 1);
  }

  const int32_t guide_radius = std::max<int32_t>(5, h / 28);
  const int32_t guide_hub = std::max<int32_t>(2, h / 85);
  drawArc(layer, left_tangent_x, guide_y, guide_radius, 2);
  drawArc(layer, left_tangent_x, guide_y, guide_hub, 2);
  drawArc(layer, right_tangent_x, guide_y, guide_radius, 2);
  drawArc(layer, right_tangent_x, guide_y, guide_hub, 2);

  const int32_t upper_radius = std::max<int32_t>(5, h / 28);
  drawArc(layer, upper_left_x, y + h * 7 / 100, upper_radius, 2);
  drawArc(layer, upper_left_x, y + h * 7 / 100, guide_hub, 2);
  drawArc(layer, upper_right_x, y + h * 7 / 100, upper_radius, 2);
  drawArc(layer, upper_right_x, y + h * 7 / 100, guide_hub, 2);
  drawArc(layer, upper_left_x, y + h * 14 / 100, h / 55, 1);
  drawArc(layer, upper_right_x, y + h * 14 / 100, h / 55, 1);

  const int32_t small_guide_y = y + h * 22 / 100;
  const int32_t small_guide_radius = std::max<int32_t>(4, h / 55);
  const int32_t small_guide_x[] = {x + w * 34 / 100,
                                   x + w * 66 / 100};
  for (const int32_t sx : small_guide_x) {
    drawArc(layer, sx, small_guide_y, small_guide_radius, 2);
    drawArc(layer, sx, small_guide_y, 1, 2);
  }
}

void CassetteWidget::drawReel(lv_event_t *event, bool right) const {
  lv_layer_t *layer = lv_event_get_layer(event);
  lv_obj_t *object = lv_event_get_target_obj(event);
  if (layer == nullptr || object == nullptr) {
    return;
  }

  lv_area_t area;
  lv_obj_get_coords(object, &area);
  const int32_t size = std::min(lv_area_get_width(&area),
                                lv_area_get_height(&area));
  const int32_t cx = area.x1 + lv_area_get_width(&area) / 2;
  const int32_t cy = area.y1 + lv_area_get_height(&area) / 2;
  const uint16_t outer = static_cast<uint16_t>(size / 2 - 5);
  const float angle = right ? right_angle_ : left_angle_;
  // The reference has exactly three short index marks on the reel rim. There
  // are no hub-to-rim spokes and no fourth marker.
  for (int marker = 0; marker < 3; ++marker) {
    lv_point_t inner;
    lv_point_t outer_point;
    markerEndpoints(cx, cy, outer, angle, marker, inner, outer_point);
    drawLine(layer, inner.x, inner.y, outer_point.x, outer_point.y, 6);
  }
}
