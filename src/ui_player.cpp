#include "ui_player.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ui_theme.h"

namespace {

constexpr int32_t kScreenWidth = 1280;
constexpr int32_t kScreenHeight = 720;
constexpr uint32_t kAnimationRefreshMs = 16;
constexpr uint32_t kStateRefreshMs = 100;
constexpr uint32_t kFolderLaunchDelayMs = 80;

void makePlainContainer(lv_obj_t *object) {
  UiTheme::transparent(object);
  lv_obj_set_style_pad_all(object, 0, 0);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

void setSwitchState(lv_obj_t *control, bool checked) {
  if (control == nullptr ||
      lv_obj_has_state(control, LV_STATE_CHECKED) == checked) {
    return;
  }
  if (checked) {
    lv_obj_add_state(control, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(control, LV_STATE_CHECKED);
  }
}

void setLabelText(lv_obj_t *label, const char *text) {
  if (label != nullptr && text != nullptr &&
      std::strcmp(lv_label_get_text(label), text) != 0) {
    lv_label_set_text(label, text);
  }
}

void setSliderValue(lv_obj_t *slider, int32_t value) {
  if (slider != nullptr && lv_slider_get_value(slider) != value) {
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
  }
}

}  // namespace

void UiPlayer::begin(AudioPlayer &player) {
  player_ = &player;
  createLayout();
  last_refresh_ms_ = millis();
  last_state_refresh_ms_ = last_refresh_ms_;
  lv_timer_create(timerCallback, kAnimationRefreshMs, this);
  refresh();
}

void UiPlayer::timerCallback(lv_timer_t *timer) {
  auto *ui = static_cast<UiPlayer *>(lv_timer_get_user_data(timer));
  if (ui != nullptr) {
    ui->refresh();
  }
}

void UiPlayer::playPauseCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr && ui->player_ != nullptr) {
    (void)ui->player_->togglePlayPause();
  }
}

void UiPlayer::stopCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr && ui->player_ != nullptr) {
    (void)ui->player_->stop();
  }
}

void UiPlayer::previousCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr && ui->player_ != nullptr) {
    (void)ui->player_->previous();
  }
}

void UiPlayer::nextCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr && ui->player_ != nullptr) {
    (void)ui->player_->next();
  }
}

void UiPlayer::seekCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr && ui->player_ != nullptr) {
    (void)ui->player_->seekPermille(static_cast<uint16_t>(
        lv_slider_get_value(lv_event_get_target_obj(event))));
  }
}

void UiPlayer::volumeCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr && ui->player_ != nullptr) {
    const int32_t value = lv_slider_get_value(lv_event_get_target_obj(event));
    lv_label_set_text_fmt(ui->volume_label_, "%ld", static_cast<long>(value));
    (void)ui->player_->setVolume(static_cast<uint8_t>(value));
  }
}

void UiPlayer::speakerEnableCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr && ui->player_ != nullptr) {
    (void)ui->player_->setSpeakerEnabled(
        lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED));
  }
}

void UiPlayer::openFolderCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr) {
    if (ui->pending_track_index_ < 0) {
      ui->folder_selection_pending_ = false;
    }
    ui->showOverlay(ui->folder_overlay_);
  }
}

void UiPlayer::closeFolderCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr) {
    ui->hideOverlay(ui->folder_overlay_);
  }
}

void UiPlayer::folderTrackCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  lv_obj_t *button = lv_event_get_current_target_obj(event);
  if (ui == nullptr || ui->player_ == nullptr || button == nullptr ||
      ui->folder_selection_pending_) {
    return;
  }

  const uintptr_t index_token =
      reinterpret_cast<uintptr_t>(lv_obj_get_user_data(button));
  if (index_token == 0) {
    return;
  }
  const size_t index = static_cast<size_t>(index_token - 1U);
  if (index >= ui->playlist_buttons_.size() ||
      ui->playlist_buttons_[index] != button) {
    return;
  }

  ui->folder_selection_pending_ = true;
  ui->folder_close_pending_ = true;
  ui->pending_track_index_ = static_cast<int32_t>(index);
  ui->pending_track_start_ms_ = 0;
}

void UiPlayer::rescanCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr && ui->player_ != nullptr) {
    (void)ui->player_->rescan();
  }
}

void UiPlayer::openEqCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr) {
    ui->showOverlay(ui->eq_overlay_);
  }
}

void UiPlayer::closeEqCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr) {
    ui->hideOverlay(ui->eq_overlay_);
  }
}

void UiPlayer::eqEnableCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr && ui->player_ != nullptr) {
    (void)ui->player_->setEqEnabled(
        lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED));
  }
}

void UiPlayer::eqFlatCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  if (ui != nullptr && ui->player_ != nullptr) {
    (void)ui->player_->setEqFlat();
  }
}

void UiPlayer::eqBandCallback(lv_event_t *event) {
  auto *ui = static_cast<UiPlayer *>(lv_event_get_user_data(event));
  lv_obj_t *slider = lv_event_get_target_obj(event);
  if (ui == nullptr || ui->player_ == nullptr || slider == nullptr) {
    return;
  }
  const size_t band =
      reinterpret_cast<uintptr_t>(lv_obj_get_user_data(slider));
  if (band >= AudioEffects::kBandCount) {
    return;
  }
  const int32_t gain = lv_slider_get_value(slider);
  lv_label_set_text_fmt(ui->eq_value_labels_[band], "%+ld",
                        static_cast<long>(gain));
  (void)ui->player_->setEqBandGain(band, static_cast<int8_t>(gain));
}

const char *UiPlayer::stateName(PlaybackState state) {
  switch (state) {
    case PlaybackState::Initializing:
      return "INITIALIZING";
    case PlaybackState::Scanning:
      return "SCANNING SD";
    case PlaybackState::Stopped:
      return "STOPPED";
    case PlaybackState::Playing:
      return "PLAY";
    case PlaybackState::Paused:
      return "PAUSE";
    case PlaybackState::Error:
      return "ERROR";
  }
  return "UNKNOWN";
}

void UiPlayer::formatTime(uint32_t milliseconds, char *buffer,
                          size_t buffer_size) {
  const uint32_t seconds = milliseconds / 1000;
  const uint32_t hours = seconds / 3600;
  if (hours > 0) {
    std::snprintf(buffer, buffer_size, "%lu:%02lu:%02lu",
                  static_cast<unsigned long>(hours),
                  static_cast<unsigned long>((seconds / 60) % 60),
                  static_cast<unsigned long>(seconds % 60));
  } else {
    std::snprintf(buffer, buffer_size, "%02lu:%02lu",
                  static_cast<unsigned long>(seconds / 60),
                  static_cast<unsigned long>(seconds % 60));
  }
}

lv_obj_t *UiPlayer::createControlButton(lv_obj_t *parent, const char *text,
                                        lv_event_cb_t callback,
                                        void *user_data, int32_t width,
                                        bool emphasized) {
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_set_size(button, width, emphasized ? 66 : 58);
  UiTheme::outlineButton(button, emphasized);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);

  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, emphasized ? &lv_font_montserrat_28
                                               : &lv_font_montserrat_20,
                             0);
  lv_obj_center(label);
  return button;
}

lv_obj_t *UiPlayer::createSmallButton(lv_obj_t *parent, const char *text,
                                      lv_event_cb_t callback,
                                      void *user_data, int32_t width) {
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_set_size(button, width, 38);
  UiTheme::outlineButton(button);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_center(label);
  return button;
}

void UiPlayer::createLayout() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, UiTheme::black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(screen, UiTheme::white(), 0);
  lv_obj_set_style_text_font(screen, &lv_font_montserrat_14, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  createMainView(screen);
  createControls(screen);
  createFolderOverlay(screen);
  createEqOverlay(screen);
}

void UiPlayer::createMainView(lv_obj_t *screen) {
  lv_obj_t *header = lv_obj_create(screen);
  lv_obj_set_pos(header, 22, 12);
  lv_obj_set_size(header, kScreenWidth - 44, 44);
  makePlainContainer(header);
  lv_obj_set_style_border_color(header, UiTheme::dim(), 0);
  lv_obj_set_style_border_width(header, 1, 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);

  lv_obj_t *brand = lv_label_create(header);
  lv_label_set_text(brand, "M5 TAPE");
  lv_obj_set_style_text_font(brand, &lv_font_montserrat_20, 0);
  UiTheme::technicalLabel(brand, UiTheme::white());
  lv_obj_align(brand, LV_ALIGN_LEFT_MID, 0, -3);

  state_label_ = lv_label_create(header);
  lv_obj_set_width(state_label_, 520);
  lv_label_set_long_mode(state_label_, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_align(state_label_, LV_TEXT_ALIGN_CENTER, 0);
  UiTheme::technicalLabel(state_label_, UiTheme::softWhite());
  lv_obj_align(state_label_, LV_ALIGN_CENTER, 0, -3);

  route_label_ = lv_label_create(header);
  UiTheme::technicalLabel(route_label_, UiTheme::white());
  lv_obj_align(route_label_, LV_ALIGN_RIGHT_MID, 0, -3);

  lv_obj_t *left = lv_obj_create(screen);
  lv_obj_set_pos(left, 22, 68);
  lv_obj_set_size(left, 830, 476);
  makePlainContainer(left);

  lv_obj_t *track_caption = lv_label_create(left);
  lv_label_set_text(track_caption, "NOW PLAYING // CASSETTE A");
  UiTheme::technicalLabel(track_caption, UiTheme::dim());
  lv_obj_set_pos(track_caption, 0, 0);

  artist_label_ = lv_label_create(left);
  lv_obj_set_pos(artist_label_, 340, 0);
  lv_obj_set_width(artist_label_, 490);
  lv_label_set_long_mode(artist_label_, LV_LABEL_LONG_MODE_DOTS);
  UiTheme::technicalLabel(artist_label_, UiTheme::softWhite());
  lv_label_set_text(artist_label_, "ARTIST // --");

  title_label_ = lv_label_create(left);
  lv_obj_set_pos(title_label_, 0, 24);
  lv_obj_set_width(title_label_, 830);
  lv_label_set_long_mode(title_label_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title_label_, UiTheme::white(), 0);
  lv_label_set_text(title_label_, "SELECT A TRACK");

  lv_obj_t *cassette = cassette_.create(left, 830, 300);
  lv_obj_set_pos(cassette, 0, 70);

  time_label_ = lv_label_create(left);
  lv_obj_set_width(time_label_, 830);
  lv_obj_set_style_text_align(time_label_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(time_label_, &lv_font_montserrat_20, 0);
  UiTheme::technicalLabel(time_label_, UiTheme::white());
  lv_label_set_text(time_label_, "00:00 / 00:00");
  lv_obj_set_pos(time_label_, 0, 380);

  progress_slider_ = lv_slider_create(left);
  lv_slider_set_range(progress_slider_, 0, 1000);
  lv_slider_set_value(progress_slider_, 0, LV_ANIM_OFF);
  lv_obj_set_pos(progress_slider_, 0, 421);
  lv_obj_set_size(progress_slider_, 830, 12);
  UiTheme::thinSlider(progress_slider_);
  lv_obj_add_event_cb(progress_slider_, seekCallback, LV_EVENT_RELEASED, this);

  lv_obj_t *timeline_marks = lv_label_create(left);
  lv_label_set_text(timeline_marks,
                    "00 |---------------+---------------+---------------| END");
  lv_obj_set_width(timeline_marks, 830);
  lv_obj_set_style_text_align(timeline_marks, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(timeline_marks, UiTheme::dim(), 0);
  lv_obj_set_pos(timeline_marks, 0, 446);

  lv_obj_t *right = lv_obj_create(screen);
  lv_obj_set_pos(right, 874, 68);
  lv_obj_set_size(right, 384, 476);
  makePlainContainer(right);

  lv_obj_t *meter = level_meter_.create(right, 384, 155);
  lv_obj_set_pos(meter, 0, 0);

  lv_obj_t *transport = lv_obj_create(right);
  lv_obj_set_pos(transport, 0, 169);
  lv_obj_set_size(transport, 384, 119);
  UiTheme::panel(transport);
  lv_obj_set_style_pad_all(transport, 12, 0);
  lv_obj_remove_flag(transport, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *transport_caption = lv_label_create(transport);
  lv_label_set_text(transport_caption, "SIGNAL PATH");
  UiTheme::technicalLabel(transport_caption, UiTheme::dim());
  lv_obj_set_pos(transport_caption, 0, 0);

  format_label_ = lv_label_create(transport);
  lv_obj_set_width(format_label_, 358);
  lv_label_set_long_mode(format_label_, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_font(format_label_, &lv_font_montserrat_20, 0);
  UiTheme::technicalLabel(format_label_, UiTheme::white());
  lv_label_set_text(format_label_, "SD // -- TRACKS");
  lv_obj_set_pos(format_label_, 0, 30);

  lv_obj_t *signal = lv_label_create(transport);
  lv_label_set_text(signal, "SD > DECODE > EQ > I2S > ES8388");
  UiTheme::technicalLabel(signal, UiTheme::softWhite());
  lv_obj_set_pos(signal, 0, 68);

  lv_obj_t *volume_panel = lv_obj_create(right);
  lv_obj_set_pos(volume_panel, 0, 302);
  lv_obj_set_size(volume_panel, 384, 83);
  UiTheme::panel(volume_panel);
  lv_obj_set_style_pad_all(volume_panel, 12, 0);
  lv_obj_remove_flag(volume_panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *volume_caption = lv_label_create(volume_panel);
  lv_label_set_text(volume_caption, "OUTPUT LEVEL");
  UiTheme::technicalLabel(volume_caption, UiTheme::dim());
  lv_obj_set_pos(volume_caption, 0, 0);

  volume_slider_ = lv_slider_create(volume_panel);
  lv_slider_set_range(volume_slider_, 0, 100);
  lv_slider_set_value(volume_slider_, 55, LV_ANIM_OFF);
  lv_obj_set_pos(volume_slider_, 0, 42);
  lv_obj_set_size(volume_slider_, 310, 14);
  UiTheme::thinSlider(volume_slider_);
  lv_obj_add_event_cb(volume_slider_, volumeCallback, LV_EVENT_VALUE_CHANGED,
                      this);

  volume_label_ = lv_label_create(volume_panel);
  lv_label_set_text(volume_label_, "55");
  lv_obj_set_style_text_font(volume_label_, &lv_font_montserrat_20, 0);
  lv_obj_align(volume_label_, LV_ALIGN_RIGHT_MID, 0, 12);

  lv_obj_t *speaker_panel = lv_obj_create(right);
  lv_obj_set_pos(speaker_panel, 0, 399);
  lv_obj_set_size(speaker_panel, 384, 70);
  UiTheme::panel(speaker_panel);
  lv_obj_set_style_pad_all(speaker_panel, 12, 0);
  lv_obj_remove_flag(speaker_panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *speaker_caption = lv_label_create(speaker_panel);
  lv_label_set_text(speaker_caption, "INTERNAL SPEAKER");
  UiTheme::technicalLabel(speaker_caption, UiTheme::white());
  lv_obj_align(speaker_caption, LV_ALIGN_LEFT_MID, 0, 0);

  speaker_switch_ = lv_switch_create(speaker_panel);
  lv_obj_set_size(speaker_switch_, 58, 28);
  UiTheme::monochromeSwitch(speaker_switch_);
  lv_obj_add_state(speaker_switch_, LV_STATE_CHECKED);
  lv_obj_align(speaker_switch_, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(speaker_switch_, speakerEnableCallback,
                      LV_EVENT_VALUE_CHANGED, this);
}

void UiPlayer::createControls(lv_obj_t *screen) {
  lv_obj_t *controls = lv_obj_create(screen);
  lv_obj_set_pos(controls, 22, 558);
  lv_obj_set_size(controls, kScreenWidth - 44, 146);
  makePlainContainer(controls);
  lv_obj_set_style_border_color(controls, UiTheme::dim(), 0);
  lv_obj_set_style_border_width(controls, 1, 0);
  lv_obj_set_style_border_side(controls, LV_BORDER_SIDE_TOP, 0);

  lv_obj_t *row = lv_obj_create(controls);
  lv_obj_set_pos(row, 0, 13);
  lv_obj_set_size(row, LV_PCT(100), 70);
  makePlainContainer(row);
  lv_obj_set_style_pad_column(row, 16, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  createControlButton(row, LV_SYMBOL_PREV, previousCallback, this, 112);
  play_button_ = createControlButton(row, LV_SYMBOL_PLAY, playPauseCallback,
                                     this, 146, true);
  play_button_label_ = lv_obj_get_child(play_button_, 0);
  createControlButton(row, LV_SYMBOL_NEXT, nextCallback, this, 112);
  createControlButton(row, LV_SYMBOL_STOP, stopCallback, this, 112);
  createControlButton(row, LV_SYMBOL_DIRECTORY "  FILES", openFolderCallback,
                      this, 166);
  createControlButton(row, "EQ", openEqCallback, this, 112);

  lv_obj_t *footer_left = lv_label_create(controls);
  lv_label_set_text(footer_left, "STEREO // 16-BIT PCM");
  UiTheme::technicalLabel(footer_left, UiTheme::dim());
  lv_obj_align(footer_left, LV_ALIGN_BOTTOM_LEFT, 0, -10);

  lv_obj_t *footer_right = lv_label_create(controls);
  lv_label_set_text(footer_right, "SD AUDIO DECK  /  REV.01");
  UiTheme::technicalLabel(footer_right, UiTheme::dim());
  lv_obj_align(footer_right, LV_ALIGN_BOTTOM_RIGHT, 0, -10);
}

void UiPlayer::createFolderOverlay(lv_obj_t *screen) {
  folder_overlay_ = lv_obj_create(screen);
  lv_obj_set_pos(folder_overlay_, 0, 0);
  lv_obj_set_size(folder_overlay_, kScreenWidth, kScreenHeight);
  lv_obj_set_style_bg_color(folder_overlay_, UiTheme::black(), 0);
  lv_obj_set_style_bg_opa(folder_overlay_, LV_OPA_90, 0);
  lv_obj_set_style_border_width(folder_overlay_, 0, 0);
  lv_obj_set_style_radius(folder_overlay_, 0, 0);
  lv_obj_set_style_pad_all(folder_overlay_, 0, 0);
  lv_obj_remove_flag(folder_overlay_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *panel = lv_obj_create(folder_overlay_);
  lv_obj_set_size(panel, 1040, 610);
  lv_obj_center(panel);
  UiTheme::panel(panel, 2);
  lv_obj_set_style_pad_all(panel, 22, 0);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(panel);
  lv_label_set_text(title, "/ MUSIC");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  UiTheme::technicalLabel(title, UiTheme::white());
  lv_obj_set_pos(title, 0, 0);

  folder_count_label_ = lv_label_create(panel);
  lv_label_set_text(folder_count_label_, "0 FILES");
  UiTheme::technicalLabel(folder_count_label_, UiTheme::dim());
  lv_obj_align(folder_count_label_, LV_ALIGN_TOP_RIGHT, -172, 8);

  lv_obj_t *rescan = createSmallButton(panel, "RESCAN SD", rescanCallback,
                                       this, 120);
  lv_obj_align(rescan, LV_ALIGN_TOP_RIGHT, -48, -3);
  lv_obj_t *close = createSmallButton(panel, LV_SYMBOL_CLOSE,
                                      closeFolderCallback, this, 38);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -3);

  lv_obj_t *line = lv_obj_create(panel);
  lv_obj_set_pos(line, 0, 50);
  lv_obj_set_size(line, LV_PCT(100), 1);
  lv_obj_set_style_bg_color(line, UiTheme::softWhite(), 0);
  lv_obj_set_style_border_width(line, 0, 0);

  folder_list_ = lv_obj_create(panel);
  lv_obj_set_pos(folder_list_, 0, 66);
  lv_obj_set_size(folder_list_, LV_PCT(100), 495);
  lv_obj_set_style_bg_color(folder_list_, UiTheme::black(), 0);
  lv_obj_set_style_bg_opa(folder_list_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(folder_list_, 0, 0);
  lv_obj_set_style_radius(folder_list_, 0, 0);
  lv_obj_set_style_pad_all(folder_list_, 0, 0);
  lv_obj_set_style_pad_row(folder_list_, 0, 0);
  lv_obj_set_flex_flow(folder_list_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(folder_list_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(folder_list_, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_bg_color(folder_list_, UiTheme::white(),
                            LV_PART_SCROLLBAR);
  lv_obj_set_style_width(folder_list_, 3, LV_PART_SCROLLBAR);
  lv_obj_add_flag(folder_overlay_, LV_OBJ_FLAG_HIDDEN);
}

void UiPlayer::createEqOverlay(lv_obj_t *screen) {
  eq_overlay_ = lv_obj_create(screen);
  lv_obj_set_pos(eq_overlay_, 0, 0);
  lv_obj_set_size(eq_overlay_, kScreenWidth, kScreenHeight);
  lv_obj_set_style_bg_color(eq_overlay_, UiTheme::black(), 0);
  lv_obj_set_style_bg_opa(eq_overlay_, LV_OPA_90, 0);
  lv_obj_set_style_border_width(eq_overlay_, 0, 0);
  lv_obj_set_style_radius(eq_overlay_, 0, 0);
  lv_obj_set_style_pad_all(eq_overlay_, 0, 0);
  lv_obj_remove_flag(eq_overlay_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *panel = lv_obj_create(eq_overlay_);
  lv_obj_set_size(panel, 1120, 620);
  lv_obj_center(panel);
  UiTheme::panel(panel, 2);
  lv_obj_set_style_pad_all(panel, 22, 0);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(panel);
  lv_label_set_text(title, "PARAMETRIC EQ // 8 BAND");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  UiTheme::technicalLabel(title, UiTheme::white());
  lv_obj_set_pos(title, 0, 0);

  lv_obj_t *enable_label = lv_label_create(panel);
  lv_label_set_text(enable_label, "ACTIVE");
  UiTheme::technicalLabel(enable_label, UiTheme::softWhite());
  lv_obj_align(enable_label, LV_ALIGN_TOP_RIGHT, -225, 7);

  eq_switch_ = lv_switch_create(panel);
  lv_obj_set_size(eq_switch_, 54, 26);
  UiTheme::monochromeSwitch(eq_switch_);
  lv_obj_add_state(eq_switch_, LV_STATE_CHECKED);
  lv_obj_align(eq_switch_, LV_ALIGN_TOP_RIGHT, -158, 2);
  lv_obj_add_event_cb(eq_switch_, eqEnableCallback, LV_EVENT_VALUE_CHANGED,
                      this);

  lv_obj_t *flat = createSmallButton(panel, "FLAT", eqFlatCallback, this, 76);
  lv_obj_align(flat, LV_ALIGN_TOP_RIGHT, -48, -4);
  lv_obj_t *close = createSmallButton(panel, LV_SYMBOL_CLOSE, closeEqCallback,
                                      this, 38);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -4);

  lv_obj_t *line = lv_obj_create(panel);
  lv_obj_set_pos(line, 0, 50);
  lv_obj_set_size(line, LV_PCT(100), 1);
  lv_obj_set_style_bg_color(line, UiTheme::softWhite(), 0);
  lv_obj_set_style_border_width(line, 0, 0);

  lv_obj_t *bands = lv_obj_create(panel);
  lv_obj_set_pos(bands, 0, 66);
  lv_obj_set_size(bands, LV_PCT(100), 500);
  makePlainContainer(bands);
  lv_obj_set_flex_flow(bands, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bands, LV_FLEX_ALIGN_SPACE_AROUND,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  static constexpr const char *kBandNames[] = {"60",  "170", "310", "600",
                                                "1K",  "3K",  "6K",  "12K"};
  for (size_t band = 0; band < AudioEffects::kBandCount; ++band) {
    lv_obj_t *column = lv_obj_create(bands);
    lv_obj_set_size(column, 112, 490);
    makePlainContainer(column);

    eq_value_labels_[band] = lv_label_create(column);
    lv_label_set_text(eq_value_labels_[band], "+0");
    lv_obj_set_width(eq_value_labels_[band], 112);
    lv_obj_set_style_text_align(eq_value_labels_[band], LV_TEXT_ALIGN_CENTER,
                                0);
    lv_obj_set_style_text_font(eq_value_labels_[band], &lv_font_montserrat_20,
                               0);
    UiTheme::technicalLabel(eq_value_labels_[band], UiTheme::white());
    lv_obj_set_pos(eq_value_labels_[band], 0, 0);

    lv_obj_t *zero = lv_obj_create(column);
    lv_obj_set_pos(zero, 25, 232);
    lv_obj_set_size(zero, 62, 1);
    lv_obj_set_style_bg_color(zero, UiTheme::dim(), 0);
    lv_obj_set_style_border_width(zero, 0, 0);

    eq_sliders_[band] = lv_slider_create(column);
    lv_slider_set_range(eq_sliders_[band], -12, 12);
    lv_slider_set_mode(eq_sliders_[band], LV_SLIDER_MODE_SYMMETRICAL);
    lv_slider_set_orientation(eq_sliders_[band],
                              LV_SLIDER_ORIENTATION_VERTICAL);
    lv_slider_set_value(eq_sliders_[band], 0, LV_ANIM_OFF);
    lv_obj_set_size(eq_sliders_[band], 28, 380);
    lv_obj_set_pos(eq_sliders_[band], 42, 48);
    lv_obj_set_user_data(eq_sliders_[band], reinterpret_cast<void *>(band));
    UiTheme::thinSlider(eq_sliders_[band], true);
    lv_obj_add_event_cb(eq_sliders_[band], eqBandCallback,
                        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *frequency = lv_label_create(column);
    lv_label_set_text(frequency, kBandNames[band]);
    lv_obj_set_width(frequency, 112);
    lv_obj_set_style_text_align(frequency, LV_TEXT_ALIGN_CENTER, 0);
    UiTheme::technicalLabel(frequency, UiTheme::softWhite());
    lv_obj_set_pos(frequency, 0, 455);
  }
  lv_obj_add_flag(eq_overlay_, LV_OBJ_FLAG_HIDDEN);
}

void UiPlayer::showOverlay(lv_obj_t *overlay) {
  if (overlay == nullptr) {
    return;
  }
  lv_anim_delete(overlay, nullptr);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(overlay);
  lv_obj_fade_in(overlay, 130, 0);
}

void UiPlayer::hideOverlay(lv_obj_t *overlay) {
  if (overlay != nullptr) {
    lv_anim_delete(overlay, nullptr);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
  }
}

void UiPlayer::refresh() {
  if (player_ == nullptr) {
    return;
  }

  const uint32_t now = millis();
  const uint32_t elapsed = now - last_refresh_ms_;
  last_refresh_ms_ = now;

  // Never hide the active button's ancestor from inside its CLICKED event.
  // Complete event dispatch first, then close the browser on this UI tick;
  // the audio command is deliberately sent on a later tick as well.
  if (folder_close_pending_) {
    folder_close_pending_ = false;
    hideOverlay(folder_overlay_);
    pending_track_start_ms_ = now + kFolderLaunchDelayMs;
    return;
  }

  if (folder_selection_pending_ && pending_track_index_ >= 0 &&
      pending_track_start_ms_ != 0 &&
      static_cast<int32_t>(now - pending_track_start_ms_) >= 0) {
    const size_t index = static_cast<size_t>(pending_track_index_);
    pending_track_index_ = -1;
    folder_selection_pending_ = false;
    (void)player_->playIndex(index);
  }

  if (!snapshot_valid_ || now - last_state_refresh_ms_ >= kStateRefreshMs) {
    const PlayerSnapshot state = player_->snapshot();
    refreshPlayerState(state, !snapshot_valid_);
    cached_snapshot_ = state;
    snapshot_valid_ = true;
    last_state_refresh_ms_ = now;
  }

  const bool playing = cached_snapshot_.state == PlaybackState::Playing;
  cassette_.update(playing,
                   cached_snapshot_.state == PlaybackState::Stopped,
                   elapsed, cached_snapshot_.position_ms,
                   cached_snapshot_.duration_ms);

  uint16_t left_peak = 0;
  uint16_t right_peak = 0;
  player_->consumeMeterPeaks(left_peak, right_peak);
  level_meter_.update(left_peak, right_peak, elapsed, playing);
}

void UiPlayer::refreshPlayerState(const PlayerSnapshot &state,
                                  bool first_refresh) {
  if (state.playlist_revision != playlist_revision_) {
    rebuildFolderList();
  }

  if (first_refresh || state.title != cached_snapshot_.title) {
    setLabelText(title_label_, state.title.c_str());
  }

  if (first_refresh || state.artist != cached_snapshot_.artist) {
    String artist_text = "ARTIST // ";
    artist_text += state.artist.isEmpty() ? "--" : state.artist;
    setLabelText(artist_label_, artist_text.c_str());
  }

  if (first_refresh || state.state != cached_snapshot_.state ||
      state.status != cached_snapshot_.status) {
    char text[160];
    std::snprintf(text, sizeof(text), "%s // %s", stateName(state.state),
                  state.status.c_str());
    setLabelText(state_label_, text);
  }

  if (first_refresh ||
      state.headphones_connected != cached_snapshot_.headphones_connected ||
      state.speaker_enabled != cached_snapshot_.speaker_enabled) {
    const char *route = state.headphones_connected
                            ? "HP // SPK MUTED"
                            : (state.speaker_enabled ? "SPK ON" : "SPK OFF");
    setLabelText(route_label_, route);
    setSwitchState(speaker_switch_, state.speaker_enabled);
  }

  if (first_refresh || state.current_index != cached_snapshot_.current_index ||
      state.track_count != cached_snapshot_.track_count ||
      state.sample_rate != cached_snapshot_.sample_rate) {
    char text[96];
    std::snprintf(
        text, sizeof(text), "%02ld / %02u  //  %lu.%lu kHz",
        static_cast<long>(state.current_index + 1),
        static_cast<unsigned>(state.track_count),
        static_cast<unsigned long>(state.sample_rate / 1000),
        static_cast<unsigned long>((state.sample_rate % 1000) / 100));
    setLabelText(format_label_, text);
  }

  if (first_refresh || state.position_ms / 1000 !=
                           cached_snapshot_.position_ms / 1000 ||
      state.duration_ms / 1000 != cached_snapshot_.duration_ms / 1000) {
    char position[16];
    char duration[16];
    char text[40];
    formatTime(state.position_ms, position, sizeof(position));
    formatTime(state.duration_ms, duration, sizeof(duration));
    std::snprintf(text, sizeof(text), "%s / %s", position, duration);
    setLabelText(time_label_, text);
  }

  if (!lv_obj_has_state(progress_slider_, LV_STATE_PRESSED)) {
    const int32_t progress =
        state.duration_ms > 0
            ? static_cast<int32_t>(
                  static_cast<uint64_t>(state.position_ms) * 1000ULL /
                  state.duration_ms)
            : 0;
    setSliderValue(progress_slider_, std::min<int32_t>(progress, 1000));
  }

  if (!lv_obj_has_state(volume_slider_, LV_STATE_PRESSED) &&
      (first_refresh || state.volume != cached_snapshot_.volume)) {
    setSliderValue(volume_slider_, state.volume);
    char text[8];
    std::snprintf(text, sizeof(text), "%u", state.volume);
    setLabelText(volume_label_, text);
  }

  if (first_refresh || state.eq_enabled != cached_snapshot_.eq_enabled) {
    setSwitchState(eq_switch_, state.eq_enabled);
  }

  for (size_t band = 0; band < AudioEffects::kBandCount; ++band) {
    if (!lv_obj_has_state(eq_sliders_[band], LV_STATE_PRESSED) &&
        (first_refresh ||
         state.eq_gains[band] != cached_snapshot_.eq_gains[band])) {
      setSliderValue(eq_sliders_[band], state.eq_gains[band]);
      char text[8];
      std::snprintf(text, sizeof(text), "%+d", state.eq_gains[band]);
      setLabelText(eq_value_labels_[band], text);
    }
  }

  if (first_refresh || state.state != cached_snapshot_.state) {
    const bool playing = state.state == PlaybackState::Playing;
    setSwitchState(play_button_, playing);
    setLabelText(play_button_label_, playing ? LV_SYMBOL_PAUSE
                                             : LV_SYMBOL_PLAY);
  }

  syncPlaylistHighlight(state.current_index);
}

void UiPlayer::rebuildFolderList() {
  std::vector<AudioTrack> tracks;
  uint32_t revision = playlist_revision_;
  if (!player_->copyTracks(tracks, revision)) {
    return;
  }
  playlist_revision_ = revision;
  playlist_buttons_.clear();
  playlist_markers_.clear();
  lv_obj_clean(folder_list_);

  lv_label_set_text_fmt(folder_count_label_, "%u FILES",
                        static_cast<unsigned>(tracks.size()));
  if (tracks.empty()) {
    lv_obj_t *empty = lv_label_create(folder_list_);
    lv_label_set_text(empty, "> NO MP3 OR WAV FILES FOUND");
    UiTheme::technicalLabel(empty, UiTheme::dim());
    lv_obj_set_style_pad_all(empty, 18, 0);
    highlighted_index_ = -2;
    return;
  }

  playlist_buttons_.reserve(tracks.size());
  for (size_t index = 0; index < tracks.size(); ++index) {
    lv_obj_t *button = lv_button_create(folder_list_);
    lv_obj_set_size(button, LV_PCT(100), 52);
    lv_obj_set_style_bg_color(button, UiTheme::black(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, UiTheme::dark(), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_side(button, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_text_color(button, UiTheme::softWhite(), 0);
    lv_obj_set_style_bg_color(button, UiTheme::black(), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(button, UiTheme::white(), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(button, UiTheme::white(), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(button, UiTheme::white(), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, UiTheme::black(), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(button, 14, 0);
    // Store index + 1 so every valid row has a non-null, explicit token.
    // The callback also checks that the token still maps back to this button.
    lv_obj_set_user_data(
        button, reinterpret_cast<void *>(static_cast<uintptr_t>(index + 1U)));
    lv_obj_add_event_cb(button, folderTrackCallback, LV_EVENT_CLICKED, this);

    lv_obj_t *marker = lv_label_create(button);
    lv_label_set_text(marker, ">");
    lv_obj_set_style_text_font(marker, &lv_font_montserrat_20, 0);
    lv_obj_align(marker, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_width(label, LV_PCT(94));
    // A scrolling label owns an infinite LVGL animation even while the
    // browser is hidden. Static ellipsis keeps all rows deterministic.
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(label, tracks[index].name.c_str());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 28, 0);
    playlist_buttons_.push_back(button);
    playlist_markers_.push_back(marker);
  }
  highlighted_index_ = -2;
}

void UiPlayer::syncPlaylistHighlight(int32_t current_index) {
  if (highlighted_index_ == current_index) {
    return;
  }
  const size_t item_count =
      std::min(playlist_buttons_.size(), playlist_markers_.size());
  for (size_t index = 0; index < item_count; ++index) {
    if (static_cast<int32_t>(index) == current_index) {
      lv_obj_add_state(playlist_buttons_[index], LV_STATE_CHECKED);
      lv_obj_remove_flag(playlist_markers_[index], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_state(playlist_buttons_[index], LV_STATE_CHECKED);
      lv_obj_add_flag(playlist_markers_[index], LV_OBJ_FLAG_HIDDEN);
    }
  }
  highlighted_index_ = current_index;
}
