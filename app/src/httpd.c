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
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>

#include "auth.h"
#include "health.h"
#include "httpd.h"
#include "rs485.h"
#include "canbus.h"
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
/* 4096 since the encoders and a CAN receive window joined it; measured at
   about 2.7 KB with both present, and a truncated JSON fails silently in a
   browser, which is the failure this headroom exists to avoid. */
static char json_buf[4096];

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
 * THE WORST CASE, counted rather than estimated - getting this wrong truncates
 * the document into invalid JSON, which the page's .catch() reports as a missed
 * poll, so it fails silently:
 *
 *   2048-byte window as hex   4096
 *   a 256-byte repeat frame    512   (PMC_485_TX_MAX, raised from 64)
 *   the scalar fields          ~215
 *   the capture budget          ~90
 *                             ----
 *                              4913
 *
 * 6144 leaves room for another field or two. 4608 did not: it was sized when
 * PMC_485_TX_MAX was 64, and raising that to 256 quietly made the worst case
 * 4823.
 */
static char rs485_buf[6144];

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

	/* The budget travels with the terminal's own poll, because this is
	 * where someone deciding whether to arm a capture is already looking.
	 * sys_heap keeps these as counters, so reading them is O(1) and safe
	 * at the panel's 400 ms rate. */
	size_t cap_armed = 0, cap_resident = 0, heap_free = 0, heap_total = 0;

	pmc_rs485_cap_info(&cap_armed, &cap_resident, &heap_free, &heap_total);

	pmc_rs485_cfg_get(&c);
	pmc_rs485_repeat_get(rep, sizeof(rep), &rep_len, &rep_ms);

	w += snprintk(rs485_buf + w, sizeof(rs485_buf) - w,
		      "{\"baud\":%u,\"bits\":%u,\"parity\":\"%c\","
		      "\"stop\":%u,\"mode\":\"%s\",\"term\":%s,"
		      "\"slew\":%s,\"echo\":%s,\"charUs\":%u,"
		      "\"turnaroundUs\":%u,\"rxTotal\":%llu,"
		      "\"txTotal\":%llu,\"errors\":%u,\"repMs\":%u,"
		      "\"capArmed\":%u,\"capResident\":%u,"
		      "\"heapFree\":%u,\"heapTotal\":%u,"
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
		      (unsigned)cap_armed, (unsigned)cap_resident,
		      (unsigned)heap_free, (unsigned)heap_total,
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

/* ------------------------------------------------------------ /can.json -- */

/*
 * The CAN receive window as a CURSOR, so a monitor can follow the bus without
 * missing frames or replaying them - exactly the shape of /rs485.json above,
 * and for exactly the same reason.
 *
 * /state.json already carries the last few frames, and that is a different
 * job: it is a snapshot for a page that redraws, and a page polling it WILL
 * miss frames between polls. This endpoint exists so one console can watch
 * every frame while another sends them.
 *
 * `lost` is reported rather than inferred. A monitor that silently resumes
 * after an overrun shows a quiet bus, which is the one reading that must
 * never be a lie here.
 */
static int can_handler(struct http_client_ctx *client,
		       enum http_transaction_status status,
		       const struct http_request_ctx *request_ctx,
		       struct http_response_ctx *response_ctx, void *user_data)
{
	static const struct http_header hdrs[] = {
		{ .name = "Content-Type", .value = "application/json" },
		{ .name = "Cache-Control", .value = "no-store" },
	};
	static char body[2048];
	struct pmc_can_frame frames[PMC_CAN_LOG];
	struct pmc_can_state st;
	uint64_t from = 0, next = 0;
	bool lost = false;
	uint32_t rep_ms = 0, rep_sent = 0;
	size_t n, used = 0;

	ARG_UNUSED(client);
	ARG_UNUSED(user_data);

	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	if (request_ctx != NULL && request_ctx->data != NULL &&
	    request_ctx->data_len > 0) {
		const char *q = (const char *)request_ctx->data;
		const char *p = strstr(q, "from=");

		if (p != NULL) {
			from = (uint64_t)strtoull(p + 5, NULL, 10);
		}
	}

	n = pmc_can_rx_read(from, frames, ARRAY_SIZE(frames), &next, &lost);
	pmc_can_state_read(&st);
	pmc_can_repeat_get(&rep_ms, &rep_sent);

	used += (size_t)snprintk(body + used, sizeof(body) - used,
				 "{\"next\":%llu,\"lost\":%s,\"state\":%d,"
				 "\"loopback\":%s,\"bitrate\":%u,"
				 "\"txErr\":%u,\"rxErr\":%u,\"rx\":%u,"
				 "\"txQueued\":%u,\"txOk\":%u,\"txPin\":%u,"
				 "\"repMs\":%u,\"repSent\":%u,\"f\":[",
				 next, lost ? "true" : "false", st.bus_state,
				 st.loopback ? "true" : "false",
				 (unsigned)st.bitrate,
				 (unsigned)st.tx_err_cnt,
				 (unsigned)st.rx_err_cnt,
				 (unsigned)st.rx_frames,
				 (unsigned)st.tx_queued, (unsigned)st.tx_ok,
				 (unsigned)st.tx_pin_toggling,
				 (unsigned)rep_ms, (unsigned)rep_sent);

	for (size_t i = 0; i < n && used < sizeof(body) - 96; i++) {
		char hex[2 * 8 + 1] = { 0 };
		size_t p = 0;

		for (uint8_t b = 0; b < frames[i].dlc && b < 8U; b++) {
			p += (size_t)snprintk(hex + p, sizeof(hex) - p, "%02x",
					      frames[i].data[b]);
		}
		used += (size_t)snprintk(body + used, sizeof(body) - used,
					 "%s{\"id\":%u,\"ext\":%u,\"rtr\":%u,"
					 "\"dlc\":%u,\"d\":\"%s\",\"t\":%lld}",
					 i ? "," : "", (unsigned)frames[i].id,
					 frames[i].ext, frames[i].rtr,
					 frames[i].dlc, hex, frames[i].stamp);
	}
	used += (size_t)snprintk(body + used, sizeof(body) - used, "]}");

	response_ctx->body = (const uint8_t *)body;
	response_ctx->body_len = used;
	response_ctx->headers = hdrs;
	response_ctx->header_count = ARRAY_SIZE(hdrs);
	response_ctx->final_chunk = true;
	return 0;
}

static struct http_resource_detail_dynamic can_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = can_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(can_resource, pmc_service, "/can.json", &can_detail);

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

/* ---------------------------------------------------------------- /ota ----
 * IMAGE UPLOAD OVER TCP, AND THE REASON IS RETRANSMISSION.
 *
 * SMP over UDP is lockstep with no window: one datagram lost in either
 * direction ends the transfer, and the protocol has nothing to resend it with.
 * Measured on this bench on 2026-11-18 - see docs/MEASURED.md - the loss rate
 * is about 0.06% per datagram, which is invisible to a ping test and fatal to
 * a 1,100-datagram upload about half the time. In the same window TCP moved
 * 492 KB through the same radio, resending 355 segments and dropping none,
 * because that is what TCP does.
 *
 * Zephyr has no TCP transport for SMP (nine transports, none of them TCP), so
 * this is our own route rather than a configuration change. It writes the
 * SIGNED image straight into the secondary slot through the same flash_img
 * API that img_mgmt uses, so MCUboot's slot logic and signature check are
 * untouched - this replaces the transport and nothing else.
 *
 * POST /ota   body = zephyr.signed.bin, header X-PMC-Token = the API token
 * GET  /ota   reports how many bytes are already staged, so a retry can skip
 *             what arrived (the upload is resumable only from a fresh start
 *             today; the offset is reported because it is the number a human
 *             wants when a transfer dies).
 *
 * Setting the pending flag is NOT done here. It is a separate, deliberate act
 * on /io, so that staging an image and arming it stay two decisions.
 */

/* The token travels in a header, not the query string: a URL ends up in logs,
 * shell history and the browser's address bar, and this one grants writes.
 */
HTTP_SERVER_REGISTER_HEADER_CAPTURE(pmc_ota_token, "X-PMC-Token");

static struct flash_img_context s_ota;
static bool   s_ota_open;
static bool   s_ota_bad;
static char   s_ota_reply[200];
/*
 * WHY THE REFUSAL CARRIES ITS OWN DIAGNOSIS. The first version logged the
 * header count to the console and the line never appeared: deferred logging
 * drops under the net_pkt flood, so the one message that mattered was
 * discarded among 781,997 others. A refusal has to explain itself to the
 * caller, who is the only party guaranteed to be listening.
 */
static char   s_ota_why[120];
/*
 * WHICH CLIENT OWNS THE UPLOAD. One server thread means one callback at a
 * time, but it does NOT mean one request at a time: with four client slots,
 * another client's DATA_MORE can interleave between ours, and two bodies
 * feeding one flash_img context would write a corrupt image that still passed
 * every length check here and failed only at MCUboot's signature.
 */
static struct http_client_ctx *s_ota_owner;

static const char *ota_token(const struct http_request_ctx *req)
{
	if (req == NULL || req->headers == NULL) {
		return NULL;
	}
	for (size_t i = 0; i < req->header_count; i++) {
		if (req->headers[i].name != NULL &&
		    strcasecmp(req->headers[i].name, "X-PMC-Token") == 0) {
			return req->headers[i].value;
		}
	}
	return NULL;
}

static void ota_finish(struct http_response_ctx *rsp, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintk(s_ota_reply, sizeof(s_ota_reply), fmt, ap);
	va_end(ap);

	rsp->status = HTTP_200_OK;
	rsp->body = (const uint8_t *)s_ota_reply;
	rsp->body_len = (n < 0) ? 0U : (size_t)n;
	rsp->final_chunk = true;
}

static void ota_abandon(void)
{
	s_ota_open = false;
	s_ota_bad = false;
	s_ota_owner = NULL;
}

static int ota_handler(struct http_client_ctx *client,
		       enum http_transaction_status status,
		       const struct http_request_ctx *request_ctx,
		       struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(user_data);

	if (status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		/* The connection is done with, whatever happened. */
		if (s_ota_owner == client) {
			ota_abandon();
		}
		return 0;
	}

	/* GET: just the staged byte count. No token - it reveals nothing and
	 * it is the number someone needs when a transfer has just died.
	 */
	if (request_ctx != NULL && request_ctx->data_len == 0U && !s_ota_open &&
	    status == HTTP_SERVER_REQUEST_DATA_FINAL && s_ota_owner == NULL) {
		ota_finish(response_ctx, "staged %u bytes\n",
			   (unsigned)flash_img_bytes_written(&s_ota));
		pmc_health_note_http_200();
		return 0;
	}

	if (!s_ota_open && !s_ota_bad) {
		const char *tok = ota_token(request_ctx);

		if (s_ota_owner != NULL && s_ota_owner != client) {
			s_ota_bad = true;
			snprintk(s_ota_why, sizeof(s_ota_why),
				 "another upload is already running");
			goto reply;
		}
		if (!pmc_auth_configured()) {
			pmc_auth_note_refusal();
			s_ota_bad = true;
			snprintk(s_ota_why, sizeof(s_ota_why),
				 "no SECRET_API_TOKEN compiled in");
			goto reply;
		}
		if (!pmc_auth_ok(tok)) {
			pmc_auth_note_refusal();
			s_ota_bad = true;
			/* Name what arrived. A header that was never captured
			 * and a token that did not match are different faults,
			 * and the first one is the server's mistake.
			 */
			snprintk(s_ota_why, sizeof(s_ota_why),
				 "token %s; %u header(s) captured, status %d",
				 tok ? "did not match" : "header absent",
				 (unsigned)(request_ctx ?
					    request_ctx->header_count : 0),
				 (int)(request_ctx ?
				       (int)request_ctx->headers_status : -1));
			goto reply;
		}

		int err = flash_img_init(&s_ota);

		if (err != 0) {
			s_ota_bad = true;
			snprintk(s_ota_why, sizeof(s_ota_why),
				 "flash_img_init failed (%d)", err);
			goto reply;
		}
		s_ota_open = true;
		s_ota_owner = client;
		LOG_INF("upload started");
	}

	if (s_ota_open && request_ctx != NULL && request_ctx->data_len > 0U) {
		int err = flash_img_buffered_write(
			&s_ota, (const uint8_t *)request_ctx->data,
			request_ctx->data_len, false);

		if (err != 0) {
			snprintk(s_ota_why, sizeof(s_ota_why),
				 "flash write failed at %u (%d)",
				 (unsigned)flash_img_bytes_written(&s_ota),
				 err);
			s_ota_bad = true;
			s_ota_open = false;
		}
	}

reply:
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;   /* more body to come */
	}

	if (s_ota_bad) {
		ota_finish(response_ctx, "refused: %s\n",
			   s_ota_why[0] ? s_ota_why : "no reason recorded");
		s_ota_why[0] = '\0';
		ota_abandon();
		return 0;
	}

	/* Flush the tail of the block buffer. Without this the last partial
	 * 512-byte block is never written and the image is short by up to 511
	 * bytes - which MCUboot reports only as a failed signature.
	 */
	int err = flash_img_buffered_write(&s_ota, NULL, 0, true);
	size_t n = flash_img_bytes_written(&s_ota);

	if (err != 0) {
		LOG_ERR("flush failed: %d", err);
		ota_finish(response_ctx, "refused: flush failed (%d)\n", err);
	} else {
		LOG_INF("upload complete: %u bytes staged", (unsigned)n);
		ota_finish(response_ctx,
			   "staged %u bytes; now arm it with do=otatest\n",
			   (unsigned)n);
	}
	ota_abandon();
	pmc_health_note_http_200();
	return 0;
}

static struct http_resource_detail_dynamic ota_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods =
			BIT(HTTP_POST) | BIT(HTTP_GET),
	},
	.cb = ota_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(ota_resource, pmc_service, "/ota", &ota_detail);

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
