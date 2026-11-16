/*
 * The health gate, exposed so the panel and the shell report the same thing.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * There is one evaluator and three consumers: the thread that decides whether
 * to confirm an image, the `health` shell command, and /state.json. They must
 * not be able to disagree - a panel that says healthy while the gate refuses
 * to confirm would be worse than no panel.
 */

#ifndef PMC_HEALTH_H
#define PMC_HEALTH_H

#include <stdbool.h>
#include <stddef.h>

size_t      pmc_health_check_count(void);
const char *pmc_health_check_name(size_t i);

/* Runs check i. *detail points at a reason when it fails, "" otherwise. */
bool pmc_health_check(size_t i, const char **detail);

/* True once every check has passed at least once. */
bool pmc_health_is_healthy(void);

/*
 * Called by the HTTP server when it answers a real route with a 200.
 *
 * This is the "the panel actually works" signal the Giga port got from a live
 * MQTT connection. Reaching main() proves almost nothing; serving a request
 * proves the network stack, the server and the route table.
 */
void pmc_health_note_http_200(void);

#endif /* PMC_HEALTH_H */
