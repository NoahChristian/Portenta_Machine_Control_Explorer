/*
 * The C face of the page renderer. Implementation is page_vars.cpp.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * httpd.c owns the routes and page_vars.cpp owns the rendering, and the seam
 * between them is three functions. The split follows the language: the
 * template registry needs C++ for its decltype-deduced rows, and everything
 * that touches the hardware is C, so exactly one translation unit is C++ and
 * it is reached only through these.
 */

#ifndef PMC_PAGE_VARS_H
#define PMC_PAGE_VARS_H

#include <stddef.h>

#include <zephyr/net/http/server.h>

/*
 * Call once at startup. Initializes the render bridge and runs the page's
 * self-test against the registry, which also calls every row provider once.
 */
void pmc_page_init(void);

/*
 * ONE STEP of the page response. Call it from the dynamic resource handler for
 * every transaction status; it decides what each one means.
 *
 * `query` is request_ctx->data for a GET - Zephyr hands over whatever follows
 * the '?' in the URL, and ONLY on the first callback of a request. Pass it
 * through every time regardless; this ignores it on the later ones, which is
 * safer than the caller having to know which call is the first.
 */
int pmc_page_step(enum http_transaction_status status,
		  struct http_response_ctx *rsp, const char *query,
		  size_t qlen);

/*
 * Serializes the same snapshot the page renders from, as JSON, for scripts.
 * Returns the byte count written.
 */
size_t pmc_page_state_json(char *out, size_t cap);

#endif /* PMC_PAGE_VARS_H */
