#pragma once

#include <lvgl.h>

namespace UiTheme {

inline lv_color_t black() { return lv_color_hex(0x000000); }
inline lv_color_t white() { return lv_color_hex(0xF4F4EF); }
inline lv_color_t softWhite() { return lv_color_hex(0xC9C9C4); }
inline lv_color_t dim() { return lv_color_hex(0x666662); }
inline lv_color_t dark() { return lv_color_hex(0x1C1C1C); }
inline lv_color_t red() { return lv_color_hex(0xE32626); }
inline lv_color_t darkRed() { return lv_color_hex(0x320909); }

inline void transparent(lv_obj_t *object) {
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_outline_width(object, 0, 0);
  lv_obj_set_style_shadow_width(object, 0, 0);
}

inline void panel(lv_obj_t *object, int32_t border_width = 1) {
  lv_obj_set_style_bg_color(object, black(), 0);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(object, softWhite(), 0);
  lv_obj_set_style_border_width(object, border_width, 0);
  lv_obj_set_style_radius(object, 0, 0);
  lv_obj_set_style_shadow_width(object, 0, 0);
}

inline void technicalLabel(lv_obj_t *label, lv_color_t color = softWhite()) {
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_letter_space(label, 2, 0);
}

inline void outlineButton(lv_obj_t *button, bool emphasized = false) {
  lv_obj_set_style_bg_color(button, black(), 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(button, white(), 0);
  lv_obj_set_style_border_width(button, emphasized ? 2 : 1, 0);
  lv_obj_set_style_radius(button, 0, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_set_style_text_color(button, white(), 0);

  lv_obj_set_style_bg_color(button, white(), LV_STATE_PRESSED);
  lv_obj_set_style_text_color(button, black(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(button, white(), LV_STATE_CHECKED);
  lv_obj_set_style_text_color(button, black(), LV_STATE_CHECKED);
}

inline void thinSlider(lv_obj_t *slider, bool vertical = false) {
  lv_obj_set_style_bg_color(slider, dark(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(slider, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, white(), LV_PART_INDICATOR);
  lv_obj_set_style_radius(slider, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, white(), LV_PART_KNOB);
  lv_obj_set_style_radius(slider, 0, LV_PART_KNOB);
  if (vertical) {
    lv_obj_set_style_width(slider, 3, LV_PART_MAIN);
    lv_obj_set_style_width(slider, 3, LV_PART_INDICATOR);
    lv_obj_set_style_width(slider, 14, LV_PART_KNOB);
    lv_obj_set_style_height(slider, 4, LV_PART_KNOB);
  } else {
    lv_obj_set_style_height(slider, 3, LV_PART_MAIN);
    lv_obj_set_style_height(slider, 3, LV_PART_INDICATOR);
    lv_obj_set_style_width(slider, 5, LV_PART_KNOB);
    lv_obj_set_style_height(slider, 16, LV_PART_KNOB);
  }
}

inline void monochromeSwitch(lv_obj_t *control) {
  const lv_style_selector_t checked_main =
      static_cast<lv_style_selector_t>(LV_PART_MAIN) |
      static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
  const lv_style_selector_t checked_knob =
      static_cast<lv_style_selector_t>(LV_PART_KNOB) |
      static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(control, dark(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(control, white(), checked_main);
  lv_obj_set_style_bg_color(control, dim(), LV_PART_KNOB);
  lv_obj_set_style_bg_color(control, black(), checked_knob);
  lv_obj_set_style_border_color(control, softWhite(), LV_PART_MAIN);
  lv_obj_set_style_border_width(control, 1, LV_PART_MAIN);
}

}  // namespace UiTheme
