#pragma once

#include <Arduino.h>
#include <FS.h>

#include <vector>

enum class AudioFormat : uint8_t { Mp3, Wav };

struct AudioTrack {
  String path;
  String name;
  uint32_t size = 0;
  AudioFormat format = AudioFormat::Mp3;
};

class SdBrowser {
 public:
  // The LVGL object pool is deliberately bounded. A virtualized playlist can
  // raise this later without risking an out-of-memory reset on card scan.
  static constexpr size_t kMaximumTracks = 256;
  static constexpr uint8_t kMaximumDepth = 8;

  bool scan(fs::FS &filesystem, std::vector<AudioTrack> &tracks,
            String &error_message) const;

 private:
  bool scanDirectory(fs::FS &filesystem, const String &directory_path,
                     uint8_t depth, std::vector<AudioTrack> &tracks,
                     String &error_message) const;
  static bool classify(const String &path, AudioFormat &format);
  static String baseName(const String &path);
};
