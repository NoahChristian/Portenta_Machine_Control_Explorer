/*
 * The boot indicator on digital output 1.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#ifndef PMC_STATUS_OUT_H
#define PMC_STATUS_OUT_H

#include <stdbool.h>

/*
 * busy = true  -> DO1 HIGH, meaning "running but not yet proved healthy"
 * busy = false -> DO1 LOW,  meaning "every health check passed"
 *
 * Safe to call before the GPIO is configured; it does nothing in that case
 * rather than faulting, because the one caller is the health thread and a
 * missing indicator must not take the application down with it.
 */
void pmc_status_set(bool busy);

/* True once every health check has passed. Declared in health.h. */
bool pmc_health_is_healthy(void);

#endif /* PMC_STATUS_OUT_H */
