/*
 * The write token. Why it fails closed is in auth.h.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "auth.h"

/*
 * app_secrets.h sits in app/, this file in app/src/, so the quoted form
 * does not find it - and __has_include then reports absent rather than
 * erroring, which looks exactly like "no token configured". The gate
 * refused every write and said so, which is the right failure, but for
 * the wrong reason.
 */
#if __has_include("../app_secrets.h")
#include "../app_secrets.h"
#endif

LOG_MODULE_REGISTER(pmc_auth, LOG_LEVEL_INF);

#if defined(SECRET_API_TOKEN)
BUILD_ASSERT(sizeof(SECRET_API_TOKEN) > 16,
	     "SECRET_API_TOKEN is too short to be worth having - "
	     "leave it undefined instead and writes stay refused");
#endif

static atomic_t s_refusals;

bool pmc_auth_configured(void)
{
#if defined(SECRET_API_TOKEN)
	return sizeof(SECRET_API_TOKEN) > 1U;
#else
	return false;
#endif
}

bool pmc_auth_ok(const char *presented)
{
#if defined(SECRET_API_TOKEN)
	static const char tok[] = SECRET_API_TOKEN;
	const size_t n = sizeof(tok) - 1U;
	size_t plen;
	uint8_t diff = 0;

	if (n == 0U || presented == NULL) {
		return false;
	}

	plen = strlen(presented);

	/*
	 * Fold the length into the difference rather than returning early on
	 * it. An early return on length is itself a timing signal, and it is
	 * free to avoid here.
	 */
	diff |= (uint8_t)((plen ^ n) != 0U);

	for (size_t i = 0; i < n; i++) {
		const uint8_t got = (i < plen) ? (uint8_t)presented[i] : 0U;

		diff |= (uint8_t)(got ^ (uint8_t)tok[i]);
	}

	return diff == 0U;
#else
	ARG_UNUSED(presented);
	return false;
#endif
}

uint32_t pmc_auth_refusals(void)
{
	return (uint32_t)atomic_get(&s_refusals);
}

void pmc_auth_note_refusal(void)
{
	const atomic_val_t n = atomic_inc(&s_refusals) + 1;

	LOG_WRN("write refused (%u since boot)", (unsigned)n);
}
