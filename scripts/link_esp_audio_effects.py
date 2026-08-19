"""Link Espressif's prebuilt ESP32-P4 Audio Effects component.

PlatformIO's pure Arduino builder does not run the ESP-IDF component manager,
so the official component archive is vendored under lib/ and linked explicitly.
"""

from os.path import isfile, join

from SCons.Script import Import

Import("env")

project_dir = env.subst("$PROJECT_DIR")
component_dir = join(project_dir, "lib", "esp_audio_effects")
include_dir = join(component_dir, "include")
library_dir = join(component_dir, "lib", "esp32p4")
archive = join(library_dir, "libesp_audio_effects.a")

if not isfile(archive):
    raise RuntimeError("Missing vendored Espressif Audio Effects P4 archive: " + archive)

env.AppendUnique(CPPPATH=[include_dir])
env.AppendUnique(LIBPATH=[library_dir])
env.AppendUnique(LIBS=["esp_audio_effects"])
