#include "audio_metadata.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

uint16_t readLe16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(data[1]) << 8;
}

uint32_t readLe32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) |
         static_cast<uint32_t>(data[1]) << 8 |
         static_cast<uint32_t>(data[2]) << 16 |
         static_cast<uint32_t>(data[3]) << 24;
}

uint32_t readBe32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) << 24 |
         static_cast<uint32_t>(data[1]) << 16 |
         static_cast<uint32_t>(data[2]) << 8 |
         static_cast<uint32_t>(data[3]);
}

bool readExact(File &file, void *destination, size_t length) {
  return file.read(static_cast<uint8_t *>(destination), length) == length;
}

bool readWavMetadata(File &file, AudioMetadata &metadata) {
  uint8_t header[12];
  if (!readExact(file, header, sizeof(header)) ||
      std::memcmp(header, "RIFF", 4) != 0 ||
      std::memcmp(header + 8, "WAVE", 4) != 0) {
    return false;
  }

  uint32_t byte_rate = 0;
  bool format_found = false;
  bool data_found = false;
  while (file.available()) {
    uint8_t chunk_header[8];
    if (!readExact(file, chunk_header, sizeof(chunk_header))) {
      break;
    }
    const uint32_t chunk_size = readLe32(chunk_header + 4);
    const uint32_t chunk_data = static_cast<uint32_t>(file.position());

    if (std::memcmp(chunk_header, "fmt ", 4) == 0 && chunk_size >= 16) {
      uint8_t format[16];
      if (!readExact(file, format, sizeof(format)) || readLe16(format) != 1) {
        return false;
      }
      metadata.sample_rate = readLe32(format + 4);
      byte_rate = readLe32(format + 8);
      metadata.block_align = readLe16(format + 12);
      format_found = metadata.sample_rate > 0 && byte_rate > 0 &&
                     metadata.block_align > 0;
    } else if (std::memcmp(chunk_header, "data", 4) == 0) {
      metadata.audio_start = chunk_data;
      metadata.audio_bytes = std::min<uint32_t>(
          chunk_size, static_cast<uint32_t>(file.size()) - chunk_data);
      data_found = true;
    }

    const uint32_t next = chunk_data + chunk_size + (chunk_size & 1U);
    if (!file.seek(next)) {
      break;
    }
    if (format_found && data_found) {
      break;
    }
  }

  if (!format_found || !data_found) {
    return false;
  }
  metadata.duration_ms = static_cast<uint32_t>(
      static_cast<uint64_t>(metadata.audio_bytes) * 1000ULL / byte_rate);
  return metadata.duration_ms > 0;
}

struct Mp3FrameInfo {
  uint32_t sample_rate = 0;
  uint16_t bitrate_kbps = 0;
  uint16_t frame_bytes = 0;
  uint16_t samples_per_frame = 0;
  uint8_t version = 0;
  bool mono = false;
  bool crc = false;
};

bool parseMp3Header(const uint8_t *bytes, Mp3FrameInfo &info) {
  const uint32_t header = readBe32(bytes);
  if ((header & 0xFFE00000U) != 0xFFE00000U) {
    return false;
  }
  const uint8_t version_id = (header >> 19) & 0x03;
  const uint8_t layer = (header >> 17) & 0x03;
  const uint8_t bitrate_index = (header >> 12) & 0x0F;
  const uint8_t sample_index = (header >> 10) & 0x03;
  if (version_id == 1 || layer != 1 || bitrate_index == 0 ||
      bitrate_index == 15 || sample_index == 3) {
    return false;
  }

  static constexpr uint16_t kMpeg1Bitrates[] = {
      0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
  static constexpr uint16_t kMpeg2Bitrates[] = {
      0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
  static constexpr uint32_t kSampleRates[] = {44100, 48000, 32000};

  info.version = version_id;
  info.bitrate_kbps = version_id == 3 ? kMpeg1Bitrates[bitrate_index]
                                      : kMpeg2Bitrates[bitrate_index];
  info.sample_rate = kSampleRates[sample_index];
  if (version_id == 2) {
    info.sample_rate /= 2;
  } else if (version_id == 0) {
    info.sample_rate /= 4;
  }
  info.samples_per_frame = version_id == 3 ? 1152 : 576;
  const uint32_t coefficient = version_id == 3 ? 144000 : 72000;
  info.frame_bytes = static_cast<uint16_t>(
      coefficient * info.bitrate_kbps / info.sample_rate +
      ((header >> 9) & 1U));
  info.mono = ((header >> 6) & 0x03) == 3;
  info.crc = ((header >> 16) & 1U) == 0;
  return info.frame_bytes >= 24;
}

bool findFirstMp3Frame(File &file, uint32_t start, uint32_t &frame_offset,
                       Mp3FrameInfo &frame_info) {
  constexpr size_t kChunkSize = 4096;
  constexpr uint32_t kMaximumScan = 64 * 1024;
  std::array<uint8_t, kChunkSize + 3> buffer{};
  uint32_t position = start;
  size_t carry = 0;
  const uint32_t end = std::min<uint32_t>(static_cast<uint32_t>(file.size()),
                                          start + kMaximumScan);

  while (position < end) {
    if (!file.seek(position)) {
      return false;
    }
    const size_t wanted = std::min<size_t>(kChunkSize, end - position);
    const size_t count = file.read(buffer.data() + carry, wanted);
    const size_t available = carry + count;
    for (size_t i = 0; i + 4 <= available; ++i) {
      Mp3FrameInfo candidate;
      if (parseMp3Header(buffer.data() + i, candidate)) {
        frame_offset = position - carry + i;
        frame_info = candidate;
        return true;
      }
    }
    if (count == 0) {
      break;
    }
    carry = std::min<size_t>(3, available);
    std::memmove(buffer.data(), buffer.data() + available - carry, carry);
    position += count;
  }
  return false;
}

bool readMp3Metadata(File &file, AudioMetadata &metadata) {
  uint32_t scan_start = 0;
  uint8_t id3[10];
  if (readExact(file, id3, sizeof(id3)) &&
      std::memcmp(id3, "ID3", 3) == 0) {
    const uint32_t tag_size = static_cast<uint32_t>(id3[6] & 0x7F) << 21 |
                              static_cast<uint32_t>(id3[7] & 0x7F) << 14 |
                              static_cast<uint32_t>(id3[8] & 0x7F) << 7 |
                              static_cast<uint32_t>(id3[9] & 0x7F);
    scan_start = 10 + tag_size + ((id3[5] & 0x10) ? 10 : 0);
  }

  uint32_t frame_offset = 0;
  Mp3FrameInfo frame;
  if (!findFirstMp3Frame(file, scan_start, frame_offset, frame)) {
    return false;
  }

  metadata.audio_start = frame_offset;
  metadata.audio_bytes = static_cast<uint32_t>(file.size()) - frame_offset;
  metadata.sample_rate = frame.sample_rate;
  metadata.block_align = 1;

  if (file.size() >= 128 && file.seek(file.size() - 128)) {
    uint8_t tag[3];
    if (readExact(file, tag, sizeof(tag)) && std::memcmp(tag, "TAG", 3) == 0 &&
        metadata.audio_bytes >= 128) {
      metadata.audio_bytes -= 128;
    }
  }

  std::array<uint8_t, 2048> first_frame{};
  const size_t frame_bytes =
      std::min<size_t>(frame.frame_bytes, first_frame.size());
  bool variable_duration_found = false;
  if (file.seek(frame_offset) &&
      file.read(first_frame.data(), frame_bytes) == frame_bytes) {
    const size_t side_info = frame.version == 3
                                 ? (frame.mono ? 17 : 32)
                                 : (frame.mono ? 9 : 17);
    const size_t xing = 4 + (frame.crc ? 2 : 0) + side_info;
    if (xing + 12 <= frame_bytes &&
        (std::memcmp(first_frame.data() + xing, "Xing", 4) == 0 ||
         std::memcmp(first_frame.data() + xing, "Info", 4) == 0)) {
      const uint32_t flags = readBe32(first_frame.data() + xing + 4);
      if ((flags & 1U) != 0) {
        const uint32_t frames = readBe32(first_frame.data() + xing + 8);
        metadata.duration_ms = static_cast<uint32_t>(
            static_cast<uint64_t>(frames) * frame.samples_per_frame * 1000ULL /
            frame.sample_rate);
        variable_duration_found = metadata.duration_ms > 0;
      }
    }

    constexpr size_t kVbri = 36;
    if (!variable_duration_found && kVbri + 18 <= frame_bytes &&
        std::memcmp(first_frame.data() + kVbri, "VBRI", 4) == 0) {
      const uint32_t frames = readBe32(first_frame.data() + kVbri + 14);
      metadata.duration_ms = static_cast<uint32_t>(
          static_cast<uint64_t>(frames) * frame.samples_per_frame * 1000ULL /
          frame.sample_rate);
      variable_duration_found = metadata.duration_ms > 0;
    }
  }

  if (!variable_duration_found) {
    metadata.duration_ms = static_cast<uint32_t>(
        static_cast<uint64_t>(metadata.audio_bytes) * 8ULL /
        frame.bitrate_kbps);
  }
  return metadata.duration_ms > 0;
}

}  // namespace

bool readAudioMetadata(fs::FS &filesystem, const AudioTrack &track,
                       AudioMetadata &metadata) {
  metadata = {};
  File file = filesystem.open(track.path, FILE_READ);
  if (!file) {
    return false;
  }
  const bool result = track.format == AudioFormat::Wav
                          ? readWavMetadata(file, metadata)
                          : readMp3Metadata(file, metadata);
  file.close();
  return result;
}
