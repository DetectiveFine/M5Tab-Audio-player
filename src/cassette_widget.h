#pragma once

#include <lvgl.h>

#include <cstdint>

class CassetteWidget {
 public:
  lv_obj_t *create(lv_obj_t *parent, int32_t width, int32_t height);
  void update(bool playing, bool stopped, uint32_t elapsed_ms,
              uint32_t position_ms, uint32_t duration_ms);

 private:
  struct ReelContext {
    CassetteWidget *owner = nullptr;
    bool right = false;
  };

  static void bodyDrawCallback(lv_event_t *event);
  static void reelDrawCallback(lv_event_t *event);

  void drawBody(lv_event_t *event) const;
  void drawReel(lv_event_t *event, bool right) const;
  void invalidateReelMarkers(lv_obj_t *reel, float old_angle,
                             float new_angle) const;

  lv_obj_t *root_ = nullptr;
  lv_obj_t *body_ = nullptr;
  lv_obj_t *left_reel_ = nullptr;
  lv_obj_t *right_reel_ = nullptr;
  ReelContext left_context_{};
  ReelContext right_context_{};
  int32_t width_ = 0;
  int32_t height_ = 0;
  float left_angle_ = 0.0f;
  float right_angle_ = 0.0f;
};
