# Espressif Audio Effects (vendored component)

This directory contains the API headers, license, and ESP32-P4 prebuilt archive
from the official `espressif/esp_audio_effects` component, version `1.3.0~1`.

The component is vendored because this is a pure Arduino PlatformIO project;
PlatformIO's Arduino builder does not resolve `idf_component.yml` dependencies.
The archive is linked by `scripts/link_esp_audio_effects.py`.

Version 1.3.0~1 is intentionally used instead of 1.4+: Espressif's 1.4+
ESP32-P4 archive requires chip revision 3.0 or newer, while M5Stack Tab5 units
also exist with earlier ESP32-P4 revisions.

Upstream: https://components.espressif.com/components/espressif/esp_audio_effects/versions/1.3.0~1
