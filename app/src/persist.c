/*
 * Settings that survive a power cycle. Why the outputs wait is in persist.h.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

#include "persist.h"
#include "expander.h"
#include "io_write.h"
#include "rs485.h"
#include "temp.h"

LOG_MODULE_REGISTER(pmc_persist, LOG_LEVEL_INF);

#define PMC_DO_COUNT 8

/*
 * The two records. Packed is deliberate: these cross an OTA, so the compiler's
 * padding would be a silent part of the on-flash layout, and a future build
 * that pads differently would read the old bytes shifted. The version byte
 * catches a change that is declared; packing removes a class of change that
 * is not.
 */
struct persist_out {
	uint8_t  ver;
	int8_t   do_out[PMC_DO_COUNT];
	int8_t   pdio_cmd[PMC_PDIO_COUNT];
	uint32_t pulse_mask;
} __packed;

struct persist_cfg {
	uint8_t  ver;
	uint32_t baud;
	uint8_t  data_bits;
	char     parity;
	uint8_t  stop_bits;
	uint8_t  mode;        /* enum pmc_485_mode, stored as a byte */
	uint8_t  term;
	uint8_t  slew;
	uint8_t  echo;
	uint8_t  temp_mode;   /* enum pmc_temp_mode */
	uint8_t  temp_wires;  /* 2 or 3 */
} __packed;

static struct persist_out s_out;
static struct persist_cfg s_cfg;

static struct pmc_persist_state s_state;
static struct k_mutex          s_lock;
static atomic_t                s_saves;

static void save_work(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(save_timer, save_work);

/* ------------------------------------------------------------- loading --- */

static int cfg_set(const char *key, size_t len, settings_read_cb read_cb,
		   void *cb_arg)
{
	struct persist_cfg tmp;
	ssize_t n;

	if (settings_name_next(key, NULL) != 0) {
		return -ENOENT;
	}

	/*
	 * LENGTH FIRST, THEN VERSION. A record written by an image whose struct
	 * was a different size would otherwise be read into however much of
	 * this one it happens to fill, and the version byte it carries would be
	 * read from the right place by luck. Reject on either.
	 */
	if (len != sizeof(tmp)) {
		LOG_WRN("stored cfg is %u bytes, this image wants %u - ignored",
			(unsigned)len, (unsigned)sizeof(tmp));
		s_state.rejected = true;
		return 0;
	}

	n = read_cb(cb_arg, &tmp, sizeof(tmp));
	if (n != (ssize_t)sizeof(tmp)) {
		s_state.rejected = true;
		return 0;
	}
	if (tmp.ver != PMC_PERSIST_VER) {
		LOG_WRN("stored cfg is version %u, this image is %u - ignored",
			tmp.ver, PMC_PERSIST_VER);
		s_state.rejected = true;
		s_state.stored_ver = tmp.ver;
		return 0;
	}

	s_cfg = tmp;
	s_state.have_cfg = true;
	return 0;
}

static int out_set(const char *key, size_t len, settings_read_cb read_cb,
		   void *cb_arg)
{
	struct persist_out tmp;
	ssize_t n;

	if (settings_name_next(key, NULL) != 0) {
		return -ENOENT;
	}
	if (len != sizeof(tmp)) {
		LOG_WRN("stored outputs are %u bytes, this image wants %u - "
			"ignored", (unsigned)len, (unsigned)sizeof(tmp));
		s_state.rejected = true;
		return 0;
	}

	n = read_cb(cb_arg, &tmp, sizeof(tmp));
	if (n != (ssize_t)sizeof(tmp)) {
		s_state.rejected = true;
		return 0;
	}
	if (tmp.ver != PMC_PERSIST_VER) {
		LOG_WRN("stored outputs are version %u, this image is %u - "
			"ignored", tmp.ver, PMC_PERSIST_VER);
		s_state.rejected = true;
		s_state.stored_ver = tmp.ver;
		return 0;
	}

	s_out = tmp;
	s_state.have_out = true;
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(pmc_cfg, "pmc/cfg", NULL, cfg_set, NULL, NULL);
SETTINGS_STATIC_HANDLER_DEFINE(pmc_out, "pmc/out", NULL, out_set, NULL, NULL);

/* ------------------------------------------------------------- writing --- */

/*
 * The snapshot is taken HERE, when the timer fires, and not when a control was
 * touched. A caller that had to hand over the new value would be a caller that
 * could hand over a stale one, and there are nine of them.
 */
static void snapshot(struct persist_out *o, struct persist_cfg *c)
{
	struct pmc_485_cfg l;

	memset(o, 0, sizeof(*o));
	memset(c, 0, sizeof(*c));

	o->ver = PMC_PERSIST_VER;
	(void)pmc_do_read(o->do_out, PMC_DO_COUNT);
	(void)pmc_pdio_commanded(o->pdio_cmd, PMC_PDIO_COUNT);
	o->pulse_mask = pmc_io_pulse_mask();

	pmc_rs485_cfg_get(&l);
	c->ver        = PMC_PERSIST_VER;
	c->baud       = l.baud;
	c->data_bits  = l.data_bits;
	c->parity     = l.parity;
	c->stop_bits  = l.stop_bits;
	c->mode       = (uint8_t)l.mode;
	c->term       = l.term ? 1U : 0U;
	c->slew       = l.slew ? 1U : 0U;
	c->echo       = l.echo ? 1U : 0U;
	c->temp_mode  = (uint8_t)pmc_temp_mode_get();
	c->temp_wires = pmc_temp_wires_get();
}

static void save_work(struct k_work *work)
{
	struct persist_out o;
	struct persist_cfg c;
	int rc;

	ARG_UNUSED(work);

	k_mutex_lock(&s_lock, K_FOREVER);
	snapshot(&o, &c);

	/*
	 * THE OUTPUT RECORD IS NOT WRITTEN UNTIL THE RESTORE HAS HAPPENED, and
	 * this was a real defect rather than a precaution.
	 *
	 * pmc_persist_init() applies the stored configuration through the same
	 * setters a person uses - and every one of those setters calls
	 * pmc_persist_note(). So restoring the RS485 line settings scheduled a
	 * save for three seconds later, while interlock (0) still had every
	 * output low and the health gate was fifty-odd seconds away. The
	 * snapshot wrote all-off over the record it was about to restore from.
	 * Measured: DO7 stored on, board reset, record came back 00000000.
	 *
	 * Before the restore, the live outputs are not a statement of anyone's
	 * intent - they are the interlock's starting position. Saving them is
	 * saving the absence of the thing being remembered.
	 *
	 * The configuration record is unaffected and still saves: it was just
	 * applied from flash, so a snapshot of it is what is already stored and
	 * the memcmp below writes nothing.
	 */
	if (!s_state.restored) {
		LOG_DBG("outputs not saved yet: the stored set is not back");
	} else if (memcmp(&o, &s_out, sizeof(o)) != 0) {
		/*
		 * Written only when it differs from what is stored. The timer
		 * coalesces bursts; this catches a control being put back where
		 * it started, which is a flash write for no change at all.
		 */
		rc = settings_save_one("pmc/out", &o, sizeof(o));
		if (rc == 0) {
			s_out = o;
			s_state.have_out = true;
			atomic_inc(&s_saves);
		} else {
			LOG_ERR("could not store outputs: %d", rc);
		}
	}
	if (memcmp(&c, &s_cfg, sizeof(c)) != 0) {
		rc = settings_save_one("pmc/cfg", &c, sizeof(c));
		if (rc == 0) {
			s_cfg = c;
			s_state.have_cfg = true;
			atomic_inc(&s_saves);
		} else {
			LOG_ERR("could not store configuration: %d", rc);
		}
	}
	k_mutex_unlock(&s_lock);
}

void pmc_persist_note(void)
{
	/*
	 * Reschedule, so the hold restarts on every change: a burst of twelve
	 * writes one record after the burst, not twelve records.
	 */
	(void)k_work_reschedule(&save_timer, K_MSEC(PMC_PERSIST_HOLD_MS));
}

/* ----------------------------------------------------------- restoring --- */

int pmc_persist_init(void)
{
	char detail[80];
	int rc;

	k_mutex_init(&s_lock);

	/*
	 * Safe to call although wifi_credentials' settings backend already
	 * does: settings_subsys_init() guards itself and returns 0 on a second
	 * call. Called anyway rather than depending on WiFi being compiled in,
	 * because a build without the radio must still remember its outputs.
	 */
	rc = settings_subsys_init();
	if (rc != 0) {
		LOG_ERR("settings subsystem: %d - nothing will be remembered",
			rc);
		return rc;
	}

	rc = settings_load_subtree("pmc");
	if (rc != 0) {
		LOG_WRN("settings load: %d", rc);
	}

	if (!s_state.have_cfg) {
		LOG_INF("no stored configuration; defaults stand");
		return 0;
	}

	/*
	 * APPLIED THROUGH THE SAME SETTERS A PERSON USES, never by writing the
	 * driver's state directly. Each one validates, and a stored value that
	 * this image no longer accepts - a baud rate withdrawn, a mode renamed
	 * - must be refused exactly as a typed one would be, rather than
	 * installed because it came from flash.
	 */
	struct pmc_485_cfg l;

	pmc_rs485_cfg_get(&l);
	l.baud      = s_cfg.baud;
	l.data_bits = s_cfg.data_bits;
	l.parity    = s_cfg.parity;
	l.stop_bits = s_cfg.stop_bits;
	l.mode      = (enum pmc_485_mode)s_cfg.mode;
	l.term      = s_cfg.term != 0U;
	l.slew      = s_cfg.slew != 0U;
	l.echo      = s_cfg.echo != 0U;

	if (!pmc_rs485_cfg_set(&l, detail, sizeof(detail))) {
		LOG_WRN("stored RS485 settings refused: %s", detail);
	} else {
		LOG_INF("RS485 restored: %u %u%c%u", l.baud, l.data_bits,
			l.parity, l.stop_bits);
	}

	if (!pmc_temp_wires_set(s_cfg.temp_wires, detail, sizeof(detail))) {
		LOG_WRN("stored RTD wire count refused: %s", detail);
	}

	/* EXACTLY the spellings pmc_temp_mode_set() compares against - it is
	   strcmp, not a case-insensitive match, and "pt100" is refused. */
	static const char *const mode_name[] = { "PT100", "J", "K" };

	if (s_cfg.temp_mode < ARRAY_SIZE(mode_name)) {
		if (!pmc_temp_mode_set(mode_name[s_cfg.temp_mode], detail,
				       sizeof(detail))) {
			LOG_WRN("stored probe type refused: %s", detail);
		} else {
			LOG_INF("probe type restored: %s",
				mode_name[s_cfg.temp_mode]);
		}
	}

	return 0;
}

int pmc_persist_restore_outputs(void)
{
	char detail[96];
	int driven = 0;

	if (s_state.restored) {
		return 0;      /* idempotent - see persist.h */
	}
	s_state.restored = true;

	if (!s_state.have_out) {
		LOG_INF("no stored outputs; everything stays de-energized");
		return 0;
	}

	for (int n = 0; n < PMC_DO_COUNT; n++) {
		char chan[8];

		/* -1 is "never commanded", which is not a level to restore. */
		if (s_out.do_out[n] != 1) {
			continue;
		}
		(void)snprintk(chan, sizeof(chan), "DO%d", n);
		if (pmc_io_write(chan, true, detail, sizeof(detail)) ==
		    PMC_W_OK) {
			driven++;
		} else {
			LOG_WRN("%s not restored: %s", chan, detail);
		}
	}

	for (int n = 0; n < PMC_PDIO_COUNT; n++) {
		char chan[12];

		if (s_out.pdio_cmd[n] != 1) {
			continue;
		}
		(void)snprintk(chan, sizeof(chan), "PDIO%d", n);
		if (pmc_io_write(chan, true, detail, sizeof(detail)) ==
		    PMC_W_OK) {
			driven++;
		} else {
			LOG_WRN("%s not restored: %s", chan, detail);
		}
	}

	/*
	 * The pulse mask LAST, because arming a channel captures the level it
	 * is at - that is what disarming puts back - and the levels above have
	 * to be in place first or a disarm would return the channel to low.
	 */
	for (int n = 0; n < PMC_DO_COUNT; n++) {
		char chan[8];

		if ((s_out.pulse_mask & BIT(n)) == 0U) {
			continue;
		}
		(void)snprintk(chan, sizeof(chan), "DO%d", n);
		if (pmc_io_pulse(chan, true, detail, sizeof(detail)) !=
		    PMC_W_OK) {
			LOG_WRN("%s pulse not restored: %s", chan, detail);
		}
	}

	LOG_INF("outputs restored after the health gate: %d driven", driven);

	/*
	 * Capture what is actually set now, which is NOT necessarily what was
	 * stored. Two ways they differ: a channel that refused to come back,
	 * and a channel somebody set by hand during the minute before the gate
	 * passed - those writes could not be saved at the time, because saving
	 * before the restore is what the comment in save_work() describes.
	 * This is the first moment the live outputs mean anything.
	 */
	pmc_persist_note();
	return driven;
}

void pmc_persist_get(struct pmc_persist_state *out)
{
	if (out == NULL) {
		return;
	}
	*out = s_state;
	out->saves = (uint32_t)atomic_get(&s_saves);
}

/* ---------------------------------------------------------------- shell --- */

/*
 * `persist` - what is stored, and whether it has been put back.
 *
 * A feature that silently remembers is a feature nobody can check. This
 * prints the stored record rather than the live state, so "what will come
 * back at the next power-up" is answerable without power-cycling to find out.
 */
static int cmd_persist(const struct shell *sh, size_t argc, char **argv)
{
	struct pmc_persist_state st;
	char line[64];
	size_t n = 0;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	pmc_persist_get(&st);

	shell_print(sh, "stored configuration : %s",
		    st.have_cfg ? "yes" : "none - defaults stand");
	shell_print(sh, "stored outputs       : %s",
		    st.have_out ? "yes" : "none - everything stays off");
	shell_print(sh, "put back this boot   : %s",
		    st.restored ? "yes" :
		    "NOT YET - waiting for every health check to pass");
	if (st.rejected) {
		shell_warn(sh, "a stored record was REJECTED (version %u, this "
			       "image is %u)", st.stored_ver, PMC_PERSIST_VER);
	}
	shell_print(sh, "records written      : %u", st.saves);

	if (!st.have_out) {
		return 0;
	}

	k_mutex_lock(&s_lock, K_FOREVER);
	for (int i = 0; i < PMC_DO_COUNT; i++) {
		n += (size_t)snprintk(line + n, sizeof(line) - n, "%d",
				      s_out.do_out[i] == 1 ? 1 : 0);
	}
	shell_print(sh, "");
	shell_print(sh, "  DO  00..07   %s", line);

	n = 0;
	for (int i = 0; i < PMC_PDIO_COUNT; i++) {
		n += (size_t)snprintk(line + n, sizeof(line) - n, "%d",
				      s_out.pdio_cmd[i] == 1 ? 1 : 0);
	}
	shell_print(sh, "  PDIO 00..11  %s", line);
	shell_print(sh, "  pulse mask   0x%02x", (unsigned)s_out.pulse_mask);
	k_mutex_unlock(&s_lock);

	shell_print(sh, "");
	shell_print(sh, "Outputs are put back only after the health gate, never");
	shell_print(sh, "at boot: interlock (0) drives all eight low first. See");
	shell_print(sh, "docs/SAFETY.md.");
	return 0;
}

SHELL_CMD_REGISTER(persist, NULL,
		   "What survives a power cycle, and whether it is back yet.",
		   cmd_persist);
