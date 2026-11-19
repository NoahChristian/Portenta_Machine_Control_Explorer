/*
 * Interlock (0): every digital output starts de-energized.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * This header used to declare pmc_status_set(), which drove DO1 as a boot
 * indicator. That is gone - see do_interlock.c for what it was, why it was
 * worth having, and what was given up by removing it.
 *
 * Nothing is exported now. The interlock runs from SYS_INIT at POST_KERNEL 45
 * and the `dowalk` shell command registers itself, so there is no entry point
 * for anything else to call. The header remains so that the file has a place
 * to state what it guarantees:
 *
 *   By the time any application code runs, all eight digital outputs have
 *   been configured as outputs, driven low, and the shared latch pulsed.
 *
 * Remembered outputs (persist.h) are restored AFTER that, from the health
 * thread, and only once every check has passed.
 */
#ifndef PMC_DO_INTERLOCK_H
#define PMC_DO_INTERLOCK_H

/* True once every health check has passed. Declared in health.h. */
bool pmc_health_is_healthy(void);

#endif /* PMC_DO_INTERLOCK_H */
