/*
 * NTP -> the carrier's RTC. The design, and why it needs no second time base,
 * is in clock.h. This file is the loop.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>
#include <zephyr/sys/timeutil.h>

#include "clock.h"

LOG_MODULE_REGISTER(pmc_clock, LOG_LEVEL_INF);

/*
 * A HOSTNAME, NOT AN ADDRESS, and that is not merely good manners here. This
 * repository's own pre-commit check 6 blocks a dotted quad in a tracked file,
 * so a hardcoded NTP address could not be committed even if it were a good
 * idea. The DNS server arrives with the DHCP lease
 * (CONFIG_NET_DHCPV4_OPTION_DNS_ADDRESS), so this resolves on whatever network
 * the board is plugged into rather than on the one it was built for.
 */
#define PMC_NTP_SERVER   "pool.ntp.org"
#define PMC_NTP_TIMEOUT  4000U

/* How long between queries once the clock is set. An hour is far more often
 * than the measurement needs and far less often than the pool minds. */
#define PMC_SYNC_PERIOD_S  3600
/* And how soon to try again while there is still no time at all. */
#define PMC_RETRY_S        60

/*
 * Below this span the figure is arithmetic rather than measurement. The RTC
 * reports whole seconds, so one second of quantization over an hour is already
 * 278 ppm - larger than anything a 32.768 kHz crystal will actually do. Six
 * hours puts the quantization floor near 46 ppm and a day near 11; neither is
 * precise, both are honest, and the span is rendered beside the number so a
 * reader can judge it.
 */
#define PMC_MIN_SPAN_S  (6 * 3600)

/*
 * Re-set the part if it has drifted further than this. Deliberately generous:
 * every re-set destroys the measurement in progress by moving the anchor, so a
 * clock that is merely a little off is left alone and measured instead.
 */
#define PMC_RESET_IF_OFF_S  30

/*
 * 3072, NOT 2048. MEASURED: at 2048 this thread's high-water mark was
 * 2032 bytes - 16 bytes of margin, 99% - read off `kernel thread stacks`
 * 75 s after a cold boot on 2026-11-18. It is the deepest stack on the
 * board, because SNTP resolution and the socket work happen on it.
 *
 * Nothing had crashed, which is the point: a 16-byte margin does not
 * announce itself, it just waits for one more frame.
 */
#define PMC_CLOCK_STACK  3072

#if DT_HAS_COMPAT_STATUS_OKAY(nxp_pcf8563)
static const struct device *const pmc_rtc =
	DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(nxp_pcf8563));
#else
static const struct device *const pmc_rtc;
#endif

static K_SEM_DEFINE(s_net_up, 0, 1);
static K_THREAD_STACK_DEFINE(s_stack, PMC_CLOCK_STACK);
static struct k_thread s_thread;

/* Guards the published measurement against a reader mid-update. */
static struct k_spinlock s_lock;
static struct pmc_clock_drift s_drift;

/* The anchor: UTC at the moment the RTC was last written. Drift is measured
 * from here, so it moves only when the part is actually set. */
static int64_t s_anchor;

void pmc_clock_net_up(void)
{
	k_sem_give(&s_net_up);
}

void pmc_clock_get(struct pmc_clock_drift *out)
{
	const k_spinlock_key_t key = k_spin_lock(&s_lock);

	*out = s_drift;
	k_spin_unlock(&s_lock, key);
}

/* UTC seconds from the part, or 0 when it has no trustworthy time. */
static int64_t rtc_read_epoch(void)
{
	struct rtc_time rt;

	if (pmc_rtc == NULL || !device_is_ready(pmc_rtc)) {
		return 0;
	}
	if (rtc_get_time(pmc_rtc, &rt) != 0) {
		return 0;  /* -ENODATA: VL set, the oscillator had stopped */
	}
	return timeutil_timegm64(rtc_time_to_tm(&rt));
}

static int rtc_write_epoch(int64_t epoch)
{
	struct rtc_time rt;
	struct tm tm;
	time_t t = (time_t)epoch;

	if (pmc_rtc == NULL || !device_is_ready(pmc_rtc)) {
		return -ENODEV;
	}
	if (gmtime_r(&t, &tm) == NULL) {
		return -EINVAL;
	}

	/*
	 * FIELD BY FIELD, NOT A CAST. struct rtc_time is layout-compatible
	 * with struct tm for the fields they share and the header offers
	 * rtc_time_to_tm() to go that way safely - but it is LONGER: it
	 * carries tm_nsec after them. Casting a struct tm* the other way and
	 * handing it to the driver would have it read that field from one past
	 * the end of an object that does not have it.
	 */
	memset(&rt, 0, sizeof(rt));
	rt.tm_sec = tm.tm_sec;
	rt.tm_min = tm.tm_min;
	rt.tm_hour = tm.tm_hour;
	rt.tm_mday = tm.tm_mday;
	rt.tm_mon = tm.tm_mon;
	rt.tm_year = tm.tm_year;
	rt.tm_wday = tm.tm_wday;
	rt.tm_yday = tm.tm_yday;
	rt.tm_isdst = -1;
	rt.tm_nsec = 0;

	return rtc_set_time(pmc_rtc, &rt);
}

/* One query. Returns true if NTP answered. */
static bool sync_once(void)
{
	struct sntp_time ts;
	int64_t ntp;
	int64_t rtc;
	int rc;

	rc = sntp_simple(PMC_NTP_SERVER, PMC_NTP_TIMEOUT, &ts);
	if (rc != 0) {
		const k_spinlock_key_t key = k_spin_lock(&s_lock);

		s_drift.failures++;
		k_spin_unlock(&s_lock, key);
		LOG_WRN("NTP query failed (%d); the clock keeps whatever it has",
			rc);
		return false;
	}

	ntp = (int64_t)ts.seconds;
	rtc = rtc_read_epoch();

	{
		const k_spinlock_key_t key = k_spin_lock(&s_lock);

		s_drift.syncs++;
		k_spin_unlock(&s_lock, key);
	}

	/*
	 * MEASURE BEFORE WRITING. Once the part is set the error is zero by
	 * construction, so the comparison has to happen while the old value is
	 * still in it - which is also why a re-set moves the anchor.
	 */
	if (rtc != 0 && s_anchor != 0) {
		const int64_t err = rtc - ntp;
		const int64_t span = ntp - s_anchor;

		if (span >= PMC_MIN_SPAN_S) {
			const k_spinlock_key_t key = k_spin_lock(&s_lock);

			s_drift.ppm_tenths =
				(int32_t)((err * 10000000LL) / span);
			s_drift.span_s = (uint32_t)span;
			s_drift.valid = true;
			k_spin_unlock(&s_lock, key);

			LOG_INF("RTC %lld s against UTC over %lld s: %d.%d ppm",
				(long long)err, (long long)span,
				s_drift.ppm_tenths / 10,
				abs(s_drift.ppm_tenths % 10));
		} else {
			LOG_DBG("%lld s of span so far; %d needed before a "
				"rate means anything", (long long)span,
				PMC_MIN_SPAN_S);
		}
	}

	if (rtc == 0 || llabs(rtc - ntp) > PMC_RESET_IF_OFF_S) {
		const int wrc = rtc_write_epoch(ntp);

		if (wrc != 0) {
			LOG_ERR("could not set the RTC (%d)", wrc);
			return true;
		}

		s_anchor = ntp;
		{
			const k_spinlock_key_t key = k_spin_lock(&s_lock);

			s_drift.set_epoch = ntp;
			s_drift.valid = false;   /* the span starts over */
			s_drift.span_s = 0;
			k_spin_unlock(&s_lock, key);
		}
		LOG_INF("RTC set from NTP: epoch %lld%s", (long long)ntp,
			rtc == 0 ? " (it had no time)" : " (it had drifted far)");
	} else if (s_anchor == 0) {
		/*
		 * The part already had a trustworthy time when we arrived and
		 * it agrees with UTC, so there is nothing to correct - but
		 * there is also no anchor, because nothing here set it. Take
		 * this moment as the anchor so the next query can measure.
		 */
		s_anchor = ntp;
		LOG_INF("RTC already agreed with UTC; measuring from here");
	}

	return true;
}

static void clock_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_sem_take(&s_net_up, K_FOREVER);

	/* The lease is in, but a DNS server arriving with it is a separate
	 * packet and the resolver may not have it yet. One short wait here is
	 * cheaper than a failed query and a minute of backoff. */
	k_sleep(K_SECONDS(2));

	for (;;) {
		const bool ok = sync_once();
		const bool have_time = (rtc_read_epoch() != 0);

		k_sleep(K_SECONDS((ok && have_time) ? PMC_SYNC_PERIOD_S
						    : PMC_RETRY_S));
	}
}

void pmc_clock_init(void)
{
	/*
	 * Priority 10, preemptible: below the networking threads, because this
	 * one blocks on a socket for up to PMC_NTP_TIMEOUT and must never be
	 * what keeps them from running.
	 */
	k_thread_create(&s_thread, s_stack, K_THREAD_STACK_SIZEOF(s_stack),
			clock_thread, NULL, NULL, NULL, 10, 0, K_NO_WAIT);
	k_thread_name_set(&s_thread, "pmc_clock");
}
