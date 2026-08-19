#pragma once

#include <Arduino.h>
#include <FS.h>

#include "sd_browser.h"

struct AudioMetadata {
  uint32_t duration_ms = 0;
  uint32_t audio_start = 0;
  uint32_t audio_bytes = 0;
  uint32_t sample_rate = 0;
  uint16_t block_align = 1;
};

bool readAudioMetadata(fs::FS &filesystem, const AudioTrack &track,
                       AudioMetadata &metadata);
