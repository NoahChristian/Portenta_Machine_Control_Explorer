# Portenta Machine Control Explorer - application
#
# A FREESTANDING Zephyr application: it lives in this repository and builds
# against whatever west workspace ZEPHYR_BASE points at, rather than living
# inside the workspace. Same arrangement as the Giga dashboard port, and for
# the same reason: the application is the thing under version control here, and
# the workspace is a toolchain.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pmc_explorer LANGUAGES C CXX)

target_sources(app PRIVATE
    src/main.c
    src/shell_mcuboot.c
    src/shell_analog.c
)

# MCUboot's maximum image sector count, needed by the APPLICATION as well as by
# the bootloader.
#
# The application writes the image trailer (through img_mgmt), so it has to
# agree with the bootloader about where that trailer begins. CONFIG_BOOT_MAX_IMG_SECTORS
# exists only in MCUboot's own Kconfig and never in the application's, so
# without this definition the app silently takes the
#
#     #define MCUBOOT_MAX_IMG_SECTORS 128
#
# fallback in mcuboot_config.h and writes the pending magic a long way from
# where the bootloader reads it. The failure is SILENT and very convincing: the
# upload succeeds, the state reads back as pending, the board resets, and
# MCUboot boots the old image as though nothing had been staged.
#
# 512 covers the SECONDARY slot's 320 QSPI sectors of 4 KB, not the primary's
# 10 sectors of 128 KB. Deriving it from the first image's partition - which is
# what CONFIG_BOOT_MAX_IMG_SECTORS_AUTO does - takes the wrong end of an
# asymmetric layout like this one.
#
# Must stay equal to the value in mcuboot/pmc.conf.
zephyr_compile_definitions(CONFIG_BOOT_MAX_IMG_SECTORS=512)
