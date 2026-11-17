/*
 * The write token.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * FAIL CLOSED, AND BUILDABLE WITHOUT IT. If app_secrets.h does not define
 * SECRET_API_TOKEN the firmware still builds and still serves the panel - and
 * refuses every write, saying so on the page. The alternative shapes are both
 * worse: a required define breaks the build for anyone who clones this, and a
 * default token is an open board that looks guarded.
 *
 * WHAT THIS IS AND IS NOT. It is a shared secret over plain HTTP on a LAN. It
 * stops a stray request and an unauthorized script; it does not stop anyone
 * who can read the traffic. On a board whose outputs switch 24 V that limit
 * should be understood rather than papered over - the honest fix is a network
 * you trust, not a longer token.
 *
 * CSRF is a separate thing and the template engine owns it: the token here
 * authorizes, HtmlTplForm's token proves the request came from a page this
 * board rendered. A write needs both.
 */
#ifndef PMC_AUTH_H
#define PMC_AUTH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/* True when a token is compiled in. False means every write is refused. */
bool pmc_auth_configured(void);

/*
 * Constant-time compare against the configured token. Returns false when
 * none is configured, when `presented` is NULL, or when it does not match.
 *
 * Constant time because a byte-at-a-time compare leaks the token's prefix to
 * anyone willing to time the refusals, and this is cheap enough that there is
 * no reason to take that trade.
 */
bool pmc_auth_ok(const char *presented);

/* Refusals since boot, for the panel. A gate nobody can see refuse is a gate
   nobody has seen work. */
uint32_t pmc_auth_refusals(void);
void pmc_auth_note_refusal(void);


#ifdef __cplusplus
}
#endif

#endif /* PMC_AUTH_H */
