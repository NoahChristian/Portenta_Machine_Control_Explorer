/*
 * The web panel's server.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 *
 * TWO ROUTES, AND THE SPLIT IS DELIBERATE.
 *
 *   /            the page, a static byte array generated from web/index.html
 *   /state.json  everything that changes, as JSON
 *
 * No markup is assembled in C. The page is authored as HTML, opens in a
 * browser as a file, and is embedded verbatim by tools/gen_page.py. A page
 * built by concatenating strings in a handler cannot be previewed, diffs
 * badly, and grows escaping bugs that appear in one branch only.
 *
 * This is the read-only half. Writing to channels will not be bolted onto
 * these handlers: it needs the request to post to the owning thread and read
 * back a verified result, and it needs the route table to carry the method and
 * the auth gate in the same row. That shape is why this file registers routes
 * from a table rather than scattering HTTP_RESOURCE_DEFINE calls - the
 * PowerMonitor bench shipped two unauthenticated write routes precisely
 * because the registrations were individually written and individually
 * forgotten.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/hostname.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>

#include <stm32_ll_system.h>

#include "health.h"
#include "web_gen/page_index.h"

LOG_MODULE_REGISTER(pmc_httpd, LOG_LEVEL_INF);

static uint16_t http_port = 80;

/*
 * One buffer, reused, and the handler is re-entered until it says it is done.
 *
 * Zephyr's dynamic resource is a GENERATOR, not a sink: the handler is called
 * repeatedly and each call returns a pointer and a length, setting final_chunk
 * when there is no more. So the JSON is rendered once into this buffer on the
 * first call of a request and then handed out in one piece.
 *
 * Sized for the whole document rather than streamed, because at this size the
 * complexity of a resumable renderer buys nothing. When the channel table
 * arrives - 45 channels rather than 3 - this is the thing that has to change,
 * and it will want the chunked HtmlTpl path instead of a bigger buffer.
 */
static char json_buf[2048];

#define AI_SWITCHES (LL_SYSCFG_ANALOG_SWITCH_PA1 | \
		     LL_SYSCFG_ANALOG_SWITCH_PC2 | \
		     LL_SYSCFG_ANALOG_SWITCH_PC3)

static const struct adc_dt_spec ai[] = {
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),
};
static const char *const ai_name[] = { "AI0", "AI1", "AI2" };
static const char *const ai_pad[]  = { "PC_3C", "PC_2C", "PA_1C" };
static const uint32_t ai_switch[] = {
	LL_SYSCFG_ANALOG_SWITCH_PC3,
	LL_SYSCFG_ANALOG_SWITCH_PC2,
	LL_SYSCFG_ANALOG_SWITCH_PA1,
};

static int read_ai(size_t i, int16_t *raw, int32_t *mv)
{
	struct adc_sequence seq = { .buffer = raw, .buffer_size = sizeof(*raw) };

	if (!adc_is_ready_dt(&ai[i])) {
		return -ENODEV;
	}
	if (adc_channel_setup_dt(&ai[i]) != 0) {
		return -EIO;
	}
	if (adc_sequence_init_dt(&ai[i], &seq) != 0) {
		return -EIO;
	}
	if (adc_read_dt(&ai[i], &seq) != 0) {
		return -EIO;
	}
	*mv = *raw;
	(void)adc_raw_to_millivolts_dt(&ai[i], mv);
	return 0;
}

static size_t render_state(char *out, size_t cap)
{
	const uint32_t pmcr = SYSCFG->PMCR & AI_SWITCHES;
	size_t n = 0;

	n += snprintk(out + n, cap - n,
		      "{\"host\":\"%s\",\"fw\":\"%s\",\"uptime\":%lld,",
		      net_hostname_get(), CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION,
		      (long long)(k_uptime_get() / 1000));

	/* Health, from the same evaluator the shell and the gate use. */
	n += snprintk(out + n, cap - n, "\"health\":[");
	for (size_t i = 0; i < pmc_health_check_count(); i++) {
		const char *detail = "";
		const bool ok = pmc_health_check(i, &detail);

		n += snprintk(out + n, cap - n,
			      "%s{\"name\":\"%s\",\"ok\":%s,\"detail\":\"%s\"}",
			      i ? "," : "", pmc_health_check_name(i),
			      ok ? "true" : "false", detail);
	}
	n += snprintk(out + n, cap - n, "],");

	/* Slots, read from the image headers rather than from a build string. */
	n += snprintk(out + n, cap - n, "\"slots\":[");
	static const uint8_t slot_id[] = {
#if PARTITION_EXISTS(slot0_partition)
		PARTITION_ID(slot0_partition),
#endif
#if PARTITION_EXISTS(slot1_partition)
		PARTITION_ID(slot1_partition),
#endif
	};
	for (size_t i = 0; i < ARRAY_SIZE(slot_id); i++) {
		struct mcuboot_img_header hdr;
		char ver[24] = "-";

		if (boot_read_bank_header(slot_id[i], &hdr, sizeof(hdr)) == 0) {
			snprintk(ver, sizeof(ver), "%u.%u.%u",
				 hdr.h.v1.sem_ver.major, hdr.h.v1.sem_ver.minor,
				 hdr.h.v1.sem_ver.revision);
		}
		n += snprintk(out + n, cap - n,
			      "%s{\"slot\":%u,\"version\":\"%s\",\"active\":%s,"
			      "\"confirmed\":%s}",
			      i ? "," : "", (unsigned)i, ver,
			      i == 0 ? "true" : "false",
			      (i == 0 && boot_is_img_confirmed()) ? "true" : "false");
	}
	n += snprintk(out + n, cap - n, "],");

	/* Analog inputs, with the switch state beside each reading, because a
	 * value from a channel whose switch is closed is not a measurement of
	 * anything on the terminal block. */
	n += snprintk(out + n, cap - n, "\"ai\":[");
	for (size_t i = 0; i < ARRAY_SIZE(ai); i++) {
		int16_t raw = 0;
		int32_t mv = 0;
		const int rc = read_ai(i, &raw, &mv);

		n += snprintk(out + n, cap - n,
			      "%s{\"name\":\"%s\",\"pad\":\"%s\",\"raw\":%d,"
			      "\"mv\":%d,\"open\":%s}",
			      i ? "," : "", ai_name[i], ai_pad[i],
			      rc == 0 ? raw : -1, rc == 0 ? mv : -1,
			      (pmcr & ai_switch[i]) ? "true" : "false");
	}
	n += snprintk(out + n, cap - n, "]}");

	return n < cap ? n : cap - 1;
}

static int state_handler(struct http_client_ctx *client,
			 enum http_transaction_status status,
			 const struct http_request_ctx *request_ctx,
			 struct http_response_ctx *response_ctx,
			 void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(request_ctx);
	ARG_UNUSED(user_data);

	/*
	 * The handler is called for every stage of a transaction, including
	 * the terminal notifications. A GET has no body, so the first call is
	 * already FINAL; answering on anything else would emit the document
	 * twice.
	 */
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}

	static const struct http_header hdrs[] = {
		{ .name = "Content-Type", .value = "application/json" },
		/* The page polls; a cached snapshot is worse than none. */
		{ .name = "Cache-Control", .value = "no-store" },
	};

	const size_t n = render_state(json_buf, sizeof(json_buf));

	response_ctx->status = 200;
	response_ctx->headers = hdrs;
	response_ctx->header_count = ARRAY_SIZE(hdrs);
	response_ctx->body = (const uint8_t *)json_buf;
	response_ctx->body_len = n;
	response_ctx->final_chunk = true;

	pmc_health_note_http_200();
	return 0;
}

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
HTTP_SERVICE_DEFINE(pmc_service, NULL, &http_port, 2, 2, NULL, NULL, NULL);

static struct http_resource_detail_static index_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_STATIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
		.content_type = "text/html; charset=utf-8",
	},
	.static_data = page_index,
	.static_data_len = PAGE_INDEX_LEN,
};
HTTP_RESOURCE_DEFINE(index_resource, pmc_service, "/", &index_detail);

static struct http_resource_detail_dynamic state_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		/*
		 * GET ONLY, and stated here rather than checked in the
		 * handler. A resource's bitmask puts the method in the same
		 * place as the route, so a method cannot be forgotten the way
		 * it can when each handler polices itself.
		 */
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = state_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(state_resource, pmc_service, "/state.json", &state_detail);

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
	const int rc = http_server_start();

	if (rc != 0) {
		/*
		 * Worth a loud message: the usual cause is running out of
		 * file descriptors or poll slots, and the errno alone names
		 * neither. See the ZVFS_POLL_MAX and ZVFS_EVENTFD_MAX notes in
		 * prj.conf - the HTTP server contributes nothing to the
		 * automatic sizing of the latter.
		 */
		LOG_ERR("http_server_start failed (%d); the panel will not answer", rc);
		return;
	}

	LOG_INF("panel listening on port %u", http_port);
}
