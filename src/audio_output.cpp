#include "audio_output.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdlib>

AudioOutputM5SpeakerEq::AudioOutputM5SpeakerEq(
    m5::Speaker_Class *speaker, uint8_t virtual_channel)
    : speaker_(speaker), virtual_channel_(virtual_channel) {
  hertz = 44100;
  channels = 2;
  gainF2P6 = 1 << 6;
}

bool AudioOutputM5SpeakerEq::SetRate(int hz) {
  if (hz <= 0) {
    return false;
  }
  const uint32_t requested_rate = static_cast<uint32_t>(hz);
  if (sample_rate_ == requested_rate && speaker_ != nullptr) {
    const auto config = speaker_->config();
    if (config.sample_rate == requested_rate && config.stereo) {
      return true;
    }
  }

  if (!configureNativeOutputRate(requested_rate)) {
    return false;
  }

  // ESP8266Audio's legacy base member is 16-bit. Keep it populated for base
  // compatibility, but use the full-width rate throughout this P4 output.
  hertz = static_cast<uint16_t>(hz);
  sample_rate_ = requested_rate;
  configured_rate_ = 0;
  return true;
}

bool AudioOutputM5SpeakerEq::SetChannels(int channel_count) {
  if (channel_count < 1 || channel_count > 2) {
    return false;
  }
  channels = static_cast<uint8_t>(channel_count);
  return true;
}

bool AudioOutputM5SpeakerEq::begin() {
  return speaker_ != nullptr && speaker_->begin();
}

bool AudioOutputM5SpeakerEq::ConsumeSample(int16_t sample[2]) {
  if (sample == nullptr) {
    return false;
  }
  if (buffer_index_ + 2 <= kSamplesPerBuffer) {
    MakeSampleStereo16(sample);
    buffers_[active_buffer_][buffer_index_++] = sample[0];
    buffers_[active_buffer_][buffer_index_++] = sample[1];
    return true;
  }

  flush();
  return false;
}

void AudioOutputM5SpeakerEq::flush() {
  if (buffer_index_ == 0 || speaker_ == nullptr || sample_rate_ == 0) {
    return;
  }

  // Keep this real-time PCM path free of Serial I/O. Track-level failures are
  // reported by the player state machine without adding jitter here.
  if (configured_rate_ != sample_rate_) {
    (void)effects_.configure(sample_rate_, 2);
    configured_rate_ = sample_rate_;
  }

  const size_t frame_count = buffer_index_ / 2;
  (void)effects_.process(buffers_[active_buffer_], frame_count);
  captureMeterPeaks(buffers_[active_buffer_], frame_count);

  const bool queued = speaker_->playRaw(
      buffers_[active_buffer_], buffer_index_, sample_rate_, true, 1,
      virtual_channel_, false);
  if (queued) {
    rendered_microseconds_ +=
        static_cast<uint64_t>(frame_count) * 1000000ULL / sample_rate_;
    active_buffer_ = (active_buffer_ + 1) % kBufferCount;
  }
  buffer_index_ = 0;
}

bool AudioOutputM5SpeakerEq::stop() {
  buffer_index_ = 0;
  if (speaker_ != nullptr) {
    speaker_->stop(virtual_channel_);
  }
  effects_.reset();
  return true;
}

void AudioOutputM5SpeakerEq::setEqEnabled(bool enabled) {
  effects_.setEnabled(enabled);
}

void AudioOutputM5SpeakerEq::setEqBandGain(size_t band, int8_t gain_db) {
  (void)effects_.setBandGain(band, gain_db);
}

void AudioOutputM5SpeakerEq::setEqFlat() { effects_.flat(); }

void AudioOutputM5SpeakerEq::setSpeakerRestartedCallback(
    SpeakerRestartedCallback callback, void *argument) {
  speaker_restarted_callback_ = callback;
  speaker_restarted_argument_ = argument;
}

void AudioOutputM5SpeakerEq::resetRenderedTime() {
  rendered_microseconds_ = 0;
  effects_.reset();
}

uint32_t AudioOutputM5SpeakerEq::renderedMilliseconds() const {
  return static_cast<uint32_t>(rendered_microseconds_ / 1000ULL);
}

bool AudioOutputM5SpeakerEq::waitForDrain(uint32_t timeout_ms) {
  const uint32_t start = millis();
  while (speaker_ != nullptr && speaker_->isPlaying(virtual_channel_) &&
         millis() - start < timeout_ms) {
    vTaskDelay(1);
  }
  return speaker_ == nullptr || !speaker_->isPlaying(virtual_channel_);
}

void AudioOutputM5SpeakerEq::consumeMeterPeaks(uint16_t &left,
                                               uint16_t &right) {
  left = meter_peak_left_.exchange(0, std::memory_order_relaxed);
  right = meter_peak_right_.exchange(0, std::memory_order_relaxed);
}

void AudioOutputM5SpeakerEq::captureMeterPeaks(const int16_t *samples,
                                               size_t frame_count) {
  uint16_t left = 0;
  uint16_t right = 0;
  for (size_t frame = 0; frame < frame_count; ++frame) {
    const int32_t l = samples[frame * 2];
    const int32_t r = samples[frame * 2 + 1];
    left = std::max<uint16_t>(
        left, static_cast<uint16_t>(std::min<int32_t>(32768, std::abs(l))));
    right = std::max<uint16_t>(
        right, static_cast<uint16_t>(std::min<int32_t>(32768, std::abs(r))));
  }
  updateAtomicPeak(meter_peak_left_, left);
  updateAtomicPeak(meter_peak_right_, right);
}

void AudioOutputM5SpeakerEq::updateAtomicPeak(std::atomic<uint16_t> &peak,
                                              uint16_t value) {
  uint16_t current = peak.load(std::memory_order_relaxed);
  while (current < value &&
         !peak.compare_exchange_weak(current, value,
                                     std::memory_order_relaxed,
                                     std::memory_order_relaxed)) {
  }
}

bool AudioOutputM5SpeakerEq::configureNativeOutputRate(uint32_t sample_rate) {
  if (speaker_ == nullptr || sample_rate < 8000 || sample_rate > 192000) {
    return false;
  }

  if (buffer_index_ > 0) {
    flush();
    (void)waitForDrain(500);
  }

  auto config = speaker_->config();
  const bool needs_restart =
      config.sample_rate != sample_rate || !config.stereo;
  if (!needs_restart) {
    return true;
  }

  speaker_->end();
  config.sample_rate = sample_rate;
  config.stereo = true;
  speaker_->config(config);
  const bool started = speaker_->begin();

  // M5Unified's Tab5 startup callback enables the internal PA. Restore the
  // player's manual/headphone routing immediately after every rate restart.
  if (speaker_restarted_callback_ != nullptr) {
    speaker_restarted_callback_(speaker_restarted_argument_);
  }
  return started;
}
