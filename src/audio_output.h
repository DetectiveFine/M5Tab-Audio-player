#pragma once

#include <AudioOutput.h>
#include <M5Unified.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "audio_effects.h"

class AudioOutputM5SpeakerEq final : public AudioOutput {
 public:
  using SpeakerRestartedCallback = void (*)(void *argument);

  explicit AudioOutputM5SpeakerEq(m5::Speaker_Class *speaker,
                                  uint8_t virtual_channel = 0);
  ~AudioOutputM5SpeakerEq() override = default;

  bool SetRate(int hz) override;
  bool SetChannels(int channels) override;
  bool begin() override;
  bool ConsumeSample(int16_t sample[2]) override;
  void flush() override;
  bool stop() override;

  void setEqEnabled(bool enabled);
  void setEqBandGain(size_t band, int8_t gain_db);
  void setEqFlat();
  bool eqAvailable() const { return effects_.available(); }
  void setSpeakerRestartedCallback(SpeakerRestartedCallback callback,
                                   void *argument);

  void resetRenderedTime();
  uint32_t renderedMilliseconds() const;
  uint32_t sampleRate() const { return sample_rate_; }
  bool waitForDrain(uint32_t timeout_ms);
  void consumeMeterPeaks(uint16_t &left, uint16_t &right);

 private:
  static constexpr size_t kSamplesPerBuffer = 1536;
  static constexpr size_t kBufferCount = 3;

  bool configureNativeOutputRate(uint32_t sample_rate);
  void captureMeterPeaks(const int16_t *samples, size_t frame_count);
  static void updateAtomicPeak(std::atomic<uint16_t> &peak, uint16_t value);

  m5::Speaker_Class *speaker_ = nullptr;
  uint8_t virtual_channel_ = 0;
  alignas(16) int16_t buffers_[kBufferCount][kSamplesPerBuffer]{};
  size_t buffer_index_ = 0;
  size_t active_buffer_ = 0;
  uint32_t sample_rate_ = 44100;
  uint32_t configured_rate_ = 0;
  uint64_t rendered_microseconds_ = 0;
  SpeakerRestartedCallback speaker_restarted_callback_ = nullptr;
  void *speaker_restarted_argument_ = nullptr;
  std::atomic<uint16_t> meter_peak_left_{0};
  std::atomic<uint16_t> meter_peak_right_{0};
  AudioEffects effects_;
};
