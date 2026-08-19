"""Exclude LVGL's ARM-only Helium assembly from RISC-V PlatformIO builds.

LVGL 9.5 ships an uppercase .S file whose body is disabled by preprocessing
on ESP32-P4. GCC still emits an empty ARM/soft-float object for it, and the
RISC-V linker rejects that object's ABI. Arduino-ESP32 uses prebuilt framework
archives, while this project and its required libraries contain no RISC-V .S
sources, so excluding uppercase assembly is scoped and safe here.
"""

from SCons.Script import Import

Import("env")


def exclude_arm_helium(node):
    source = node.srcnode().get_path().replace("\\", "/")
    if source.endswith("/lvgl/src/draw/sw/blend/helium/lv_blend_helium.S"):
        return None
    return node


env.AddBuildMiddleware(exclude_arm_helium)
