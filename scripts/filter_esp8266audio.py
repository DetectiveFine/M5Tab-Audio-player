"""Compile only the ESP8266Audio pieces used by this player.

The upstream library builds every optional output backend by default. Some of
those backends (notably PDM) target older ESP-IDF APIs and are irrelevant when
PCM is sent through M5Unified's Tab5 speaker driver.
"""

from SCons.Script import Import

Import("env")

_ROOT_MARKER = "/ESP8266Audio/src/"
_USED_TRANSLATION_UNITS = {
    "AudioFileSourceBuffer.cpp",
    "AudioFileSourceFS.cpp",
    "AudioFileSourceID3.cpp",
    "AudioGeneratorMP3.cpp",
    "AudioGeneratorWAV.cpp",
    "AudioLogger.cpp",
}


def filter_esp8266audio(node):
    source = node.srcnode().get_path().replace("\\", "/")
    if _ROOT_MARKER not in source:
        return node

    relative = source.split(_ROOT_MARKER, 1)[1]
    if relative.startswith("libmad/") or relative in _USED_TRANSLATION_UNITS:
        return node
    return None


env.AddBuildMiddleware(filter_esp8266audio)
