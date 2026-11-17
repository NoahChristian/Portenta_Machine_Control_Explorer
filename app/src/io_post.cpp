/*
 * POST /io - the only route that changes anything.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * The method restriction lives with the path in httpd.c's route table, so a
 * GET never reaches this file. What happens here is: gather the body, check
 * the token BEFORE parsing anything for meaning, then dispatch on a verb with
 * the channel named rather than numbered.
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <HtmlTplServerZephyr.h>

#include "ao.h"
#include "auth.h"
extern "C" {
#include "health.h"
}
#include "io_post.h"
#include "io_write.h"
#include "temp.h"

LOG_MODULE_REGISTER(pmc_post, LOG_LEVEL_INF);

/*
 * FILE SCOPE, NOT THE STACK. HtmlTplForm NUL-terminates decoded values in
 * place and keeps pointers into them, so a buffer that dies with the handler
 * would leave those pointers in a dead frame. One request at a time is
 * guaranteed here because Zephyr runs a single server thread.
 */
static char s_body[512];
static HtmlTplZephyrBody s_post = { s_body, sizeof(s_body), 0, false };
static char s_reply[224];

/*
 * One urlencoded field. Nothing is percent-decoded: every value this route
 * accepts is [A-Za-z0-9_-] by construction - a channel name, a verb, a token,
 * a number - so anything carrying an escape fails to match the allow-list,
 * which is the outcome wanted anyway. Decoding would add a parser whose bugs
 * all point at a 24 V output.
 */
static const char *field(const char *body, const char *key,
			 char *out, size_t cap)
{
	const size_t klen = strlen(key);
	const char *p = body;

	while (p != nullptr && *p != '\0') {
		const char *amp;

		if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
			size_t n;

			p += klen + 1;
			amp = strchr(p, '&');
			n = (amp != nullptr) ? (size_t)(amp - p) : strlen(p);
			if (n >= cap) {
				return nullptr;
			}
			memcpy(out, p, n);
			out[n] = '\0';
			return out;
		}
		amp = strchr(p, '&');
		p = (amp != nullptr) ? amp + 1 : nullptr;
	}
	return nullptr;
}

static size_t refuse(const char *why, ...)
{
	va_list ap;
	int n;

	va_start(ap, why);
	n = vsnprintk(s_reply, sizeof(s_reply), why, ap);
	va_end(ap);
	return (n < 0) ? 0U : (size_t)n;
}

extern "C" int pmc_io_post_step(enum http_transaction_status status,
				const struct http_request_ctx *req,
				struct http_response_ctx *rsp)
{
	static const struct http_header hdrs[] = {
		{ "Content-Type", "text/plain; charset=utf-8" },
		{ "Cache-Control", "no-store" },
	};
	char tok[64];
	char verb[20];
	char chan[20];
	char val[12];
	char detail[160];
	size_t n;

	if (status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		s_post.len = 0;
		s_post.overflow = false;
		return 0;
	}

	if (!htmlTplFeedFromZephyr(s_post, status, req)) {
		return 0;  /* more of the body still to come */
	}

	rsp->status = HTTP_200_OK;
	rsp->headers = hdrs;
	rsp->header_count = ARRAY_SIZE(hdrs);
	rsp->body = reinterpret_cast<const uint8_t *>(s_reply);
	rsp->final_chunk = true;

	if (s_post.overflow) {
		n = refuse("refused: body longer than %u bytes\n",
			   (unsigned)sizeof(s_body));
		goto done;
	}

	/* The token first. An unauthorized request must not reach a channel
	 * lookup, let alone a pin. */
	if (!pmc_auth_configured()) {
		pmc_auth_note_refusal();
		n = refuse("refused: no SECRET_API_TOKEN compiled in, so every "
			   "write is refused (%u since boot)\n",
			   pmc_auth_refusals());
		goto done;
	}
	if (!pmc_auth_ok(field(s_body, "token", tok, sizeof(tok)))) {
		pmc_auth_note_refusal();
		n = refuse("refused: bad or missing token (%u refusals since "
			   "boot)\n", pmc_auth_refusals());
		goto done;
	}

	if (field(s_body, "do", verb, sizeof(verb)) == nullptr) {
		n = refuse("refused: no verb\n");
		goto done;
	}

	if (strcmp(verb, "set") == 0) {
		if (field(s_body, "chan", chan, sizeof(chan)) == nullptr ||
		    field(s_body, "v", val, sizeof(val)) == nullptr) {
			n = refuse("refused: set needs chan and v\n");
			goto done;
		}
		const enum pmc_write_result r =
			pmc_io_write(chan, val[0] == '1', detail,
				     sizeof(detail));
		n = refuse("%s: %s\n", pmc_write_result_str(r), detail);
		goto done;
	}

	if (strcmp(verb, "pulse") == 0) {
		if (field(s_body, "chan", chan, sizeof(chan)) == nullptr ||
		    field(s_body, "ms", val, sizeof(val)) == nullptr) {
			n = refuse("refused: pulse needs chan and ms\n");
			goto done;
		}
		const uint32_t ms = (uint32_t)atoi(val);

		/* Bounded here rather than trusted. A pulse is a 24 V output
		 * held on for as long as the number says. */
		if (ms == 0U || ms > 5000U) {
			n = refuse("refused: ms must be 1..5000, got '%s'\n",
				   val);
			goto done;
		}
		const enum pmc_write_result r =
			pmc_io_pulse(chan, ms, detail, sizeof(detail));
		n = refuse("%s: %s\n", pmc_write_result_str(r), detail);
		goto done;
	}

	if (strcmp(verb, "ao") == 0) {
		if (field(s_body, "chan", chan, sizeof(chan)) == nullptr ||
		    field(s_body, "mv", val, sizeof(val)) == nullptr) {
			n = refuse("refused: ao needs chan and mv\n");
			goto done;
		}
		const long mv = atol(val);

		/* Bounded here as well as in pmc_ao_set: a negative number would
		 * otherwise arrive there as a very large unsigned one. */
		if (mv < 0 || mv > (long)PMC_AO_MAX_MV) {
			n = refuse("refused: mv must be 0..%u, got '%s'\n",
				   (unsigned)PMC_AO_MAX_MV, val);
			goto done;
		}
		const enum pmc_write_result r =
			pmc_ao_set(chan, (uint32_t)mv, detail, sizeof(detail));
		n = refuse("%s: %s\n", pmc_write_result_str(r), detail);
		goto done;
	}

	if (strcmp(verb, "tmode") == 0) {
		/*
		 * The acquisition mode is a property of the board, not of a
		 * channel, so this verb takes no chan. Datasheet 3-9: one
		 * single-pole double-throw switch chooses between the two
		 * converters, and whichever way it is thrown applies to
		 * whichever channel the one-hot select has enabled.
		 */
		char mode[8];

		if (field(s_body, "mode", mode, sizeof(mode)) == nullptr) {
			n = refuse("refused: tmode needs mode\n");
			goto done;
		}
		if (pmc_temp_mode_set(mode, detail, sizeof(detail))) {
			n = refuse("%s", detail);
		} else {
			n = refuse("%s", detail);
		}
		goto done;
	}

	n = refuse("refused: unknown verb '%s'\n", verb);

done:
	rsp->body_len = n;
	pmc_health_note_http_200();
	return 0;
}
