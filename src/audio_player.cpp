#include "audio_player.h"

#include <AudioFileSourceBuffer.h>
#include <AudioFileSourceFS.h>
#include <AudioFileSourceID3.h>
#include <AudioGenerator.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <new>

namespace {

class SemaphoreGuard {
 public:
  explicit SemaphoreGuard(SemaphoreHandle_t semaphore) : semaphore_(semaphore) {
    if (semaphore_ != nullptr) {
      xSemaphoreTake(semaphore_, portMAX_DELAY);
    }
  }
  ~SemaphoreGuard() {
    if (semaphore_ != nullptr) {
      xSemaphoreGive(semaphore_);
    }
  }

 private:
  SemaphoreHandle_t semaphore_;
};

String artistFromFileName(const String &name) {
  const int separator = name.indexOf(" - ");
  return separator > 0 ? name.substring(0, separator) : String();
}

}  // namespace

AudioPlayer::AudioPlayer() : output_(&M5.Speaker, kSpeakerChannel) {
  snapshot_.eq_gains.fill(0);
  output_.setSpeakerRestartedCallback(speakerRestartedCallback, this);
}

bool AudioPlayer::begin(fs::FS *filesystem, bool filesystem_mounted) {
  filesystem_ = filesystem;
  filesystem_mounted_ = filesystem_mounted;
  state_mutex_ = xSemaphoreCreateMutex();
  command_queue_ = xQueueCreate(32, sizeof(Command));
  if (state_mutex_ == nullptr || command_queue_ == nullptr) {
    return false;
  }

  const BaseType_t created = xTaskCreatePinnedToCore(
      taskEntry, "audio_player", 16 * 1024, this, 5, &task_handle_, 0);
  return created == pdPASS;
}

bool AudioPlayer::playIndex(size_t index) {
  return enqueue(CommandType::PlayIndex, static_cast<int32_t>(index));
}

bool AudioPlayer::togglePlayPause() {
  return enqueue(CommandType::TogglePlayPause);
}

bool AudioPlayer::stop() { return enqueue(CommandType::Stop); }

bool AudioPlayer::previous() { return enqueue(CommandType::Previous); }

bool AudioPlayer::next() { return enqueue(CommandType::Next); }

bool AudioPlayer::seekPermille(uint16_t position) {
  return enqueue(CommandType::Seek, std::min<uint16_t>(position, 1000));
}

bool AudioPlayer::setVolume(uint8_t percent) {
  return enqueue(CommandType::Volume, std::min<uint8_t>(percent, 100));
}

bool AudioPlayer::setSpeakerEnabled(bool enabled) {
  return enqueue(CommandType::SpeakerEnable, enabled ? 1 : 0);
}

bool AudioPlayer::setEqEnabled(bool enabled) {
  return enqueue(CommandType::EqEnable, enabled ? 1 : 0);
}

bool AudioPlayer::setEqBandGain(size_t band, int8_t gain_db) {
  if (band >= AudioEffects::kBandCount) {
    return false;
  }
  return enqueue(CommandType::EqBand, static_cast<int32_t>(band), gain_db);
}

bool AudioPlayer::setEqFlat() { return enqueue(CommandType::EqFlat); }

bool AudioPlayer::rescan() { return enqueue(CommandType::Rescan); }

PlayerSnapshot AudioPlayer::snapshot() const {
  SemaphoreGuard lock(state_mutex_);
  return snapshot_;
}

bool AudioPlayer::copyTracks(std::vector<AudioTrack> &tracks,
                             uint32_t &playlist_revision) const {
  SemaphoreGuard lock(state_mutex_);
  if (playlist_revision == snapshot_.playlist_revision) {
    return false;
  }
  tracks = tracks_;
  playlist_revision = snapshot_.playlist_revision;
  return true;
}

void AudioPlayer::consumeMeterPeaks(uint16_t &left, uint16_t &right) {
  output_.consumeMeterPeaks(left, right);
}

void AudioPlayer::taskEntry(void *argument) {
  static_cast<AudioPlayer *>(argument)->taskLoop();
}

void AudioPlayer::metadataCallback(void *argument, const char *type,
                                   bool is_unicode, const char *text) {
  auto *player = static_cast<AudioPlayer *>(argument);
  if (player == nullptr || type == nullptr || text == nullptr || is_unicode ||
      text[0] == '\0') {
    return;
  }
  SemaphoreGuard lock(player->state_mutex_);
  if (std::strcmp(type, "Title") == 0) {
    player->snapshot_.title = text;
  } else if (std::strcmp(type, "Performer") == 0) {
    player->snapshot_.artist = text;
  }
}

void AudioPlayer::speakerRestartedCallback(void *argument) {
  auto *player = static_cast<AudioPlayer *>(argument);
  if (player != nullptr) {
    player->applySpeakerRouting(true);
  }
}

bool AudioPlayer::enqueue(CommandType type, int32_t value,
                          int32_t secondary) {
  if (command_queue_ == nullptr) {
    return false;
  }
  const Command command{type, value, secondary};
  return xQueueSend(command_queue_, &command, 0) == pdTRUE;
}

void AudioPlayer::taskLoop() {
  M5.Speaker.setVolume(static_cast<uint8_t>(snapshot_.volume * 255U / 100U));
  output_.setEqEnabled(snapshot_.eq_enabled);
  pollHeadphoneState(true);
  scanTracks();

  while (true) {
    Command command;
    const TickType_t wait = state_ == PlaybackState::Playing ? 0 : pdMS_TO_TICKS(10);
    if (xQueueReceive(command_queue_, &command, wait) == pdTRUE) {
      processCommand(command);
      while (xQueueReceive(command_queue_, &command, 0) == pdTRUE) {
        processCommand(command);
      }
    }

    pollHeadphoneState();

    if (state_ == PlaybackState::Playing && decoder_ != nullptr) {
      if (!decoder_->loop()) {
        finishTrackAndAdvance();
      } else {
        updateProgress();
      }
    } else {
      vTaskDelay(1);
    }
  }
}

void AudioPlayer::processCommand(const Command &command) {
  switch (command.type) {
    case CommandType::PlayIndex:
      (void)startTrack(static_cast<size_t>(command.value));
      break;

    case CommandType::TogglePlayPause:
      if (state_ == PlaybackState::Playing) {
        setState(PlaybackState::Paused, "Paused");
      } else if (state_ == PlaybackState::Paused) {
        setState(PlaybackState::Playing, "Playing");
      } else if (!tracks_.empty()) {
        const size_t index = current_index_ >= 0
                                 ? static_cast<size_t>(current_index_)
                                 : 0;
        (void)startTrack(index);
      }
      break;

    case CommandType::Stop:
      stopPlayback(true);
      break;

    case CommandType::Previous:
      if (!tracks_.empty()) {
        size_t index = current_index_ >= 0
                           ? static_cast<size_t>(current_index_)
                           : 0;
        if (position_base_ms_ + output_.renderedMilliseconds() < 5000) {
          index = (index + tracks_.size() - 1) % tracks_.size();
        }
        (void)startTrack(index);
      }
      break;

    case CommandType::Next:
      if (!tracks_.empty()) {
        const size_t index = current_index_ >= 0
                                 ? (static_cast<size_t>(current_index_) + 1) %
                                       tracks_.size()
                                 : 0;
        (void)startTrack(index);
      }
      break;

    case CommandType::Seek:
      if (current_index_ >= 0 && decoder_ != nullptr) {
        const bool paused = state_ == PlaybackState::Paused;
        (void)startTrack(static_cast<size_t>(current_index_),
                         static_cast<uint16_t>(command.value), paused, true);
      }
      break;

    case CommandType::Volume: {
      const uint8_t volume = static_cast<uint8_t>(command.value);
      M5.Speaker.setVolume(static_cast<uint8_t>(volume * 255U / 100U));
      SemaphoreGuard lock(state_mutex_);
      snapshot_.volume = volume;
      break;
    }

    case CommandType::SpeakerEnable: {
      speaker_user_enabled_ = command.value != 0;
      {
        SemaphoreGuard lock(state_mutex_);
        snapshot_.speaker_enabled = speaker_user_enabled_;
      }
      applySpeakerRouting();
      break;
    }

    case CommandType::EqEnable: {
      const bool enabled = command.value != 0;
      output_.setEqEnabled(enabled);
      SemaphoreGuard lock(state_mutex_);
      snapshot_.eq_enabled = enabled;
      break;
    }

    case CommandType::EqBand: {
      const size_t band = static_cast<size_t>(command.value);
      const int8_t gain = static_cast<int8_t>(
          std::max<int32_t>(-12, std::min<int32_t>(12, command.secondary)));
      output_.setEqBandGain(band, gain);
      SemaphoreGuard lock(state_mutex_);
      snapshot_.eq_gains[band] = gain;
      break;
    }

    case CommandType::EqFlat: {
      output_.setEqFlat();
      SemaphoreGuard lock(state_mutex_);
      snapshot_.eq_gains.fill(0);
      break;
    }

    case CommandType::Rescan:
      stopPlayback(false);
      scanTracks();
      break;
  }
}

void AudioPlayer::pollHeadphoneState(bool initialize) {
  const uint32_t now = millis();
  if (!initialize && now - last_headphone_poll_ms_ < kHeadphonePollIntervalMs) {
    return;
  }
  last_headphone_poll_ms_ = now;

  // The Tab5 reference design exposes HP_DET as an active-high input on P7
  // of the first PI4IOE5V6408 I/O expander (I2C address 0x43).
  const bool detected = M5.getIOExpander(0).digitalRead(kHeadphoneDetectPin);
  if (initialize) {
    headphone_candidate_ = detected;
    headphone_candidate_samples_ = kHeadphoneDebounceSamples;
  } else if (detected != headphone_candidate_) {
    headphone_candidate_ = detected;
    headphone_candidate_samples_ = 1;
    return;
  } else if (headphone_candidate_samples_ < kHeadphoneDebounceSamples) {
    ++headphone_candidate_samples_;
  }

  if (headphone_candidate_samples_ < kHeadphoneDebounceSamples ||
      headphones_connected_ == headphone_candidate_) {
    return;
  }

  headphones_connected_ = headphone_candidate_;
  {
    SemaphoreGuard lock(state_mutex_);
    snapshot_.headphones_connected = headphones_connected_;
  }
  applySpeakerRouting();
}

void AudioPlayer::applySpeakerRouting(bool force) {
  // P1 controls only the built-in power amplifier. The ES8388 and I2S stay
  // active, so headphone playback continues without a decoder restart.
  const bool enable_amp = speaker_user_enabled_ && !headphones_connected_;
  if (force || enable_amp != speaker_amp_enabled_) {
    M5.getIOExpander(0).digitalWrite(kSpeakerEnablePin, enable_amp);
    speaker_amp_enabled_ = enable_amp;
  }
}

void AudioPlayer::scanTracks() {
  setState(PlaybackState::Scanning, "Scanning SD card...");
  if (!filesystem_mounted_ || filesystem_ == nullptr) {
    setError("SD card is not mounted");
    return;
  }

  std::vector<AudioTrack> found;
  String error;
  const bool success = browser_.scan(*filesystem_, found, error);
  {
    SemaphoreGuard lock(state_mutex_);
    tracks_ = std::move(found);
    snapshot_.track_count = tracks_.size();
    ++snapshot_.playlist_revision;
    snapshot_.current_index = -1;
    snapshot_.position_ms = 0;
    snapshot_.duration_ms = 0;
    snapshot_.title = tracks_.empty() ? "No audio files" : "Select a track";
    snapshot_.artist = "";
  }
  current_index_ = -1;

  if (!success) {
    setError(error);
  } else if (tracks_.empty()) {
    setState(PlaybackState::Stopped, "No MP3 or WAV files found");
  } else if (!error.isEmpty()) {
    setState(PlaybackState::Stopped, error);
  } else {
    setState(PlaybackState::Stopped,
             String(tracks_.size()) + " tracks ready");
  }
}

bool AudioPlayer::startTrack(size_t index, uint16_t seek_permille,
                             bool start_paused,
                             bool preserve_display_metadata) {
  if (filesystem_ == nullptr || index >= tracks_.size()) {
    return false;
  }

  const AudioTrack track = tracks_[index];
  String display_title = track.name;
  String display_artist = artistFromFileName(track.name);

  if (preserve_display_metadata &&
      current_index_ == static_cast<int32_t>(index)) {
    SemaphoreGuard lock(state_mutex_);
    if (!snapshot_.title.isEmpty()) {
      display_title = snapshot_.title;
    }
    if (!snapshot_.artist.isEmpty()) {
      display_artist = snapshot_.artist;
    }
  }

  stopPlayback(false);
  {
    SemaphoreGuard lock(state_mutex_);
    snapshot_.title = display_title;
    snapshot_.artist = display_artist;
  }
  if (!readAudioMetadata(*filesystem_, track, current_metadata_)) {
    current_metadata_.audio_start = 0;
    current_metadata_.audio_bytes = track.size;
    current_metadata_.block_align = 1;
  }

  raw_source_ = new (std::nothrow) AudioFileSourceFS(*filesystem_, track.path.c_str());
  if (raw_source_ == nullptr || !raw_source_->isOpen()) {
    setError("Cannot open: " + track.name);
    cleanupDecoder();
    return false;
  }

  file_buffer_ = static_cast<uint8_t *>(heap_caps_malloc(
      kFileBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (file_buffer_ == nullptr) {
    file_buffer_ = static_cast<uint8_t *>(malloc(kFileBufferBytes));
  }
  if (file_buffer_ == nullptr) {
    setError("No memory for the SD audio buffer");
    cleanupDecoder();
    return false;
  }

  buffered_source_ = new (std::nothrow)
      AudioFileSourceBuffer(raw_source_, file_buffer_, kFileBufferBytes);
  if (buffered_source_ == nullptr) {
    setError("Cannot create the SD audio buffer");
    cleanupDecoder();
    return false;
  }

  if (track.format == AudioFormat::Mp3) {
    id3_source_ = new (std::nothrow) AudioFileSourceID3(buffered_source_);
    mp3_memory_ = heap_caps_malloc(AudioGeneratorMP3::preAllocSize(),
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (id3_source_ == nullptr || mp3_memory_ == nullptr) {
      setError("No memory for the MP3 decoder");
      cleanupDecoder();
      return false;
    }
    id3_source_->RegisterMetadataCB(metadataCallback, this);
    decoder_source_ = id3_source_;
    decoder_ = new (std::nothrow) AudioGeneratorMP3(
        mp3_memory_, AudioGeneratorMP3::preAllocSize());
  } else {
    decoder_source_ = buffered_source_;
    auto *wav = new (std::nothrow) AudioGeneratorWAV();
    if (wav != nullptr) {
      wav->SetBufferSize(4096);
    }
    decoder_ = wav;
  }

  if (decoder_ == nullptr || !decoder_->begin(decoder_source_, &output_)) {
    setError("Unsupported or damaged audio file: " + track.name);
    cleanupDecoder();
    return false;
  }

  seek_permille = std::min<uint16_t>(seek_permille, 1000);
  position_base_ms_ = static_cast<uint32_t>(
      static_cast<uint64_t>(current_metadata_.duration_ms) * seek_permille /
      1000ULL);
  output_.resetRenderedTime();

  if (seek_permille > 0 && current_metadata_.audio_bytes > 0) {
    uint32_t target = current_metadata_.audio_start +
                      static_cast<uint32_t>(
                          static_cast<uint64_t>(current_metadata_.audio_bytes) *
                          seek_permille / 1000ULL);
    if (track.format == AudioFormat::Wav && current_metadata_.block_align > 1) {
      const uint32_t relative = target - current_metadata_.audio_start;
      target = current_metadata_.audio_start +
               relative / current_metadata_.block_align *
                   current_metadata_.block_align;
    }
    if (!decoder_source_->seek(static_cast<int32_t>(target), SEEK_SET)) {
      Serial.printf("Seek failed at byte %u in %s\n", target,
                    track.path.c_str());
    } else {
      decoder_->desync();
    }
  }

  current_index_ = static_cast<int32_t>(index);
  {
    SemaphoreGuard lock(state_mutex_);
    snapshot_.current_index = current_index_;
    snapshot_.duration_ms = current_metadata_.duration_ms;
    snapshot_.position_ms = position_base_ms_;
    snapshot_.sample_rate = current_metadata_.sample_rate;
  }
  setState(start_paused ? PlaybackState::Paused : PlaybackState::Playing,
           start_paused ? "Paused" : "Playing");
  return true;
}

void AudioPlayer::stopPlayback(bool update_snapshot) {
  if (decoder_ != nullptr && decoder_->isRunning()) {
    (void)decoder_->stop();
  } else {
    (void)output_.stop();
  }
  cleanupDecoder();
  position_base_ms_ = 0;
  output_.resetRenderedTime();

  if (update_snapshot) {
    SemaphoreGuard lock(state_mutex_);
    snapshot_.position_ms = 0;
    snapshot_.status = "Stopped";
    snapshot_.state = PlaybackState::Stopped;
    state_ = PlaybackState::Stopped;
  }
}

void AudioPlayer::finishTrackAndAdvance() {
  output_.flush();
  (void)output_.waitForDrain(500);
  if (decoder_ != nullptr && decoder_->isRunning()) {
    (void)decoder_->stop();
  }
  cleanupDecoder();

  if (tracks_.empty()) {
    setState(PlaybackState::Stopped, "Stopped");
    return;
  }
  const size_t next_index = current_index_ >= 0
                                ? (static_cast<size_t>(current_index_) + 1) %
                                      tracks_.size()
                                : 0;
  (void)startTrack(next_index);
}

void AudioPlayer::cleanupDecoder() {
  delete decoder_;
  decoder_ = nullptr;
  decoder_source_ = nullptr;
  delete id3_source_;
  id3_source_ = nullptr;
  delete buffered_source_;
  buffered_source_ = nullptr;
  delete raw_source_;
  raw_source_ = nullptr;
  if (file_buffer_ != nullptr) {
    heap_caps_free(file_buffer_);
    file_buffer_ = nullptr;
  }
  if (mp3_memory_ != nullptr) {
    heap_caps_free(mp3_memory_);
    mp3_memory_ = nullptr;
  }
}

void AudioPlayer::updateProgress() {
  const uint32_t now = millis();
  if (now - last_progress_update_ms_ < 50) {
    return;
  }
  last_progress_update_ms_ = now;
  uint32_t position = position_base_ms_ + output_.renderedMilliseconds();
  if (current_metadata_.duration_ms > 0) {
    position = std::min(position, current_metadata_.duration_ms);
  }

  SemaphoreGuard lock(state_mutex_);
  snapshot_.position_ms = position;
  snapshot_.sample_rate = output_.sampleRate();
}

void AudioPlayer::setState(PlaybackState state, const String &status) {
  state_ = state;
  SemaphoreGuard lock(state_mutex_);
  snapshot_.state = state;
  snapshot_.status = status;
}

void AudioPlayer::setError(const String &message) {
  Serial.printf("Audio player error: %s\n", message.c_str());
  setState(PlaybackState::Error, message);
}
