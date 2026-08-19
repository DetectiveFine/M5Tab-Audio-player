# M5Stack Tab5 LVGL Audio Player

<img
  src="https://i.pinimg.com/736x/92/ca/44/92ca44180a5c342297c7fa14ffd7cf51.jpg"
  width="700"
  height="250"
  style="object-fit: cover;">

A touch-controlled MP3/WAV player for the M5Stack Tab5 (ESP32-P4), built
with the Arduino framework and LVGL 9.5.

## Features

- Recursive SD card scanning up to eight directory levels and 256 tracks.
- Scrollable file browser with direct track selection.
- MP3 and PCM WAV decoding with independent left and right output channels.
- Native sample-rate playback without linear sample-rate conversion.
- Play/Pause, Stop, Previous, Next, seek, volume, and SD rescan controls.
- Elapsed time, duration, sample rate, track count, title, and artist display.
- ID3 title and artist preservation after seeking.
- Eight-band real-time parametric EQ at 60, 170, 310, 600 Hz and 1, 3, 6,
  and 12 kHz, with a -12 to +12 dB range per band.
- Stereo peak meter with peak-hold indicators.
- Animated tape-path and reel visualization whose reel speeds follow playback
  progress.
- Automatic built-in speaker muting when headphones are connected, plus a
  manual speaker enable switch.
- Dedicated FreeRTOS audio task and a 64 KiB SD read buffer, preferably placed
  in PSRAM.

## Audio Path

```text
SD_MMC / FATFS
  -> AudioFileSourceBuffer (64 KiB)
  -> ESP8266Audio MP3 or WAV decoder
  -> interleaved 16-bit stereo PCM
  -> Espressif esp_audio_effects parametric EQ
  -> M5Unified Speaker_Class buffer
  -> I2S0 + ES8388 codec
  -> headphone output and built-in amplifier/speaker
```

The ES8388 and I2S path remain stereo throughout playback. The output is
reconfigured to the source track's native sample rate before the first PCM
block is submitted. The Tab5 codec pins detected by M5Unified are MCLK 30,
BCLK 27, LRCLK 29, and DOUT 26.

Headphone detection uses the active-high HP_DET signal on P7 of the first
PI4IOE5V6408 I/O expander. P1 controls only the built-in power amplifier, so
muting the speaker does not interrupt headphone playback or restart decoding.

## Display Path

LVGL renders the 1280x720 landscape UI into a small RGB565 partial buffer.
The ESP32-P4 PPA normally rotates safe dirty rectangles into the native DSI
framebuffer. Small rectangles use software rotation because they are below
the PPA macro-block size.

PPA operations run in non-blocking mode with a finite completion timeout. If
the accelerator reports an error or fails to complete, rendering permanently
falls back to CPU rotation instead of blocking the LVGL task. CPU-written
framebuffer regions are synchronized only over their accumulated dirty range.

## Espressif Audio Effects Integration

The project vendors the official `espressif/esp_audio_effects` component API,
license, and ESP32-P4 archive at version `1.3.0~1` under
`lib/esp_audio_effects`.

This version is intentional. Espressif's 1.4+ ESP32-P4 assembly archive
requires chip revision 3.0 or newer, while some M5Stack Tab5 units use earlier
ESP32-P4 revisions. The `1.3.0~1` archive does not impose that requirement.

Because this is a pure Arduino PlatformIO project, the ESP-IDF component
manager is not available. `scripts/link_esp_audio_effects.py` adds the vendored
headers and archive to the build explicitly.

Only the ESP8266Audio translation units required by this player are compiled:
filesystem sources, buffering, ID3, MP3/libmad, WAV, and its silent logger.
`scripts/filter_esp8266audio.py` excludes unused output backends that depend on
older ESP-IDF APIs. Actual PCM output always uses the M5Unified Tab5 driver.

## Project Structure

```text
src/
  main.cpp             Board, display, touch, LVGL, PPA, and SDMMC setup
  sd_browser.*         Recursive playlist discovery and sorting
  audio_metadata.*     WAV/MP3 duration and seek metadata
  audio_player.*       Playback state machine, command queue, and audio task
  audio_output.*       Stereo PCM buffering, metering, and M5Unified output
  audio_effects.*      Wrapper for the official Espressif EQ API
  cassette_widget.*    Tape-path and reel visualization
  level_meter.*        Stereo peak and peak-hold display
  ui_player.*          Complete LVGL player and EQ interface
scripts/
  exclude_lvgl_helium.py    Excludes ARM-only LVGL assembly on RISC-V
  filter_esp8266audio.py    Keeps only required ESP8266Audio sources
  link_esp_audio_effects.py Links the vendored ESP32-P4 EQ archive
```

## Build and Upload

```sh
platformio run -e esp32p4_pioarduino
platformio run -e esp32p4_pioarduino -t upload
platformio device monitor -b 115200
```

Copy `.mp3` files or supported PCM `.wav` files into any directory on the SD
card. Scanning starts automatically at boot. Use **Rescan SD** after replacing
the card or changing its contents.

## Serial Logging

Arduino core logging is limited to errors. Routine startup, successful mount,
sample-rate, framebuffer, and headphone-state messages are disabled to avoid
timing noise during playback. Fatal initialization failures, SD errors, audio
errors, seek failures, and display fallback events remain available at
115200 baud.
