/*
 * A minimal CANopen node. What it is and what it deliberately is not are in
 * canopen.h; this file is the implementation and the self-test.
 *
 * SPDX-License-Identifier: MIT
 * Written by Noah Christian (C) 2026
 */

#include "canopen.h"
#include "canbus.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(pmc_canopen, LOG_LEVEL_INF);

#if defined(CONFIG_PMC_CANOPEN)

/* ------------------------------------------------- the wire protocol --- */

/*
 * The four identifiers this node uses, from the CiA 301 predefined
 * connection set. All 11-bit, all with the node id in the low seven bits
 * except NMT, which is broadcast.
 */
#define CO_ID_NMT    0x000U   /* in:  [command, target node]              */
#define CO_ID_SDO_RX 0x600U   /* in:  + node, a request to this node      */
#define CO_ID_SDO_TX 0x580U   /* out: + node, this node's answer          */
#define CO_ID_HB     0x700U   /* out: + node, boot-up and heartbeat       */

/*
 * NMT COMMAND CODES, WHICH ARE NOT THE STATE CODES. The command to enter
 * operational is 0x01 and the heartbeat byte for operational is 0x05. Both
 * encodings appear in the same specification table and reusing one for the
 * other produces a node that looks right until a real master talks to it.
 * The state codes are in canopen.h; these are only ever compared against
 * byte 0 of an incoming NMT frame.
 */
#define CO_NMT_START      0x01U
#define CO_NMT_STOP       0x02U
#define CO_NMT_PREOP      0x80U
#define CO_NMT_RESET_NODE 0x81U
#define CO_NMT_RESET_COMM 0x82U

/* SDO client command specifiers, by their top three bits. */
#define CO_CCS_DOWNLOAD 0x20U   /* initiate download: the client writes */
#define CO_CCS_UPLOAD   0x40U   /* initiate upload:   the client reads  */

/* SDO server responses. */
#define CO_SCS_UPLOAD   0x40U   /* + the size bits, see upload_cmd()  */
#define CO_SCS_DOWNLOAD 0x60U
#define CO_SCS_ABORT    0x80U

/* The abort codes this node can produce, from CiA 301 table 22. */
#define CO_ABORT_CS        0x05040001UL /* command specifier not valid     */
#define CO_ABORT_READONLY  0x06010002UL /* write to a read-only object     */
#define CO_ABORT_NO_OBJECT 0x06020000UL /* the object does not exist       */
#define CO_ABORT_LENGTH    0x06070010UL /* data type or length mismatch    */
#define CO_ABORT_NO_SUB    0x06090011UL /* the sub-index does not exist    */

/*
 * The object dictionary, in full. Five entries, all mandatory in CiA 301,
 * and one of them writable.
 *
 * 0x1008 (manufacturer device name) IS ABSENT ON PURPOSE. It is a string,
 * strings longer than four bytes need a SEGMENTED transfer, and segmented
 * transfer is the first of the parts this node does not implement. An object
 * that exists but cannot be read is worse than one that is honestly missing:
 * a tool reading it would get an abort from the middle of a transfer rather
 * than "no such object", which is a harder thing to diagnose than an absence.
 */
#define CO_OD_DEVICE_TYPE  0x1000U
#define CO_OD_ERROR_REG    0x1001U
#define CO_OD_HB_TIME      0x1017U
#define CO_OD_IDENTITY     0x1018U

/*
 * DEVICE TYPE ZERO, WHICH IS A CLAIM AND NOT A PLACEHOLDER. 0x1000 carries
 * the CiA device profile a node conforms to - 401 for a generic I/O module,
 * which is what this board would be if anything here conformed to it. It does
 * not: there are no PDOs and no profile object dictionary. Zero means "no
 * standardized device profile", which is exactly true, and writing 0x191 here
 * would make a scanner report a DS-401 module that then answers none of the
 * profile's objects.
 */
#define CO_DEVICE_TYPE 0UL

/*
 * VENDOR ID ZERO, FOR THE SAME REASON. Vendor identifiers are assigned by
 * CiA and none has been assigned to this project. A made-up number is not a
 * harmless placeholder: the space is real and somebody owns whatever gets
 * invented.
 *
 * The product code is ASCII 'PMCE' so it is legible in a tool's hex view, and
 * it is vendor-specific - with vendor id zero it identifies this firmware on
 * this bench and claims nothing globally.
 */
#define CO_VENDOR_ID    0UL
#define CO_PRODUCT_CODE 0x504D4345UL  /* 'P' 'M' 'C' 'E' */

/* ------------------------------------------------------------ state --- */

static atomic_t co_node = (atomic_t)CONFIG_PMC_CANOPEN_NODE_ID;
static atomic_t co_state = (atomic_t)PMC_CO_PREOP;
static atomic_t co_hb_ms = (atomic_t)CONFIG_PMC_CANOPEN_HEARTBEAT_MS;

static atomic_t n_boots, n_hb, n_nmt, n_nmt_other;
static atomic_t n_sdo, n_sdo_ok, n_sdo_abort, n_dropped;

static uint32_t id_revision;
static uint32_t id_serial;
static bool     co_ready;

/*
 * The handler queue. Eight frames deep, which is a judgment about the gap
 * between an interrupt and a thread at priority 7 rather than about bus
 * traffic: a full queue means the node could not keep up, and that is
 * COUNTED and reported rather than waited on. A tap that blocks in interrupt
 * context would take the CAN driver down with it.
 */
K_MSGQ_DEFINE(co_q, sizeof(struct pmc_can_frame), 8, 4);

/*
 * NOT A CAN IDENTIFIER: 0xFFFFFFFF is past the 29-bit maximum, so no received
 * frame can ever carry it. A queue entry with this id is a POKE - it means
 * "something changed, recompute your deadline" and the handler ignores it.
 *
 * It exists because a timing change made from another thread is invisible to
 * one already blocked. THIS WAS MEASURED, and the first attempt at it was
 * worse than the bug: the thread re-read the period when its loop next went
 * round, which with a 1000 ms heartbeat could be a second away, so writing
 * 0x1017 = 200 moved the next heartbeat LATER rather than sooner. The
 * self-test caught that too. Polling on a short tick would also have worked
 * and would have cost a wakeup ten times a second forever to make a setting
 * that changes twice a day feel responsive.
 */
#define CO_POKE_ID 0xFFFFFFFFU

/*
 * Set by co_poke(), cleared by the thread. A FLAG AND NOT "the period
 * changed", because a reset restores the period to the value it already had
 * and still has to restart the schedule from the boot-up message - comparing
 * the period against the last one it scheduled would miss exactly that case.
 */
static atomic_t co_resched;

static void co_poke(void)
{
	struct pmc_can_frame poke = { .id = CO_POKE_ID };

	atomic_set(&co_resched, 1);
	/* A full queue needs no poke: the thread is about to run anyway. */
	(void)k_msgq_put(&co_q, &poke, K_NO_WAIT);
}

static K_THREAD_STACK_DEFINE(co_stack, 1536);
static struct k_thread co_tid;

/* ------------------------------------------------------- transmit --- */

static void co_tx(uint32_t id, const uint8_t *data, uint8_t len)
{
	char detail[80];

	/*
	 * The outcome is deliberately dropped here. On a bus with no second
	 * node nothing is ever acknowledged, so a heartbeat producer that
	 * checked its own sends would log a failure every period forever -
	 * and the CAN counters already report frames in flight and the
	 * controller's error state, which is the honest place for it.
	 */
	(void)pmc_can_send(id, false, data, len, detail, sizeof(detail));
}

static void co_send_hb(void)
{
	const uint8_t node = (uint8_t)atomic_get(&co_node);
	const uint8_t byte = (uint8_t)atomic_get(&co_state);

	co_tx(CO_ID_HB + node, &byte, 1);
	atomic_inc(&n_hb);
}

static void co_send_bootup(void)
{
	const uint8_t node = (uint8_t)atomic_get(&co_node);
	const uint8_t zero = PMC_CO_BOOTUP;

	/*
	 * The boot-up message IS a heartbeat frame with a zero payload, on the
	 * same identifier. That is what makes a node's appearance detectable
	 * by a tool that is only watching 0x700+n.
	 */
	co_tx(CO_ID_HB + node, &zero, 1);
	atomic_inc(&n_boots);
}

/* ------------------------------------------------------------ SDO --- */

static void sdo_abort(uint16_t idx, uint8_t sub, uint32_t code)
{
	const uint8_t node = (uint8_t)atomic_get(&co_node);
	uint8_t d[8];

	d[0] = CO_SCS_ABORT;
	sys_put_le16(idx, &d[1]);
	d[3] = sub;
	sys_put_le32(code, &d[4]);

	co_tx(CO_ID_SDO_TX + node, d, 8);
	atomic_inc(&n_sdo_abort);
}

/*
 * The command byte for an expedited upload response carries the number of
 * UNUSED bytes in bits 3..2, not the number of used ones. So four bytes is
 * 0x43 and one byte is 0x4F, which reads backwards and is a place to get it
 * wrong once.
 */
static uint8_t upload_cmd(uint8_t len)
{
	return (uint8_t)(CO_SCS_UPLOAD | 0x03U | ((4U - len) << 2));
}

/* Returns the width in bytes, or 0 for an object this node does not have. */
static uint8_t od_read(uint16_t idx, uint8_t sub, uint32_t *val, uint32_t *abort)
{
	*abort = CO_ABORT_NO_OBJECT;

	switch (idx) {
	case CO_OD_DEVICE_TYPE:
		if (sub != 0U) { *abort = CO_ABORT_NO_SUB; return 0; }
		*val = CO_DEVICE_TYPE;
		return 4;

	case CO_OD_ERROR_REG:
		if (sub != 0U) { *abort = CO_ABORT_NO_SUB; return 0; }
		/*
		 * Always zero, and that is not a stub: the error register's
		 * bit 0 means "a generic error is present", and this node has
		 * no error state to report through CANopen. Reporting a
		 * fabricated bit would be worse than reporting none.
		 */
		*val = 0;
		return 1;

	case CO_OD_HB_TIME:
		if (sub != 0U) { *abort = CO_ABORT_NO_SUB; return 0; }
		*val = (uint32_t)atomic_get(&co_hb_ms);
		return 2;

	case CO_OD_IDENTITY:
		switch (sub) {
		case 0: *val = 4;               return 1;
		case 1: *val = CO_VENDOR_ID;    return 4;
		case 2: *val = CO_PRODUCT_CODE; return 4;
		case 3: *val = id_revision;     return 4;
		case 4: *val = id_serial;       return 4;
		default: *abort = CO_ABORT_NO_SUB; return 0;
		}

	default:
		return 0;
	}
}

static void sdo_upload(uint16_t idx, uint8_t sub)
{
	const uint8_t node = (uint8_t)atomic_get(&co_node);
	uint32_t val = 0, abort = 0;
	const uint8_t len = od_read(idx, sub, &val, &abort);
	uint8_t d[8];

	if (len == 0U) {
		sdo_abort(idx, sub, abort);
		return;
	}

	memset(d, 0, sizeof(d));
	d[0] = upload_cmd(len);
	sys_put_le16(idx, &d[1]);
	d[3] = sub;
	sys_put_le32(val, &d[4]);   /* little endian, and only `len` of it is
				       meaningful - the rest is padding the
				       size bits above already accounted for */

	co_tx(CO_ID_SDO_TX + node, d, 8);
	atomic_inc(&n_sdo_ok);
}

static void sdo_download(uint16_t idx, uint8_t sub, const uint8_t *d, uint8_t dlc)
{
	const uint8_t node = (uint8_t)atomic_get(&co_node);
	uint8_t resp[8];
	uint32_t val;

	/*
	 * ONE WRITABLE OBJECT: the producer heartbeat time. Everything else in
	 * the dictionary is a constant, and a node that accepts a write it
	 * then ignores is the worst of the three possible behaviors.
	 */
	if (idx != CO_OD_HB_TIME) {
		uint32_t throwaway = 0, abort = 0;

		/* Does it exist at all? "Read-only" and "no such object" are
		   different answers and a client acts differently on each. */
		sdo_abort(idx, sub,
			  od_read(idx, sub, &throwaway, &abort) != 0U
				  ? CO_ABORT_READONLY : abort);
		return;
	}
	if (sub != 0U) {
		sdo_abort(idx, sub, CO_ABORT_NO_SUB);
		return;
	}
	if (dlc < 6U) {
		sdo_abort(idx, sub, CO_ABORT_LENGTH);
		return;
	}

	/*
	 * EXPEDITED ONLY. Bit 1 of the command byte is the expedited flag; a
	 * client that opens a segmented download of a two-byte object is
	 * doing something legal that this node cannot follow, and saying so
	 * is better than taking the first four bytes and hoping.
	 */
	if ((d[0] & 0x02U) == 0U) {
		sdo_abort(idx, sub, CO_ABORT_CS);
		return;
	}

	/* UNSIGNED16, so the two bytes the client sent are the whole value and
	   there is no range left to check. */
	val = sys_get_le16(&d[4]);
	atomic_set(&co_hb_ms, (atomic_val_t)val);

	memset(resp, 0, sizeof(resp));
	resp[0] = CO_SCS_DOWNLOAD;
	sys_put_le16(idx, &resp[1]);
	resp[3] = sub;
	co_tx(CO_ID_SDO_TX + node, resp, 8);
	atomic_inc(&n_sdo_ok);

	LOG_INF("heartbeat time set to %u ms over SDO", (unsigned)val);
}

static void handle_sdo(const struct pmc_can_frame *f)
{
	uint16_t idx;
	uint8_t sub, ccs;

	atomic_inc(&n_sdo);

	/*
	 * SDO IS NOT AVAILABLE IN THE STOPPED STATE, per CiA 301: a stopped
	 * node answers NMT and produces its heartbeat, and nothing else. So
	 * this returns without a reply rather than aborting - an abort would
	 * be a reply, and the whole point of the state is that there is none.
	 *
	 * It counts as a request received and as neither an answer nor an
	 * abort, which is why `sdo_rx` minus the two is a meaningful number:
	 * requests refused by the state machine. Same shape as the CAN
	 * counters, where queued minus the outcomes is what is still in
	 * flight.
	 */
	if ((uint8_t)atomic_get(&co_state) == PMC_CO_STOPPED) {
		return;
	}

	if (f->dlc < 4U) {
		/* Too short to carry an index, so there is nothing to name in
		   an abort. Dropped, and counted as a refused request. */
		return;
	}

	ccs = f->data[0] & 0xE0U;
	idx = sys_get_le16(&f->data[1]);
	sub = f->data[3];

	if (ccs == CO_CCS_UPLOAD) {
		sdo_upload(idx, sub);
	} else if (ccs == CO_CCS_DOWNLOAD) {
		sdo_download(idx, sub, f->data, f->dlc);
	} else {
		/* Segmented transfer, block transfer and abort-from-client all
		   land here. Named by code rather than ignored, so a client
		   gets a diagnosis instead of a timeout. */
		sdo_abort(idx, sub, CO_ABORT_CS);
	}
}

/* ------------------------------------------------------------ NMT --- */

static bool apply_nmt(uint8_t cmd)
{
	switch (cmd) {
	case CO_NMT_START:
		atomic_set(&co_state, (atomic_val_t)PMC_CO_OPERATIONAL);
		return true;
	case CO_NMT_STOP:
		atomic_set(&co_state, (atomic_val_t)PMC_CO_STOPPED);
		return true;
	case CO_NMT_PREOP:
		atomic_set(&co_state, (atomic_val_t)PMC_CO_PREOP);
		return true;
	case CO_NMT_RESET_NODE:
	case CO_NMT_RESET_COMM:
		/*
		 * THE TWO RESETS DO THE SAME THING HERE, AND THAT IS WORTH
		 * SAYING RATHER THAN HIDING. Reset communication restores the
		 * communication profile objects, which for this node is the
		 * heartbeat time and nothing else. Reset node additionally
		 * restores the manufacturer and profile areas, and this node
		 * has neither. So the two are identical until an application
		 * object exists, and pretending to distinguish them would be
		 * a distinction with nothing behind it.
		 */
		atomic_set(&co_hb_ms,
			   (atomic_val_t)CONFIG_PMC_CANOPEN_HEARTBEAT_MS);
		atomic_set(&co_state, (atomic_val_t)PMC_CO_PREOP);
		co_send_bootup();
		/*
		 * The heartbeat schedule restarts from the boot-up message,
		 * which is what a master watching 0x700+n expects: boot-up
		 * then one period, not boot-up then whatever was left of the
		 * previous period.
		 */
		co_poke();
		return true;
	default:
		return false;
	}
}

static void handle_nmt(const struct pmc_can_frame *f)
{
	const uint8_t node = (uint8_t)atomic_get(&co_node);

	if (f->dlc < 2U) {
		return;
	}
	/* Node 0 addresses every node on the bus, which is how a master
	   starts a whole network with one frame. */
	if (f->data[1] != 0U && f->data[1] != node) {
		atomic_inc(&n_nmt_other);
		return;
	}
	if (apply_nmt(f->data[0])) {
		atomic_inc(&n_nmt);
	}
}

/* ------------------------------------------------- the receive tap --- */

static void co_tap(const struct pmc_can_frame *f)
{
	/*
	 * INTERRUPT CONTEXT. Copy and return; see canbus.h. A full queue is
	 * counted here and reported by the status line, because the
	 * alternative - blocking - would stall the CAN driver's callback.
	 */
	/*
	 * 11-bit data frames only. The predefined connection set is entirely
	 * base-format, and a remote frame carries no data to act on - CANopen
	 * uses RTR only for an optional PDO request this node does not have.
	 */
	if (f->ext || f->rtr) {
		return;
	}
	if (k_msgq_put(&co_q, f, K_NO_WAIT) != 0) {
		atomic_inc(&n_dropped);
	}
}

/* ---------------------------------------------------- the thread --- */

static void co_thread_fn(void *a, void *b, void *c)
{
	int64_t next_hb;

	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	/*
	 * The boot-up message, which is the first thing a node must send and
	 * the thing a scanner is waiting for. Sent from the thread rather
	 * than from init so the only code that transmits on this node's
	 * identifiers is this thread.
	 */
	co_send_bootup();
	next_hb = k_uptime_get() + (int64_t)atomic_get(&co_hb_ms);

	for (;;) {
		struct pmc_can_frame f;
		const uint16_t hb = (uint16_t)atomic_get(&co_hb_ms);
		int64_t left;

		/*
		 * A NEW PERIOD RESCHEDULES THE PENDING HEARTBEAT, rather than
		 * taking effect only after the one already due.
		 *
		 * Measured, because this was wrong first: with the period at
		 * 1000 ms and a master writing 0x1017 = 200, the next
		 * heartbeat still came up to a second later and only then
		 * settled into the new cadence. Nothing about that is
		 * detectable from the bus except as a node that ignored the
		 * write for one period - which is exactly how a flaky CANopen
		 * device looks. The self-test caught it by timing three
		 * heartbeats instead of trusting the setter's return.
		 */
		if (atomic_cas(&co_resched, 1, 0)) {
			next_hb = k_uptime_get() + hb;
		}

		left = hb ? (next_hb - k_uptime_get()) : 250;
		if (left < 0) {
			left = 0;
		}

		/*
		 * ONE WAIT FOR BOTH JOBS: the next frame or the next
		 * heartbeat, whichever comes first. With the heartbeat off,
		 * the 250 ms above is an idle tick rather than K_FOREVER -
		 * forever would mean a `canopen hb 500` from the shell did
		 * nothing until a frame happened to arrive, and a setting
		 * that takes effect on the next unrelated event is the kind
		 * of behavior that gets diagnosed as a dead node.
		 */
		if (k_msgq_get(&co_q, &f, K_MSEC(left)) == 0) {
			const uint8_t node = (uint8_t)atomic_get(&co_node);

			if (f.id == CO_POKE_ID) {
				/* Nothing to handle; the point was to get here
				   and recompute the deadline above. */
			} else if (f.id == CO_ID_NMT) {
				handle_nmt(&f);
			} else if (f.id == (uint32_t)(CO_ID_SDO_RX + node)) {
				handle_sdo(&f);
			}
			/*
			 * Everything else is somebody else's traffic - and in
			 * loopback that includes this node's own heartbeats
			 * and SDO answers coming straight back. Not counted:
			 * the CAN layer's rx counter already counts every
			 * frame, and a second count of "frames that were not
			 * for me" invites reading it as an error.
			 */
			continue;
		}

		if (hb != 0U && k_uptime_get() >= next_hb) {
			co_send_hb();
			next_hb = k_uptime_get() + hb;
		}
	}
}

/* ------------------------------------------------------------ API --- */

static void read_identity(void)
{
	uint8_t uid[16];
	ssize_t n;

	/*
	 * 0x1018:03, the revision number, carries the RELEASE and not the
	 * image: maj.min.rev fits a U32 exactly and the +build flash counter
	 * does not fit at all. So two flashes of one release are identical
	 * here, which is the thing docs/CONFIG.md says a version on a panel
	 * must never do - and the answer is that this is not the panel.
	 * `whoami` identifies the image; this field identifies the product
	 * revision, which is what a CANopen client asks it for.
	 */
#if PARTITION_EXISTS(slot0_partition)
	struct mcuboot_img_header hdr;

	if (boot_read_bank_header(PARTITION_ID(slot0_partition), &hdr,
				  sizeof(hdr)) == 0) {
		id_revision = ((uint32_t)hdr.h.v1.sem_ver.major << 24) |
			      ((uint32_t)hdr.h.v1.sem_ver.minor << 16) |
			      (uint32_t)hdr.h.v1.sem_ver.revision;
	}
#endif

	/*
	 * 0x1018:04, the serial number, from the SoC's own 96-bit unique id
	 * XOR-FOLDED to the 32 bits the object holds. A fold, not a
	 * truncation, so all three words contribute - and it is one-way, so
	 * this does not publish the UID itself on the bus. Two boards could
	 * in principle fold to the same value; for a bench with one board
	 * that is a statement for completeness rather than a risk.
	 */
	n = hwinfo_get_device_id(uid, sizeof(uid));
	if (n > 0) {
		for (ssize_t i = 0; i < n; i++) {
			const unsigned shift = (unsigned)(i % 4) * 8U;

			id_serial ^= (uint32_t)uid[i] << shift;
		}
	}
}

int pmc_canopen_init(void)
{
	read_identity();

	pmc_can_rx_tap_set(co_tap);

	k_thread_create(&co_tid, co_stack, K_THREAD_STACK_SIZEOF(co_stack),
			co_thread_fn, NULL, NULL, NULL, 7, 0, K_NO_WAIT);
	k_thread_name_set(&co_tid, "pmc_canopen");

	co_ready = true;
	LOG_INF("CANopen node %u, heartbeat %u ms, pre-operational",
		(unsigned)atomic_get(&co_node),
		(unsigned)atomic_get(&co_hb_ms));
	return 0;
}

void pmc_canopen_state_read(struct pmc_co_state_read *out)
{
	if (out == NULL) {
		return;
	}
	memset(out, 0, sizeof(*out));

	out->enabled = co_ready;
	out->node = (uint8_t)atomic_get(&co_node);
	out->state = (uint8_t)atomic_get(&co_state);
	out->hb_ms = (uint16_t)atomic_get(&co_hb_ms);

	out->boots = (uint32_t)atomic_get(&n_boots);
	out->hb_sent = (uint32_t)atomic_get(&n_hb);
	out->nmt_rx = (uint32_t)atomic_get(&n_nmt);
	out->nmt_ignored = (uint32_t)atomic_get(&n_nmt_other);
	out->sdo_rx = (uint32_t)atomic_get(&n_sdo);
	out->sdo_ok = (uint32_t)atomic_get(&n_sdo_ok);
	out->sdo_abort = (uint32_t)atomic_get(&n_sdo_abort);
	out->dropped = (uint32_t)atomic_get(&n_dropped);

	out->vendor = CO_VENDOR_ID;
	out->product = CO_PRODUCT_CODE;
	out->revision = id_revision;
	out->serial = id_serial;
}

bool pmc_canopen_nmt_local(uint8_t cmd, char *detail, size_t cap)
{
	if (!apply_nmt(cmd)) {
		(void)snprintk(detail, cap,
			       "refused: 0x%02x is not an NMT command",
			       (unsigned)cmd);
		return false;
	}
	(void)snprintk(detail, cap, "state is now 0x%02x",
		       (unsigned)atomic_get(&co_state));
	return true;
}

bool pmc_canopen_node_set(uint8_t node, char *detail, size_t cap)
{
	if (node < 1U || node > 127U) {
		(void)snprintk(detail, cap,
			       "refused: a CANopen node id is 1..127, not %u",
			       (unsigned)node);
		return false;
	}
	atomic_set(&co_node, (atomic_val_t)node);
	(void)snprintk(detail, cap,
		       "node %u: heartbeat 0x%x, SDO in 0x%x out 0x%x",
		       (unsigned)node, CO_ID_HB + node,
		       CO_ID_SDO_RX + node, CO_ID_SDO_TX + node);
	return true;
}

bool pmc_canopen_hb_set(uint16_t ms, char *detail, size_t cap)
{
	if (ms != 0U && ms < 20U) {
		(void)snprintk(detail, cap,
			       "refused: %u ms is faster than this is for - "
			       "20 ms minimum", (unsigned)ms);
		return false;
	}
	atomic_set(&co_hb_ms, (atomic_val_t)ms);
	/*
	 * TAKES EFFECT NOW, not after the period already running. Without this
	 * poke a write of 0x1017 from a master, or `canopen hb` from the
	 * shell, would be invisible until the pending heartbeat fell due -
	 * which is up to a whole old period of looking like a node that
	 * ignored the write.
	 */
	co_poke();
	(void)snprintk(detail, cap, "%s",
		       ms == 0U ? "heartbeat off - a scanner will not see this "
				  "node until it is asked something"
				: "heartbeat set");
	return true;
}

/* ------------------------------------------------------ the shell --- */

static const char *state_name(uint8_t s)
{
	switch (s) {
	case PMC_CO_BOOTUP:      return "boot-up";
	case PMC_CO_STOPPED:     return "stopped";
	case PMC_CO_OPERATIONAL: return "operational";
	case PMC_CO_PREOP:       return "pre-operational";
	default:                 return "?";
	}
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct pmc_co_state_read s;
	struct pmc_can_state can;

	ARG_UNUSED(argc); ARG_UNUSED(argv);

	pmc_canopen_state_read(&s);
	pmc_can_state_read(&can);

	shell_print(sh, "node       %u   state %s (0x%02x)   heartbeat %u ms",
		    s.node, state_name(s.state), s.state, s.hb_ms);
	shell_print(sh, "ids        heartbeat 0x%x, SDO in 0x%x, out 0x%x, "
			"NMT 0x000",
		    CO_ID_HB + s.node, CO_ID_SDO_RX + s.node,
		    CO_ID_SDO_TX + s.node);
	shell_print(sh, "identity   vendor 0x%08x  product 0x%08x  "
			"rev 0x%08x  serial 0x%08x",
		    s.vendor, s.product, s.revision, s.serial);
	shell_print(sh, "sent       %u boot-up, %u heartbeat", s.boots,
		    s.hb_sent);
	shell_print(sh, "nmt        %u accepted, %u for another node",
		    s.nmt_rx, s.nmt_ignored);
	shell_print(sh, "sdo        %u in, %u answered, %u aborted, "
			"%u refused (stopped, or too short to name an object)",
		    s.sdo_rx, s.sdo_ok, s.sdo_abort,
		    s.sdo_rx - s.sdo_ok - s.sdo_abort);
	if (s.dropped != 0U) {
		shell_warn(sh, "dropped    %u frame(s): the handler queue was "
			       "full", s.dropped);
	}
	shell_print(sh, "bus        %s, %u bit/s, %s",
		    can.started ? "started" : "STOPPED", can.bitrate,
		    can.loopback ? "INTERNAL loopback - nothing reaches the "
				   "transceiver"
				 : "normal - a frame needs a second node to "
				   "acknowledge it");
	return 0;
}

static int nmt_cmd(const struct shell *sh, uint8_t cmd)
{
	char detail[80];

	if (pmc_canopen_nmt_local(cmd, detail, sizeof(detail))) {
		shell_print(sh, "%s", state_name(
				    (uint8_t)atomic_get(&co_state)));
		return 0;
	}
	shell_error(sh, "%s", detail);
	return -EINVAL;
}

static int cmd_start(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	return nmt_cmd(sh, CO_NMT_START);
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	return nmt_cmd(sh, CO_NMT_STOP);
}

static int cmd_preop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	return nmt_cmd(sh, CO_NMT_PREOP);
}

static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	return nmt_cmd(sh, CO_NMT_RESET_NODE);
}

static int cmd_node(const struct shell *sh, size_t argc, char **argv)
{
	char detail[96];
	const long v = strtol(argv[1], NULL, 0);

	ARG_UNUSED(argc);

	if (!pmc_canopen_node_set((uint8_t)v, detail, sizeof(detail))) {
		shell_error(sh, "%s", detail);
		return -EINVAL;
	}
	shell_print(sh, "%s", detail);
	return 0;
}

static int cmd_hb(const struct shell *sh, size_t argc, char **argv)
{
	char detail[96];
	const long v = strtol(argv[1], NULL, 0);

	ARG_UNUSED(argc);

	if (v < 0 || v > UINT16_MAX) {
		shell_error(sh, "refused: 0..65535 ms");
		return -EINVAL;
	}
	if (!pmc_canopen_hb_set((uint16_t)v, detail, sizeof(detail))) {
		shell_error(sh, "%s", detail);
		return -EINVAL;
	}
	shell_print(sh, "%s", detail);
	return 0;
}

/* ------------------------------------------------- the self-test --- */

/*
 * WHAT THIS PROVES AND WHAT IT CANNOT.
 *
 * It runs the node against itself through INTERNAL loopback, where a
 * transmitted frame is delivered to the receive path without reaching the TX
 * pin - measured, see docs/MEASURED.md. So this board is both the node and
 * the tool, and the test exercises the NMT decode, all four state
 * transitions, the heartbeat cadence, the object dictionary, the expedited
 * SDO server in both directions and the abort path.
 *
 * It proves nothing about the transceiver, the wiring, the termination, the
 * bit timing against another node's, or the receive path off a real wire.
 * That is in the TBD list and this command prints the limitation in its own
 * header rather than leaving it to be read somewhere else.
 *
 * It is also the only test here that can run with no hardware attached, which
 * is the reason it exists: a self-test nobody can run is a self-test that
 * rots.
 */
#define ST_SCRATCH 24
static struct pmc_can_frame st_buf[ST_SCRATCH];
static atomic_t st_busy;

static int st_pass, st_fail;

static void st_result(const struct shell *sh, const char *what, bool ok,
		      const char *fmt, ...)
{
	va_list ap;
	char detail[128];

	va_start(ap, fmt);
	(void)vsnprintk(detail, sizeof(detail), fmt, ap);
	va_end(ap);

	if (ok) {
		st_pass++;
		shell_print(sh, "  %-26s PASS  %s", what, detail);
	} else {
		st_fail++;
		shell_error(sh, "  %-26s FAIL  %s", what, detail);
	}
}

/*
 * Wait for the next frame with this identifier, starting from `cur`, and
 * advance `cur` past it.
 *
 * READING FORWARD FROM A CURSOR rather than scanning the whole window is what
 * makes each step independent of the ones before it: a heartbeat from step 2
 * cannot satisfy step 4. The cursor is the CAN layer's own frame index, so
 * there is no second counter to keep in step.
 */
static bool st_wait(uint64_t *cur, uint32_t id, uint32_t ms,
		    struct pmc_can_frame *out)
{
	const int64_t deadline = k_uptime_get() + ms;

	do {
		uint64_t next = *cur;
		bool lost = false;
		const size_t n = pmc_can_rx_read(*cur, st_buf, ST_SCRATCH,
						 &next, &lost);

		/*
		 * `next - n` and not `*cur`: when the window has already
		 * overwritten the frames asked for, the reader clamps `from`
		 * forward and `lost` says so, and index arithmetic from the
		 * old cursor would then point at the wrong frames. This is
		 * correct in both cases, which is why it is written this way
		 * rather than the obvious way.
		 */
		for (size_t i = 0; i < n; i++) {
			if (st_buf[i].id == id) {
				*cur = next - n + i + 1;
				if (out != NULL) {
					*out = st_buf[i];
				}
				return true;
			}
		}
		*cur = next;
		k_sleep(K_MSEC(10));
	} while (k_uptime_get() < deadline);

	return false;
}

static void st_sdo_req(uint8_t node, uint8_t cmd, uint16_t idx, uint8_t sub,
		       uint32_t val)
{
	char detail[80];
	uint8_t d[8];

	memset(d, 0, sizeof(d));
	d[0] = cmd;
	sys_put_le16(idx, &d[1]);
	d[3] = sub;
	sys_put_le32(val, &d[4]);
	(void)pmc_can_send(CO_ID_SDO_RX + node, false, d, 8, detail,
			   sizeof(detail));
}

static void st_nmt(uint8_t cmd, uint8_t node)
{
	char detail[80];
	const uint8_t d[2] = { cmd, node };

	(void)pmc_can_send(CO_ID_NMT, false, d, 2, detail, sizeof(detail));
}

/* One NMT command, then the next heartbeat must carry `want`. */
static void st_nmt_step(const struct shell *sh, uint64_t *cur, uint8_t node,
			uint8_t cmd, uint8_t want, const char *what)
{
	struct pmc_can_frame f;

	st_nmt(cmd, node);
	/*
	 * Two heartbeat periods of patience, because the command may arrive
	 * just after one was sent. A single period would make this test fail
	 * about as often as the scheduler felt like it, and a flaky test is
	 * worse than no test - it teaches people to re-run it.
	 */
	if (!st_wait(cur, CO_ID_HB + node, 600, &f)) {
		st_result(sh, what, false, "no heartbeat within 600 ms");
		return;
	}
	st_result(sh, what, f.dlc == 1U && f.data[0] == want,
		  "heartbeat 0x%02x, wanted 0x%02x", f.data[0], want);
}

static int cmd_selftest(const struct shell *sh, size_t argc, char **argv)
{
	char detail[96];
	struct pmc_can_state can0;
	struct pmc_can_frame f;
	uint64_t cur;
	uint8_t node;
	uint16_t hb0;
	int64_t t[4];
	int nhb = 0;

	ARG_UNUSED(argc); ARG_UNUSED(argv);

	if (!atomic_cas(&st_busy, 0, 1)) {
		shell_error(sh, "a self-test is already running");
		return -EBUSY;
	}

	st_pass = 0;
	st_fail = 0;
	node = (uint8_t)atomic_get(&co_node);
	hb0 = (uint16_t)atomic_get(&co_hb_ms);
	pmc_can_state_read(&can0);

	if (!can0.present || !can0.started) {
		shell_error(sh, "the CAN controller is not started");
		atomic_set(&st_busy, 0);
		return -ENODEV;
	}

	shell_print(sh, "");
	shell_print(sh, "canopen selftest - node %u, INTERNAL loopback.", node);
	shell_print(sh, "Nothing past the MCU is tested: no transceiver, no "
			"wire, no second node.");
	shell_print(sh, "");

	/* 0. Loopback, so the node's own frames come back to it. */
	if (!can0.loopback &&
	    !pmc_can_set_loopback(true, detail, sizeof(detail))) {
		shell_error(sh, "could not enter loopback: %s", detail);
		atomic_set(&st_busy, 0);
		return -EIO;
	}

	/* 1. Reset, which must produce a boot-up message. */
	cur = pmc_can_rx_cursor();
	st_nmt(CO_NMT_RESET_NODE, node);
	if (st_wait(&cur, CO_ID_HB + node, 400, &f)) {
		st_result(sh, "boot-up on reset",
			  f.dlc == 1U && f.data[0] == PMC_CO_BOOTUP,
			  "0x%x  %u byte, 0x%02x", (unsigned)f.id, f.dlc,
			  f.data[0]);
	} else {
		st_result(sh, "boot-up on reset", false,
			  "nothing on 0x%x within 400 ms",
			  (unsigned)(CO_ID_HB + node));
	}

	/* 2. Heartbeat cadence, at a period chosen to make the test short. */
	(void)pmc_canopen_hb_set(200, detail, sizeof(detail));
	while (nhb < 3 && st_wait(&cur, CO_ID_HB + node, 500, &f)) {
		t[nhb++] = f.stamp;
	}
	if (nhb < 3) {
		st_result(sh, "heartbeat cadence", false,
			  "only %d heartbeat(s) in the window", nhb);
	} else {
		const int64_t d1 = t[1] - t[0];
		const int64_t d2 = t[2] - t[1];
		const bool ok = d1 > 150 && d1 < 250 && d2 > 150 && d2 < 250 &&
				f.data[0] == PMC_CO_PREOP;

		st_result(sh, "heartbeat cadence", ok,
			  "%ld and %ld ms apart at 200 ms, state 0x%02x",
			  (long)d1, (long)d2, f.data[0]);
	}

	/* 3-5. Every NMT transition, each confirmed by the next heartbeat. */
	st_nmt_step(sh, &cur, node, CO_NMT_START, PMC_CO_OPERATIONAL,
		    "nmt start");
	st_nmt_step(sh, &cur, node, CO_NMT_PREOP, PMC_CO_PREOP,
		    "nmt pre-operational");
	st_nmt_step(sh, &cur, node, CO_NMT_STOP, PMC_CO_STOPPED, "nmt stop");

	/* 6. SDO must be refused in the stopped state, with no answer at all. */
	cur = pmc_can_rx_cursor();
	st_sdo_req(node, 0x40, CO_OD_IDENTITY, 1, 0);
	st_result(sh, "sdo silent when stopped",
		  !st_wait(&cur, CO_ID_SDO_TX + node, 200, NULL),
		  "no answer on 0x%x in 200 ms",
		  (unsigned)(CO_ID_SDO_TX + node));

	/* 7. Back to pre-operational, then read the identity object. */
	st_nmt(CO_NMT_PREOP, node);
	k_sleep(K_MSEC(30));
	cur = pmc_can_rx_cursor();
	st_sdo_req(node, 0x40, CO_OD_IDENTITY, 2, 0);
	if (st_wait(&cur, CO_ID_SDO_TX + node, 300, &f)) {
		const uint32_t got = sys_get_le32(&f.data[4]);

		st_result(sh, "sdo read 1018:02",
			  f.data[0] == 0x43U && got == CO_PRODUCT_CODE,
			  "cmd 0x%02x, 0x%08x ('%c%c%c%c')", f.data[0], got,
			  f.data[7], f.data[6], f.data[5], f.data[4]);
	} else {
		st_result(sh, "sdo read 1018:02", false, "no answer in 300 ms");
	}

	/* 8. An object that does not exist must abort, with the right code. */
	cur = pmc_can_rx_cursor();
	st_sdo_req(node, 0x40, 0x5555, 0, 0);
	if (st_wait(&cur, CO_ID_SDO_TX + node, 300, &f)) {
		const uint32_t code = sys_get_le32(&f.data[4]);

		st_result(sh, "sdo abort on 5555:00",
			  f.data[0] == CO_SCS_ABORT &&
				  code == CO_ABORT_NO_OBJECT,
			  "cmd 0x%02x, abort 0x%08x", f.data[0], code);
	} else {
		st_result(sh, "sdo abort on 5555:00", false,
			  "no answer in 300 ms");
	}

	/* 9. Write the heartbeat time, then read back what was written. */
	cur = pmc_can_rx_cursor();
	st_sdo_req(node, 0x2B, CO_OD_HB_TIME, 0, 400);
	if (st_wait(&cur, CO_ID_SDO_TX + node, 300, &f) &&
	    f.data[0] == CO_SCS_DOWNLOAD) {
		cur = pmc_can_rx_cursor();
		st_sdo_req(node, 0x40, CO_OD_HB_TIME, 0, 0);
		if (st_wait(&cur, CO_ID_SDO_TX + node, 300, &f)) {
			const uint32_t got = sys_get_le16(&f.data[4]);

			st_result(sh, "sdo write 1017:00 = 400",
				  f.data[0] == 0x4BU && got == 400U,
				  "read back %u ms, cmd 0x%02x", got,
				  f.data[0]);
		} else {
			st_result(sh, "sdo write 1017:00 = 400", false,
				  "the write was accepted and the read back "
				  "was not answered");
		}
	} else {
		st_result(sh, "sdo write 1017:00 = 400", false,
			  "the write was not accepted");
	}

	/*
	 * 10. Put everything back, and assert that too. A test that leaves
	 * the node in its test configuration has broken the thing it was
	 * measuring - the heartbeat period, the NMT state and the controller's
	 * mode all have to go back to what they were.
	 */
	(void)pmc_canopen_hb_set(hb0, detail, sizeof(detail));
	st_nmt(CO_NMT_PREOP, node);
	k_sleep(K_MSEC(30));
	if (!can0.loopback) {
		(void)pmc_can_set_loopback(false, detail, sizeof(detail));
	}
	{
		struct pmc_can_state now;

		pmc_can_state_read(&now);
		st_result(sh, "restored",
			  (uint16_t)atomic_get(&co_hb_ms) == hb0 &&
				  (uint8_t)atomic_get(&co_state) ==
					  PMC_CO_PREOP &&
				  now.loopback == can0.loopback,
			  "%s, heartbeat %u ms, loopback %s",
			  state_name((uint8_t)atomic_get(&co_state)),
			  (unsigned)atomic_get(&co_hb_ms),
			  now.loopback ? "on" : "off");
	}

	shell_print(sh, "");
	if (st_fail == 0) {
		shell_print(sh, "  %d passed. The controller, the filter, the "
				"state machine and the SDO", st_pass);
		shell_print(sh, "  server all work. The bus is still "
				"unmeasured - see docs/MEASURED.md.");
	} else {
		shell_error(sh, "  %d passed, %d FAILED", st_pass, st_fail);
	}
	shell_print(sh, "");

	atomic_set(&st_busy, 0);
	return st_fail == 0 ? 0 : -EIO;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_canopen,
	SHELL_CMD(status, NULL, "ids, identity, counters and the bus mode",
		  cmd_status),
	SHELL_CMD(start, NULL, "NMT start: enter operational", cmd_start),
	SHELL_CMD(stop, NULL, "NMT stop: answer NMT and heartbeat only",
		  cmd_stop),
	SHELL_CMD(preop, NULL, "NMT enter pre-operational", cmd_preop),
	SHELL_CMD(reset, NULL, "NMT reset node: boot-up again", cmd_reset),
	SHELL_CMD_ARG(node, NULL, "node <1-127> - change the node id",
		      cmd_node, 2, 0),
	SHELL_CMD_ARG(hb, NULL, "hb <ms> - producer heartbeat, 0 to stop",
		      cmd_hb, 2, 0),
	SHELL_CMD(selftest, NULL,
		  "run the node against itself through internal loopback",
		  cmd_selftest),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(canopen, &sub_canopen,
		   "A minimal CANopen node: boot-up, heartbeat, NMT and an "
		   "expedited SDO server.", cmd_status);

#else /* CONFIG_PMC_CANOPEN */

int pmc_canopen_init(void) { return -ENOTSUP; }

void pmc_canopen_state_read(struct pmc_co_state_read *out)
{
	if (out != NULL) {
		memset(out, 0, sizeof(*out));
	}
}

bool pmc_canopen_nmt_local(uint8_t cmd, char *detail, size_t cap)
{
	ARG_UNUSED(cmd);
	(void)snprintk(detail, cap, "refused: no CANopen in this build");
	return false;
}

bool pmc_canopen_node_set(uint8_t node, char *detail, size_t cap)
{
	ARG_UNUSED(node);
	(void)snprintk(detail, cap, "refused: no CANopen in this build");
	return false;
}

bool pmc_canopen_hb_set(uint16_t ms, char *detail, size_t cap)
{
	ARG_UNUSED(ms);
	(void)snprintk(detail, cap, "refused: no CANopen in this build");
	return false;
}

#endif /* CONFIG_PMC_CANOPEN */
