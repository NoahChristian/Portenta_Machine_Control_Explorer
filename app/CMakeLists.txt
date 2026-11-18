# Portenta Machine Control Explorer - application.
#
# A FREESTANDING Zephyr application: it lives here and builds against whatever
# west workspace ZEPHYR_BASE points at, rather than living inside one. The
# application is the thing under version control; the workspace is a toolchain.
#
# The reasoning behind the definitions below is in docs/CONFIG.md.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pmc_explorer LANGUAGES C CXX)

# The HTTP service's resource table needs a linker section named after the
# service, which only the application knows. BOTH calls are needed: one
# supplies the section for the linker-script path, the other for the
# CMake-generated one.
zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(
  NAME http_resource_desc_pmc_service
  KVMA RAM_REGION GROUP RODATA_REGION
)

target_sources(app PRIVATE
    src/main.c
    src/shell_mcuboot.c
    src/shell_analog.c
    src/whoami.c
    src/health.c
    src/net.c
    src/clock.c
    src/expander.c
    src/io_write.c
    src/auth.c
    src/ao.c
    src/temp.c
    src/rs485.c
    src/snapshot.c
    src/status_out.c
    src/httpd.c
    src/page_vars.cpp
    src/io_post.cpp
)

# Arduino_HTTP_HTML by PATH, deliberately, and not a vendored copy: the engine
# is shared with a sibling project on this bench and the Zephyr port of it
# lives there, so a copy taken in here would drift from the one whose tests
# run. It is a hard build requirement, like ZEPHYR_BASE.
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
    ${HTMLTPL_DIR}/src/HtmlTplForm.cpp
    ${HTMLTPL_DIR}/src/HtmlTplEntropyZephyr.cpp
    ${HTMLTPL_DIR}/src/HtmlTplServerZephyr.cpp
)
target_include_directories(app PRIVATE ${HTMLTPL_DIR}/src)

# EVERY -D HERE, VISIBLE TO EVERY TARGET, and that is not tidiness: these knobs
# change the LAYOUT of shared types, so a library translation unit compiled
# with different values would link cleanly and then disagree about where fields
# are. target_compile_options() on `app` alone would not reach both.
zephyr_compile_definitions(
  # Forms, and therefore the CSRF token, are ON.
  HTMLTPL_FORMS=1

  # The page is compiled into the image and parsed in place, so there is no
  # filesystem template to disagree with the firmware.
  HTMLTPL_SOURCE_FS=0

  # One TCP MSS, and therefore the peak RAM cost of rendering a page of any
  # length.
  HTMLTPL_OUTBUF=1460

  # The library default is 64 and the panel is past it. Must be visible to
  # EVERY target: the registry and the library disagreeing about this is a
  # silent layout mismatch, which is why these live here.
  HTMLTPL_MAX_VARS=112
)

# Needed by the APPLICATION as well as by the bootloader, because the app
# writes the image trailer through img_mgmt and the symbol exists only in
# MCUboot's Kconfig. Without it the app takes mcuboot_config.h's 128 fallback
# and stages the pending magic where the bootloader does not look - a silent
# and very convincing failure. Must stay equal to mcuboot/pmc.conf's value.
zephyr_compile_definitions(CONFIG_BOOT_MAX_IMG_SECTORS=512)
