# Copyright (c) 2026 Noah Christian
# SPDX-License-Identifier: Apache-2.0
#
# A shield is usually devicetree and Kconfig only. This one carries source
# because the carrier needs one register written before the kernel starts, and
# that write belongs to the CARRIER rather than to any application that happens
# to use it. See pmc_syscfg.c.

# NAMED EXPLICITLY, and on Windows that is not cosmetic.
#
# A bare zephyr_library() derives its name from the path between ZEPHYR_BASE
# and this directory. For an out-of-tree shield that path is long and full of
# separators, so the derived name came out as
#
#   lib..__..__Documents__Arduino__Portenta_Machine_Control_Explorer__boards__
#   shields__arduino_portenta_machine_control.a
#
# which put the object directory at 261 characters against CMake's
# CMAKE_OBJECT_PATH_MAX of 250. The result was not a clean error: CMake emitted
# a WARNING saying the object "cannot be safely placed under this directory",
# configured and generated successfully anyway, and then ar failed with
#
#   error reading .../pmc_syscfg.c.obj: No such file or directory
#
# i.e. the compile silently produced nothing and the failure surfaced one step
# later, pointing at the archiver. Naming the library fixes it and keeps the
# path short no matter where the repository is checked out.
zephyr_library_named(pmc_shield)
zephyr_library_sources(pmc_syscfg.c)
