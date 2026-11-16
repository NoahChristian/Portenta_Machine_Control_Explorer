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
 *     a sibling project on this bench is that a page is a TEMPLATE the device renders, with
 *     every substitution checked at build time against a registry. A byte
 *     array is not checked against anything: a typo in a JavaScript field name
 *     produced an empty cell and no error anywhere.
 *   - Every reading on screen came from a different instant, and nothing said
 *     so. The panel now takes one snapshot per render, and says in its own
 *     heading that it does.
 *   - A page that polls against a server with a small fixed number of client
 *     slots wedges itself. That cost a debugging session and a
 *     `Connection: close` header; with nothing polling, neither is needed.
 *
 * WHY A TABLE AND NOT SCATTERED REGISTRATIONS. A resource declares
 * bitmask_of_supported_http_methods in the same place as its path, which is
 * the structural answer to the bug a sibling project on this bench shipped twice:
 * server.on() lets a mutating route be registered with no method restriction
 * and no auth, and the registration written individually is the one forgotten
 * individually. Both routes below are GET only and say so where they live.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>

#include "health.h"
#include "httpd.h"
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
 */
static char json_buf[2048];

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
