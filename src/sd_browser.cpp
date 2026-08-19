#include "sd_browser.h"

#include <algorithm>

bool SdBrowser::scan(fs::FS &filesystem, std::vector<AudioTrack> &tracks,
                     String &error_message) const {
  tracks.clear();
  error_message = "";
  if (!scanDirectory(filesystem, "/", 0, tracks, error_message)) {
    return false;
  }

  std::sort(tracks.begin(), tracks.end(),
            [](const AudioTrack &left, const AudioTrack &right) {
              String left_key = left.path;
              String right_key = right.path;
              left_key.toLowerCase();
              right_key.toLowerCase();
              return left_key < right_key;
            });
  return true;
}

bool SdBrowser::scanDirectory(fs::FS &filesystem,
                              const String &directory_path, uint8_t depth,
                              std::vector<AudioTrack> &tracks,
                              String &error_message) const {
  if (depth > kMaximumDepth) {
    return true;
  }

  File directory = filesystem.open(directory_path);
  if (!directory || !directory.isDirectory()) {
    error_message = "Cannot open directory: " + directory_path;
    return false;
  }

  File entry;
  while ((entry = directory.openNextFile())) {
    String path = entry.path();
    if (path.isEmpty()) {
      path = entry.name();
    }
    if (!path.startsWith("/")) {
      path = directory_path;
      if (!path.endsWith("/")) {
        path += '/';
      }
      path += entry.name();
    }

    if (entry.isDirectory()) {
      entry.close();
      if (!scanDirectory(filesystem, path, depth + 1, tracks,
                         error_message)) {
        directory.close();
        return false;
      }
    } else {
      AudioFormat format;
      if (classify(path, format)) {
        AudioTrack track;
        track.path = path;
        track.name = baseName(path);
        track.size = static_cast<uint32_t>(entry.size());
        track.format = format;
        tracks.push_back(std::move(track));
      }
      entry.close();
      if (tracks.size() >= kMaximumTracks) {
        error_message = "Playlist limit reached (256 files).";
        directory.close();
        return true;
      }
    }
  }
  directory.close();
  return true;
}

bool SdBrowser::classify(const String &path, AudioFormat &format) {
  String lower = path;
  lower.toLowerCase();
  if (lower.endsWith(".mp3")) {
    format = AudioFormat::Mp3;
    return true;
  }
  if (lower.endsWith(".wav")) {
    format = AudioFormat::Wav;
    return true;
  }
  return false;
}

String SdBrowser::baseName(const String &path) {
  const int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}
