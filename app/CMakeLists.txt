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

# The HTTP service's resource table needs a linker section named after the
# SERVICE, which only the application knows the name of - see sections-rom.ld.
# BOTH calls are needed: the first supplies the section for the linker-script
# path, the second for the CMake-generated one.
zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(
  NAME http_resource_desc_pmc_service
  KVMA RAM_REGION GROUP RODATA_REGION
)

target_sources(app PRIVATE
    src/main.c
    src/shell_mcuboot.c
    src/shell_analog.c
    src/health.c
    src/net.c
    src/clock.c
    src/expander.c
    src/snapshot.c
    src/status_out.c
    src/httpd.c
    src/page_vars.cpp
)

# ---------------------------------------------------------------------------
# Arduino_HTTP_HTML: the HtmlTpl SSI engine, compiled from its own repository.
#
# A DEPENDENCY BY PATH, DELIBERATELY, AND NOT A VENDORED COPY. The library is
# a sibling sketchbook project with its own golden test suite, its own
# versioning and its own history, and the Zephyr port of it lives THERE - which
# is the whole point: the engine is shared with a sibling project on this
# bench, and a copy taken into this repository would be a second engine that
# drifts from the one whose tests run. It is a hard requirement for a build,
# the same way ZEPHYR_BASE is; see README.md.
set(HTMLTPL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../Arduino_HTTP_HTML"
    CACHE PATH "Path to the Arduino_HTTP_HTML checkout")
if(NOT EXISTS "${HTMLTPL_DIR}/src/HtmlTplEngine.cpp")
  message(FATAL_ERROR
    "Arduino_HTTP_HTML not found at ${HTMLTPL_DIR}.\n"
    "The panel is a TEMPLATE rendered on the board, not markup assembled in C, "
    "so the engine is not optional. Clone it beside this repository or pass "
    "-DHTMLTPL_DIR=<path>.")
endif()

target_sources(app PRIVATE
    ${HTMLTPL_DIR}/src/HtmlTplEngine.cpp
    ${HTMLTPL_DIR}/src/HtmlTplVars.cpp
    ${HTMLTPL_DIR}/src/HtmlTplCsrf.cpp
    ${HTMLTPL_DIR}/src/HtmlTplServerZephyr.cpp
)
target_include_directories(app PRIVATE ${HTMLTPL_DIR}/src)

# EVERY -D HERE, VISIBLE TO EVERY TARGET, and that is not tidiness.
# HTMLTPL_* knobs change the LAYOUT of shared types - HtmlTplCursor carries a
# window sized by HTMLTPL_WINDOW, and the registry's form slots are sized by
# HTMLTPL_MAX_VARS. A library translation unit compiled with different values
# from the registry's would link cleanly and then disagree about where fields
# are. zephyr_compile_definitions() reaches both, which target_compile_options
# on `app` alone would not.
zephyr_compile_definitions(
  # Forms, and therefore the CSRF token, are OFF because this panel is
  # READ-ONLY today. Turning them on would compile a form binder nothing feeds
  # and render a token nothing checks, which reads as protection and is not.
  # M4 flips this to 1 and adds HtmlTplEntropyZephyr.cpp, which is the file
  # that seeds the token from sys_csrand_get(). With forms off, a registry may
  # hold 255 rows instead of HTMLTPL_MAX_VARS.
  HTMLTPL_FORMS=0

  # No filesystem template source: the page is compiled into the image and
  # parsed in place through a const char*. This drops the 256-byte read window
  # from every HtmlTplCursor, and more importantly drops the whole class of
  # "the template on the card disagrees with the firmware" problem that the
  # file source exists to manage.
  HTMLTPL_SOURCE_FS=0

  # One TCP MSS. The render thread publishes a chunk this size, so it is also
  # the peak RAM cost of rendering a page of any length - see
  # HtmlTplServerZephyr.h.
  HTMLTPL_OUTBUF=1460
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
