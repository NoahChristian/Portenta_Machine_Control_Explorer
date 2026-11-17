/*
 * The write route's C entry point.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * httpd.c is C and the template engine is C++, so the route table stays in C
 * and the work happens behind this door - the same arrangement pmc_page_step()
 * already uses for the GET route. The alternative, making httpd.c a C++ file,
 * would drag the whole route table across a language boundary to reach one
 * header.
 */
#ifndef PMC_IO_POST_H
#define PMC_IO_POST_H

#include <zephyr/net/http/server.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One step of a POST to /io. Call for every status, exactly as the handler
 * receives them; it gathers the body across callbacks and answers once.
 * Always returns 0 - a refusal is a reply, not a dropped socket.
 */
int pmc_io_post_step(enum http_transaction_status status,
		     const struct http_request_ctx *req,
		     struct http_response_ctx *rsp);

#ifdef __cplusplus
}
#endif

#endif /* PMC_IO_POST_H */
