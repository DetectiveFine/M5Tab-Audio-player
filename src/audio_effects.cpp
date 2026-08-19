#include "audio_effects.h"

#include <algorithm>

AudioEffects::AudioEffects() {
  gains_.fill(0);
  supported_.fill(false);
}

AudioEffects::~AudioEffects() { close(); }

bool AudioEffects::configure(uint32_t sample_rate, uint8_t channels) {
  close();
  if (sample_rate < 1000 || channels == 0) {
    return false;
  }

  uint8_t supported_count = 0;
  for (size_t i = 0; i < kBandCount; ++i) {
    supported_[i] = kBandFrequencies[i] < sample_rate / 2;
    if (supported_[i]) {
      ++supported_count;
    }
    parameters_[i].filter_type = ESP_AE_EQ_FILTER_PEAK;
    parameters_[i].fc = kBandFrequencies[i];
    parameters_[i].q = 1.1f;
    parameters_[i].gain = static_cast<float>(gains_[i]);
  }
  if (supported_count == 0) {
    return false;
  }

  esp_ae_eq_cfg_t config = {};
  config.sample_rate = sample_rate;
  config.channel = channels;
  config.bits_per_sample = ESP_AE_BIT16;
  // Frequencies are sorted, so the supported bands are a contiguous prefix.
  // Unsupported bands above Nyquist are not passed to the official API.
  config.filter_num = supported_count;
  config.para = parameters_.data();

  if (esp_ae_eq_open(&config, &handle_) != ESP_AE_ERR_OK ||
      handle_ == nullptr) {
    handle_ = nullptr;
    return false;
  }

  for (size_t i = 0; i < kBandCount; ++i) {
    applyBandState(i);
  }
  return true;
}

bool AudioEffects::process(int16_t *interleaved_samples,
                           size_t frame_count) {
  // A flat EQ must be a true PCM bypass. Besides saving CPU, this avoids even
  // the tiny rounding error of passing samples through zero-gain biquads.
  if (!enabled_ || isFlat() || handle_ == nullptr ||
      interleaved_samples == nullptr || frame_count == 0) {
    return true;
  }
  return esp_ae_eq_process(handle_, static_cast<uint32_t>(frame_count),
                           interleaved_samples, interleaved_samples) ==
         ESP_AE_ERR_OK;
}

void AudioEffects::reset() {
  if (handle_ != nullptr) {
    (void)esp_ae_eq_reset(handle_);
  }
}

void AudioEffects::close() {
  if (handle_ != nullptr) {
    esp_ae_eq_close(handle_);
    handle_ = nullptr;
  }
  supported_.fill(false);
}

void AudioEffects::setEnabled(bool enabled) {
  enabled_ = enabled;
  for (size_t i = 0; i < kBandCount; ++i) {
    applyBandState(i);
  }
}

bool AudioEffects::setBandGain(size_t band, int8_t gain_db) {
  if (band >= kBandCount) {
    return false;
  }
  gain_db = std::max<int8_t>(-12, std::min<int8_t>(12, gain_db));
  gains_[band] = gain_db;
  parameters_[band].gain = static_cast<float>(gain_db);
  if (handle_ == nullptr || !supported_[band]) {
    return true;
  }
  return esp_ae_eq_set_filter_para(handle_, static_cast<uint8_t>(band),
                                   &parameters_[band]) == ESP_AE_ERR_OK;
}

int8_t AudioEffects::bandGain(size_t band) const {
  return band < kBandCount ? gains_[band] : 0;
}

void AudioEffects::flat() {
  for (size_t i = 0; i < kBandCount; ++i) {
    (void)setBandGain(i, 0);
  }
  reset();
}

void AudioEffects::applyBandState(size_t band) {
  if (handle_ == nullptr || band >= kBandCount || !supported_[band]) {
    return;
  }
  if (enabled_) {
    (void)esp_ae_eq_enable_filter(handle_, static_cast<uint8_t>(band));
  } else {
    (void)esp_ae_eq_disable_filter(handle_, static_cast<uint8_t>(band));
  }
}

bool AudioEffects::isFlat() const {
  return std::all_of(gains_.begin(), gains_.end(),
                     [](int8_t gain) { return gain == 0; });
}
