#pragma once

#include <Arduino.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio_effects.h"
#include "audio_metadata.h"
#include "audio_output.h"
#include "sd_browser.h"

class AudioFileSource;
class AudioFileSourceBuffer;
class AudioFileSourceFS;
class AudioFileSourceID3;
class AudioGenerator;

enum class PlaybackState : uint8_t {
  Initializing,
  Scanning,
  Stopped,
  Playing,
  Paused,
  Error,
};

struct PlayerSnapshot {
  PlaybackState state = PlaybackState::Initializing;
  String title = "Starting...";
  String artist;
  String status = "Initializing audio";
  int32_t current_index = -1;
  size_t track_count = 0;
  uint32_t playlist_revision = 0;
  uint32_t position_ms = 0;
  uint32_t duration_ms = 0;
  uint32_t sample_rate = 0;
  uint8_t volume = 55;
  bool speaker_enabled = true;
  bool headphones_connected = false;
  bool eq_enabled = true;
  std::array<int8_t, AudioEffects::kBandCount> eq_gains{};
};

class AudioPlayer {
 public:
  AudioPlayer();
  ~AudioPlayer() = default;

  bool begin(fs::FS *filesystem, bool filesystem_mounted);

  bool playIndex(size_t index);
  bool togglePlayPause();
  bool stop();
  bool previous();
  bool next();
  bool seekPermille(uint16_t position);
  bool setVolume(uint8_t percent);
  bool setSpeakerEnabled(bool enabled);
  bool setEqEnabled(bool enabled);
  bool setEqBandGain(size_t band, int8_t gain_db);
  bool setEqFlat();
  bool rescan();

  PlayerSnapshot snapshot() const;
  bool copyTracks(std::vector<AudioTrack> &tracks,
                  uint32_t &playlist_revision) const;
  void consumeMeterPeaks(uint16_t &left, uint16_t &right);

 private:
  enum class CommandType : uint8_t {
    PlayIndex,
    TogglePlayPause,
    Stop,
    Previous,
    Next,
    Seek,
    Volume,
    SpeakerEnable,
    EqEnable,
    EqBand,
    EqFlat,
    Rescan,
  };

  struct Command {
    CommandType type;
    int32_t value = 0;
    int32_t secondary = 0;
  };

  static constexpr size_t kFileBufferBytes = 64 * 1024;
  static constexpr uint8_t kSpeakerChannel = 0;
  static constexpr uint8_t kHeadphoneDetectPin = 7;
  static constexpr uint8_t kSpeakerEnablePin = 1;
  static constexpr uint32_t kHeadphonePollIntervalMs = 75;
  static constexpr uint8_t kHeadphoneDebounceSamples = 3;

  static void taskEntry(void *argument);
  static void metadataCallback(void *argument, const char *type,
                               bool is_unicode, const char *text);
  static void speakerRestartedCallback(void *argument);

  bool enqueue(CommandType type, int32_t value = 0,
               int32_t secondary = 0);
  void taskLoop();
  void processCommand(const Command &command);
  void scanTracks();
  bool startTrack(size_t index, uint16_t seek_permille = 0,
                  bool start_paused = false,
                  bool preserve_display_metadata = false);
  void stopPlayback(bool update_snapshot);
  void finishTrackAndAdvance();
  void cleanupDecoder();
  void updateProgress();
  void pollHeadphoneState(bool initialize = false);
  void applySpeakerRouting(bool force = false);
  void setState(PlaybackState state, const String &status);
  void setError(const String &message);

  fs::FS *filesystem_ = nullptr;
  bool filesystem_mounted_ = false;
  mutable SemaphoreHandle_t state_mutex_ = nullptr;
  QueueHandle_t command_queue_ = nullptr;
  TaskHandle_t task_handle_ = nullptr;

  std::vector<AudioTrack> tracks_;
  PlayerSnapshot snapshot_;
  PlaybackState state_ = PlaybackState::Initializing;
  int32_t current_index_ = -1;
  uint32_t position_base_ms_ = 0;
  uint32_t last_progress_update_ms_ = 0;
  uint32_t last_headphone_poll_ms_ = 0;
  bool speaker_user_enabled_ = true;
  bool headphones_connected_ = false;
  bool headphone_candidate_ = false;
  bool speaker_amp_enabled_ = true;
  uint8_t headphone_candidate_samples_ = 0;
  AudioMetadata current_metadata_;

  AudioFileSourceFS *raw_source_ = nullptr;
  AudioFileSourceBuffer *buffered_source_ = nullptr;
  AudioFileSourceID3 *id3_source_ = nullptr;
  AudioFileSource *decoder_source_ = nullptr;
  AudioGenerator *decoder_ = nullptr;
  uint8_t *file_buffer_ = nullptr;
  void *mp3_memory_ = nullptr;

  AudioOutputM5SpeakerEq output_;
  SdBrowser browser_;
};
