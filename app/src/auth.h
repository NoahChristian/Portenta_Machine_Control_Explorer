/*
 * The write token.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * PROVISIONED, NOT COMPILED IN (changed 2026-11-19). The token lives in the
 * `kvs` NVS partition, put there once per board over the serial shell by
 * tools/token_provision.ps1. It is not in the firmware, so the image carries
 * no secret at all and can be published, attached to a release, or handed to
 * somebody else unchanged.
 *
 * It used to be `#define SECRET_API_TOKEN` pulled in from app_secrets.h and
 * compared as a string literal, which put it in the binary as plain ASCII:
 * a byte search found it at one offset in zephyr.signed.bin and `strings` was
 * the whole attack. That was written down in this file and accepted, which was
 * tolerable while the artifact never left the bench and stopped being so the
 * moment a public release could carry it.
 *
 * This also makes the token a property of the BOARD rather than of the build.
 * Two boards running the identical image now hold different tokens, one can be
 * rotated without a rebuild, and an OTA can no longer move a credential from
 * one unit to another by accident.
 *
 * NVS HOLDS IT IN THE CLEAR, and that is a decision rather than an oversight.
 * Hashing would defend against someone who can read the QSPI chip - but that
 * someone is holding the board, and the terminals are right there; the token
 * guards the network path, not the screwdriver. It would also have meant a
 * crypto dependency in an image that currently has none, to defend the one
 * copy that never leaves the unit. The copy that travels is the one that
 * mattered, and it is gone.
 *
 * FAIL CLOSED, AND BUILDABLE WITHOUT IT. A board with nothing provisioned
 * serves the panel read-only and refuses every write, saying so on the page.
 * That is also the state of a freshly flashed board, deliberately: an
 * un-provisioned unit should not be writable, and "the token did not survive
 * the update" must look different from "there is no token".
 *
 * The NVS partition sits outside both image slots, so provisioning survives
 * every OTA - the same property that makes the WiFi credentials a one-time
 * step in tools/wifi_provision.ps1.
 *
 * WHAT THIS IS AND IS NOT. It is a shared secret over plain HTTP on a LAN. It
 * stops a stray request and an unauthorized script; it does not stop anyone
 * who can read the traffic. On a board whose outputs switch 24 V that limit
 * should be understood rather than papered over - the honest fix is a network
 * you trust, not a longer token. Moving the token out of the image did not
 * change this, and nothing about provisioning should be read as having done.
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
 */
#ifndef PMC_AUTH_H
#define PMC_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A floor, not a style guide. Below this the token is guessable over a LAN
 * faster than anyone would notice the refusals, and there is no reason to
 * accept one: the provisioning script generates 32 characters. It was a
 * BUILD_ASSERT when the token was compiled in; the check has to happen at
 * provisioning time now, which is also where a person can be told about it.
 */
#define PMC_AUTH_TOKEN_MIN 16
#define PMC_AUTH_TOKEN_MAX 64

/* True when a token is provisioned. False means every write is refused. */
bool pmc_auth_configured(void);

/*
 * Constant-time compare against the provisioned token. Returns false when
 * none is provisioned, when `presented` is NULL, or when it does not match.
 *
 * Constant time because a byte-at-a-time compare leaks the token's prefix to
 * anyone willing to time the refusals, and this is cheap enough that there is
 * no reason to take that trade.
 */
bool pmc_auth_ok(const char *presented);

/*
 * Store a token in NVS, replacing any existing one. Returns 0, or -EINVAL
 * when the length is outside [PMC_AUTH_TOKEN_MIN, PMC_AUTH_TOKEN_MAX], or the
 * settings backend's error.
 *
 * The RAM copy is only updated once the write to flash has succeeded, so a
 * failed save leaves the board answering to the token it already had rather
 * than to one that exists nowhere.
 */
int pmc_auth_set(const char *tok);

/* Forget the provisioned token. The board is read-only afterwards. */
int pmc_auth_clear(void);

/* Length of the provisioned token, 0 when there is none. For reporting the
   state of provisioning without reporting the token. */
size_t pmc_auth_len(void);

/* Refusals since boot, for the panel. A gate nobody can see refuse is a gate
   nobody has seen work. */
uint32_t pmc_auth_refusals(void);
void pmc_auth_note_refusal(void);

#ifdef __cplusplus
}
#endif

#endif /* PMC_AUTH_H */
