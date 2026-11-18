/*
 * The web panel's server: the route table, and nothing else.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * TWO ROUTES, AND THE PAGE IS RENDERED, NOT ASSEMBLED.
 *
 *   /            the panel, rendered server-side from web/index.html by the
 *                HtmlTpl SSI engine through page_vars.cpp
 *   /state.json  the same snapshot as JSON, for scripts
 *
 * THIS USED TO SERVE A C BYTE ARRAY AND LET JAVASCRIPT FILL IT IN. The page
 * was authored as HTML, which was right, and then embedded verbatim as
 * `static const uint8_t page_index[]` and animated by a script polling
 * /state.json every two seconds. Three things were wrong with that:
 *
 *   - It was still markup living in C. The rule this repository carries from
 *     a sibling project on this bench is that a page is a TEMPLATE the device
 *     renders, with every substitution checked at build time against a
 *     registry. A byte array is not checked against anything: a typo in a
 *     JavaScript field name produced an empty cell and no error anywhere.
 *   - Every reading on screen came from a different instant, and nothing said
 *     so. The panel now takes one snapshot per render, and says in its own
 *     heading that it does.
 *   - A page that polls against a server with a small fixed number of client
 *     slots wedges itself. That cost a debugging session and a
 *     `Connection: close` header; with nothing polling, neither is needed.
 *
 * WHY A TABLE AND NOT SCATTERED REGISTRATIONS. A resource declares
 * bitmask_of_supported_http_methods in the same place as its path, which is
 * the structural answer to a bug a sibling project on this bench shipped
 * twice: server.on() lets a mutating route be registered with no method
 * restriction and no auth, and the registration written individually is the
 * one forgotten individually. Both routes below are GET only and say so
 * where they live.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/sys/reboot.h>

#include "health.h"
#include "httpd.h"
#include "rs485.h"
#include "io_post.h"
#include "page_vars.h"

LOG_MODULE_REGISTER(pmc_httpd, LOG_LEVEL_INF);

static uint16_t http_port = 80;

/*
 * HOST IS NULL, NOT "0.0.0.0".
 *
 * The documentation is explicit: "If left NULL, the listening port will listen
 * on all addresses." Passing the dotted-quad wildcard instead looks equivalent
 * and is not - the service came up, http_server_start() returned 0, and port
 * 80 refused every connection while the board answered SMP on the same
 * address. No error anywhere; the only symptom was a refused connection, which
 * reads like a firewall or a wrong address rather than a host string.
 */
HTTP_SERVICE_DEFINE(pmc_service, NULL, &http_port, 4, 4, NULL, NULL, NULL);

/* ------------------------------------------------------------------ / ---- */

static int index_handler(struct http_client_ctx *client,
			 enum http_transaction_status status,
			 const struct http_request_ctx *request_ctx,
			 struct http_response_ctx *response_ctx, void *user_data)
{
	const char *query = NULL;
	size_t qlen = 0;

	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	/*
	 * Whatever follows the '?' in the URL. Zephyr hands it over on the
	 * FIRST callback of a request and zeroes the length thereafter, which
	 * is documented on struct http_request_ctx and is easy to be caught by:
	 * reading it on a later call gets an empty string, so a feature built
	 * on it works for a one-chunk page and stops working when the page
	 * grows. The page uses it for ?ch=<channel>, the clicked terminal;
	 * page_vars decides which call is the first and ignores the rest.
	 */
	if (request_ctx != NULL && request_ctx->data_len > 0U) {
		query = (const char *)request_ctx->data;
		qlen = request_ctx->data_len;
	}

	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		pmc_health_note_http_200();
	}

	return pmc_page_step(status, response_ctx, query, qlen);
}

static struct http_resource_detail_dynamic index_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = index_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(index_resource, pmc_service, "/", &index_detail);

/* --------------------------------------------------------- /state.json --- */

/*
 * One buffer, filled on the first callback and handed out whole.
 *
 * Not streamed: at this size a resumable renderer buys nothing, and unlike the
 * page there is no template to walk. If this ever needs to grow past the
 * buffer it should become a second HtmlTpl page rather than a bigger buffer -
 * the engine already streams, and a JSON template would be checked against the
 * same registry the HTML one is.
 *
 * SIZED AGAINST A MEASUREMENT: the live document is 1708 bytes, so 2048 was
 * 83% full. A silent truncation here produces invalid JSON, the page's
 * .json() throws, and its .catch() treats it as a missed poll - so the
 * failure would be invisible. 3072 leaves room for the channels still to
 * come.
 */
static char json_buf[3072];

static int state_handler(struct http_client_ctx *client,
			 enum http_transaction_status status,
			 const struct http_request_ctx *request_ctx,
			 struct http_response_ctx *response_ctx, void *user_data)
{
	static const struct http_header hdrs[] = {
		{ .name = "Content-Type", .value = "application/json" },
		{ .name = "Cache-Control", .value = "no-store" },
	};

	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	/*
	 * The handler is called for every stage of a transaction, including the
	 * terminal notification. A GET has no body, so the first call is
	 * already FINAL; answering on anything else would emit the document
	 * twice.
	 */
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = hdrs;
	response_ctx->header_count = ARRAY_SIZE(hdrs);
	response_ctx->body = (const uint8_t *)json_buf;
	response_ctx->body_len = pmc_page_state_json(json_buf,
						     sizeof(json_buf));
	response_ctx->final_chunk = true;

	pmc_health_note_http_200();
	return 0;
}

static struct http_resource_detail_dynamic state_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = state_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(state_resource, pmc_service, "/state.json", &state_detail);

/* ---------------------------------------------------------- /rs485.json ---
 * Its own route rather than a corner of /state.json, because a terminal wants
 * to poll fast and the rest of the snapshot touches every expander on the
 * board to answer. The query carries ?from=<absolute byte position>, so the
 * browser asks only for what it has not seen - and the reply says plainly
 * when the window it asked for has already been overwritten.
 *
 * SIZED AGAINST THE DRAIN RATE, which is the constraint nobody looks at: the
 * window below is what leaves per poll, so 2048 bytes every 400 ms sustains
 * 5120 B/s, or a saturated line up to about 51200 baud. The ring absorbs
 * bursts above that; past it, `lost` is the honest answer and no buffer size
 * changes it, because the WiFi link is then the limit. The old 768 sustained
 * only 19200.
 *
 * 2048 bytes is 4096 hex characters plus ~215 of fields, so this is 4608.
 */
static char rs485_buf[4608];

static int rs485_handler(struct http_client_ctx *client,
			 enum http_transaction_status status,
			 const struct http_request_ctx *request_ctx,
			 struct http_response_ctx *response_ctx, void *user_data)
{
	static const struct http_header hdrs[] = {
		{ .name = "Content-Type", .value = "application/json" },
		{ .name = "Cache-Control", .value = "no-store" },
	};
	/* Static, not automatic: 2048 bytes would be half the server thread's
	 * stack. One request at a time is guaranteed - Zephyr runs a single
	 * HTTP server thread.
	 */
	static uint8_t bytes[2048];
	uint64_t from = 0, next = 0;
	bool lost = false;
	size_t n;

	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	/* On a GET, request_ctx->data is the query string. */
	if (request_ctx != NULL && request_ctx->data != NULL &&
	    request_ctx->data_len > 0) {
		const char *q = (const char *)request_ctx->data;
		const char *p = strstr(q, "from=");

		if (p != NULL) {
			from = (uint64_t)strtoull(p + 5, NULL, 10);
		}
	}

	n = pmc_rs485_rx_read(from, bytes, sizeof(bytes), &next, &lost);

	struct pmc_485_cfg c;
	uint8_t rep[PMC_485_TX_MAX];
	size_t rep_len = 0;
	uint32_t rep_ms = 0;
	size_t w = 0;

	pmc_rs485_cfg_get(&c);
	pmc_rs485_repeat_get(rep, sizeof(rep), &rep_len, &rep_ms);

	w += snprintk(rs485_buf + w, sizeof(rs485_buf) - w,
		      "{\"baud\":%u,\"bits\":%u,\"parity\":\"%c\","
		      "\"stop\":%u,\"mode\":\"%s\",\"term\":%s,"
		      "\"slew\":%s,\"echo\":%s,\"charUs\":%u,"
		      "\"turnaroundUs\":%u,\"rxTotal\":%llu,"
		      "\"txTotal\":%llu,\"errors\":%u,\"repMs\":%u,"
		      "\"lost\":%s,\"next\":%llu,\"hex\":\"",
		      (unsigned)c.baud, (unsigned)c.data_bits, c.parity,
		      (unsigned)c.stop_bits,
		      c.mode == PMC_485_RS232 ? "rs232" :
		      c.mode == PMC_485_FULL ? "full" : "half",
		      c.term ? "true" : "false", c.slew ? "true" : "false",
		      c.echo ? "true" : "false",
		      (unsigned)pmc_rs485_char_us(),
		      (unsigned)pmc_rs485_turnaround_us(),
		      (unsigned long long)pmc_rs485_rx_total(),
		      (unsigned long long)pmc_rs485_tx_total(),
		      (unsigned)pmc_rs485_errors(), (unsigned)rep_ms,
		      lost ? "true" : "false", (unsigned long long)next);

	for (size_t i = 0; i < n && w + 3 < sizeof(rs485_buf); i++) {
		w += snprintk(rs485_buf + w, sizeof(rs485_buf) - w, "%02x",
			      bytes[i]);
	}
	w += snprintk(rs485_buf + w, sizeof(rs485_buf) - w, "\",\"rep\":\"");
	for (size_t i = 0; i < rep_len && w + 3 < sizeof(rs485_buf); i++) {
		w += snprintk(rs485_buf + w, sizeof(rs485_buf) - w, "%02x",
			      rep[i]);
	}
	w += snprintk(rs485_buf + w, sizeof(rs485_buf) - w, "\"}");

	response_ctx->status = HTTP_200_OK;
	response_ctx->headers = hdrs;
	response_ctx->header_count = ARRAY_SIZE(hdrs);
	response_ctx->body = (const uint8_t *)rs485_buf;
	response_ctx->body_len = w;
	response_ctx->final_chunk = true;

	pmc_health_note_http_200();
	return 0;
}

static struct http_resource_detail_dynamic rs485_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = rs485_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(rs485_resource, pmc_service, "/rs485.json", &rs485_detail);

/* ------------------------------------------------------------- /io ------ */

/*
 * THE ONLY ROUTE THAT CHANGES ANYTHING, and the only one declared POST.
 *
 * bitmask_of_supported_http_methods sits in the same struct as the path, so
 * the method restriction cannot be forgotten separately from the registration.
 * A GET to /io is refused by the server before any of this code runs.
 *
 * The work is in io_post.cpp because it needs the template engine's POST-body
 * gatherer, which is C++. This file stays C and stays a route table.
 */
static int io_handler(struct http_client_ctx *client,
		      enum http_transaction_status status,
		      const struct http_request_ctx *request_ctx,
		      struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	return pmc_io_post_step(status, request_ctx, response_ctx);
}

static struct http_resource_detail_dynamic io_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = io_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(io_resource, pmc_service, "/io", &io_detail);

/* ---------------------------------------------------------- the reset --- */

/*
 * POST /reset reboots the board.
 *
 * POST-ONLY, DELIBERATELY. A GET here would be fired by a browser prefetch, a
 * link checker or a favicon probe, and a board that reboots when something
 * crawls it is worse than no hook at all.
 *
 * The reboot is deferred so the response can actually leave: sys_reboot() from
 * inside the handler kills the connection mid-reply and the caller cannot tell
 * "rebooted" from "fell off the network", which is the exact ambiguity this
 * hook exists to remove.
 *
 * NOT token-gated, because this is a bench instrument and the token is for
 * writes that energize 24 V outputs. A reset is recoverable by definition.
 */
static void reboot_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	sys_reboot(SYS_REBOOT_COLD);
}
static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_fn);

static int reset_handler(struct http_client_ctx *client,
			 enum http_transaction_status status,
			 const struct http_request_ctx *request_ctx,
			 struct http_response_ctx *response_ctx, void *user_data)
{
	static const char body[] = "rebooting\n";

	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	LOG_WRN("reset requested over HTTP - cold reboot in 300 ms");
	k_work_reschedule(&reboot_work, K_MSEC(300));

	response_ctx->body = (const uint8_t *)body;
	response_ctx->body_len = sizeof(body) - 1U;
	response_ctx->final_chunk = true;
	return 0;
}

static struct http_resource_detail_dynamic reset_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = reset_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(reset_resource, pmc_service, "/reset", &reset_detail);

/* --------------------------------------------------------------- start --- */

/*
 * THE SERVER DOES NOT START ITSELF, AND IT IS STARTED FROM main().
 *
 * HTTP_SERVICE_DEFINE and HTTP_RESOURCE_DEFINE only build the tables; nothing
 * listens until http_server_start() is called. There is no
 * CONFIG_HTTP_SERVER_AUTOMATIC_START, and the omission is completely silent -
 * the build is clean, the routes exist, the board answers SMP on the same
 * address, and port 80 simply refuses the connection. Which reads exactly like
 * a firewall or a wrong address rather than a server that was never started.
 *
 * CALLED FROM main(), NOT FROM A SYS_INIT. A SYS_INIT at APPLICATION level
 * runs before main() and before the networking threads are going, and the
 * sibling Giga port - which has a working server on this same Zephyr - calls
 * it from main() straight after bringing the network up, with the comment that
 * it deliberately does NOT wait for an address because the socket is bound to
 * the wildcard. That is also why the host above is NULL: a wildcard-bound
 * listener survives a WiFi drop, where an interface-bound one does not.
 */
void pmc_httpd_init(void)
{
	/*
	 * THE PAGE SELF-TEST IS NOT CALLED FROM HERE, and the reason is the
	 * stack. This function runs on the system workqueue - net.c schedules
	 * it as a k_work from the L4_CONNECTED event - and a self-test RENDERS
	 * the page, which puts HtmlTpl's 1460-byte output buffer on whatever
	 * stack calls it. CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE is 2048. It
	 * overflowed on the first boot after the change, the MPU caught it
	 * honestly, and the board halted with no console and no network.
	 *
	 * main() runs it instead, on a stack sized for it. See prj.conf.
	 */
	const int rc = http_server_start();
	if (rc != 0) {
		/*
		 * Worth a loud message: the usual cause is running out of file
		 * descriptors or poll slots, and the errno alone names neither.
		 * See the ZVFS_POLL_MAX and ZVFS_EVENTFD_MAX notes in
		 * prj.conf - the HTTP server contributes nothing to the
		 * automatic sizing of the latter.
		 */
		LOG_ERR("http_server_start failed (%d); the panel will not answer",
			rc);
		return;
	}

	LOG_INF("panel listening on port %u", http_port);
}
