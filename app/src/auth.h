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
 * CSRF: NOT ENFORCED, and this comment used to say it was. It read "the token
 * here authorizes, HtmlTplForm's token proves the request came from a page
 * this board rendered - a write needs both". The engine does carry a CSRF
 * token and the page does render one, but NOTHING IN THE WRITE PATH CHECKS
 * IT: io_post.cpp gates on this token alone. Proved on the running board -
 * a POST with the correct token, no CSRF field and no Referer reached the
 * verb dispatcher (2026-11-18, docs/MEASURED.md).
 *
 * A doc comment claiming a control that does not exist is worse than no
 * comment, because it is the thing somebody quotes when asked what guards
 * the board. Either wire the check in, or say this.
 *
 * SO: THE TOKEN IS THE ONLY GATE on a write, and anything that knows it can
 * write from anywhere that can reach the port.
 *
 * AND IT IS RECOVERABLE FROM THE IMAGE. It is compiled in as a plain string
 * and compared as one, so it sits in the binary in cleartext - found at one
 * offset in zephyr.signed.bin by a byte search. `strings` is the whole
 * attack. Hashing it would fix that half and not the other: the token still
 * crosses the LAN in cleartext in every POST body, because this is plain
 * HTTP. Both limits are properties of the design, not bugs in it, and the
 * honest fix is a network you trust.
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
