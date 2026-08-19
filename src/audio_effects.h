#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <esp_ae_eq.h>

class AudioEffects {
 public:
  static constexpr size_t kBandCount = 8;
  static constexpr std::array<uint32_t, kBandCount> kBandFrequencies = {
      60, 170, 310, 600, 1000, 3000, 6000, 12000};

  AudioEffects();
  ~AudioEffects();

  bool configure(uint32_t sample_rate, uint8_t channels);
  bool process(int16_t *interleaved_samples, size_t frame_count);
  void reset();
  void close();

  void setEnabled(bool enabled);
  bool enabled() const { return enabled_; }
  bool available() const { return handle_ != nullptr; }

  bool setBandGain(size_t band, int8_t gain_db);
  int8_t bandGain(size_t band) const;
  void flat();

 private:
  void applyBandState(size_t band);
  bool isFlat() const;

  esp_ae_eq_handle_t handle_ = nullptr;
  std::array<esp_ae_eq_filter_para_t, kBandCount> parameters_{};
  std::array<int8_t, kBandCount> gains_{};
  std::array<bool, kBandCount> supported_{};
  bool enabled_ = true;
};
