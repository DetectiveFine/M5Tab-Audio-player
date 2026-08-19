#pragma once

#include <lvgl.h>

#include <array>
#include <cstdint>
#include <vector>

#include "audio_player.h"
#include "cassette_widget.h"
#include "level_meter.h"

class UiPlayer {
 public:
  void begin(AudioPlayer &player);

 private:
  static void timerCallback(lv_timer_t *timer);
  static void playPauseCallback(lv_event_t *event);
  static void stopCallback(lv_event_t *event);
  static void previousCallback(lv_event_t *event);
  static void nextCallback(lv_event_t *event);
  static void seekCallback(lv_event_t *event);
  static void volumeCallback(lv_event_t *event);
  static void speakerEnableCallback(lv_event_t *event);
  static void openFolderCallback(lv_event_t *event);
  static void closeFolderCallback(lv_event_t *event);
  static void folderTrackCallback(lv_event_t *event);
  static void rescanCallback(lv_event_t *event);
  static void openEqCallback(lv_event_t *event);
  static void closeEqCallback(lv_event_t *event);
  static void eqEnableCallback(lv_event_t *event);
  static void eqFlatCallback(lv_event_t *event);
  static void eqBandCallback(lv_event_t *event);

  static const char *stateName(PlaybackState state);
  static void formatTime(uint32_t milliseconds, char *buffer,
                         size_t buffer_size);
  static lv_obj_t *createControlButton(lv_obj_t *parent, const char *text,
                                       lv_event_cb_t callback,
                                       void *user_data, int32_t width,
                                       bool emphasized = false);
  static lv_obj_t *createSmallButton(lv_obj_t *parent, const char *text,
                                     lv_event_cb_t callback,
                                     void *user_data, int32_t width);

  void createLayout();
  void createMainView(lv_obj_t *screen);
  void createControls(lv_obj_t *screen);
  void createFolderOverlay(lv_obj_t *screen);
  void createEqOverlay(lv_obj_t *screen);
  void showOverlay(lv_obj_t *overlay);
  void hideOverlay(lv_obj_t *overlay);
  void refresh();
  void refreshPlayerState(const PlayerSnapshot &state, bool first_refresh);
  void rebuildFolderList();
  void syncPlaylistHighlight(int32_t current_index);

  AudioPlayer *player_ = nullptr;
  uint32_t playlist_revision_ = 0;
  uint32_t last_refresh_ms_ = 0;
  uint32_t last_state_refresh_ms_ = 0;
  int32_t highlighted_index_ = -2;
  bool snapshot_valid_ = false;
  bool folder_selection_pending_ = false;
  bool folder_close_pending_ = false;
  int32_t pending_track_index_ = -1;
  uint32_t pending_track_start_ms_ = 0;
  PlayerSnapshot cached_snapshot_{};

  CassetteWidget cassette_;
  StereoLevelMeter level_meter_;

  lv_obj_t *artist_label_ = nullptr;
  lv_obj_t *title_label_ = nullptr;
  lv_obj_t *state_label_ = nullptr;
  lv_obj_t *route_label_ = nullptr;
  lv_obj_t *format_label_ = nullptr;
  lv_obj_t *time_label_ = nullptr;
  lv_obj_t *progress_slider_ = nullptr;
  lv_obj_t *play_button_ = nullptr;
  lv_obj_t *play_button_label_ = nullptr;
  lv_obj_t *volume_slider_ = nullptr;
  lv_obj_t *volume_label_ = nullptr;
  lv_obj_t *speaker_switch_ = nullptr;

  lv_obj_t *folder_overlay_ = nullptr;
  lv_obj_t *folder_list_ = nullptr;
  lv_obj_t *folder_count_label_ = nullptr;
  std::vector<lv_obj_t *> playlist_buttons_;
  std::vector<lv_obj_t *> playlist_markers_;

  lv_obj_t *eq_overlay_ = nullptr;
  lv_obj_t *eq_switch_ = nullptr;
  std::array<lv_obj_t *, AudioEffects::kBandCount> eq_sliders_{};
  std::array<lv_obj_t *, AudioEffects::kBandCount> eq_value_labels_{};
};
