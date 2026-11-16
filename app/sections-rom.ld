/*
 * Linker section for the HTTP service's resource table.
 *
 * SPDX-License-Identifier: MIT
 *
 * HTTP_SERVICE_DEFINE() puts each resource in a section named after the
 * SERVICE, not after the subsystem - `http_resource_desc_<service>` - and then
 * takes the address of that section's start and end symbols. The server
 * library cannot declare the section, because it cannot know what an
 * application will call its services, so every application that defines one
 * declares its own here. Without it the link fails on
 * `_http_resource_desc_pmc_service_list_start` being undefined.
 *
 * The name below must match the first argument of HTTP_SERVICE_DEFINE() in
 * src/httpd.c exactly. Renaming the service without renaming this is a link
 * error rather than a silent problem, which is the good case.
 */

#include <zephyr/linker/iterable_sections.h>

ITERABLE_SECTION_ROM(http_resource_desc_pmc_service, Z_LINK_ITERABLE_SUBALIGN)
