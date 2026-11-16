/*
 * The web panel's server.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#ifndef PMC_HTTPD_H
#define PMC_HTTPD_H

/*
 * Start listening. Call from main(), after the network has been brought up -
 * not from a SYS_INIT, which runs too early. Does not wait for an address:
 * the socket is bound to the wildcard.
 *
 * Failure is logged and not fatal. Losing the panel is bad; refusing to run
 * the I/O because the panel could not start would be worse.
 */
void pmc_httpd_init(void);

#endif /* PMC_HTTPD_H */
