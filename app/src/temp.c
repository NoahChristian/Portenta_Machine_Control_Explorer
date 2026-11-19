/*
 * Temperature: three channels, two converters, one bus.
 *
 * THE ONE THING THAT MAKES THIS FAMILY DANGEROUS is that a wrong answer here
 * is a plausible number rather than an error. Every other family on this board
 * fails loudly: an expander that does not answer returns -EIO, an output with
 * no supply reads back low. A temperature channel read too soon after the mux
 * moved returns a perfectly well-formed temperature - of the channel you were
 * looking at before. Nothing in the data distinguishes it. So the dwell below
 * is not a tuning parameter, it is the correctness argument.
 *
 * Datasheet 3-8 and 3-9, which are worth having in front of you:
 *
 *   "Three independent temperature measurement channels are available. Each
 *    channel can measure non-grounded thermocouples or PT100 sensors, but not
 *    both at the same time."
 *   "Only a single channel at a time is available to be read, according to
 *    the analog switch's position."
 *
 * So readings are a ROTATION, never a snapshot. This is the opposite of the
 * expanders, where one port read deliberately samples all 20 channels in the
 * same instant. Three temperatures on this page were taken up to a second
 * apart and the panel says so.
 */
#include "temp.h"
#include "persist.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/spi.h>
#include <string.h>

LOG_MODULE_REGISTER(pmc_temp, LOG_LEVEL_INF);

/*
 * The switches. SEL0/1/2 are ONE-HOT channel enables - one TMUX1511 each -
 * and TH throws the NX3L4053 between the two converters. They are not a
 * binary code; see the overlay comment.
 */
#define SEL0_PORT DT_NODELABEL(gpiod)
#define SEL0_PIN  6
#define SEL1_PORT DT_NODELABEL(gpioi)
#define SEL1_PIN  4
#define SEL2_PORT DT_NODELABEL(gpiog)
#define SEL2_PIN  10
#define TH_PORT   DT_NODELABEL(gpioc)
#define TH_PIN    15

static const struct device *const sel_dev[PMC_TEMP_COUNT] = {
	DEVICE_DT_GET(SEL0_PORT), DEVICE_DT_GET(SEL1_PORT), DEVICE_DT_GET(SEL2_PORT),
};
static const uint8_t sel_pin[PMC_TEMP_COUNT] = { SEL0_PIN, SEL1_PIN, SEL2_PIN };

static const struct device *const th_dev = DEVICE_DT_GET(TH_PORT);

static const struct device *const tc_dev  = DEVICE_DT_GET(DT_NODELABEL(pmc_tc));
/*
 * ONE CHIP, TWO HANDLES. Both are the same MAX31865 on the same chip select;
 * they differ only in the three-wire bit of the configuration register, and
 * the driver rewrites that register from the instance's own shadow before
 * every conversion. So reading through one of these IS selecting the wire
 * count - see the overlay for why poking the bit directly does not survive.
 */
static const struct device *const rtd_2w = DEVICE_DT_GET(DT_NODELABEL(pmc_rtd));
static const struct device *const rtd_3w = DEVICE_DT_GET(DT_NODELABEL(pmc_rtd_3w));

static atomic_t three_wire;   /* 0 = two/four-wire, 1 = three-wire */

static const struct device *rtd_now(void)
{
	return atomic_get(&three_wire) ? rtd_3w : rtd_2w;
}

uint8_t pmc_temp_wires_get(void)
{
	return atomic_get(&three_wire) ? 3 : 2;
}

/*
 * The raw view of the RTD converter, for its fault register. The driver reads
 * that register on a faulted conversion and then CLEARS it, so by the time a
 * caller sees -EIO the cause is already gone. Reading it here, on a
 * conversion of our own, is the only way to keep the detail.
 */
static const struct spi_dt_spec rtd_raw =
	SPI_DT_SPEC_GET(DT_NODELABEL(pmc_rtd),
			SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);

#define RTD_REG_CONFIG 0x00
#define RTD_REG_FAULT  0x07
#define RTD_WR(r)      ((r) | 0x80)

static int rtd_reg_write(uint8_t reg, uint8_t val)
{
	uint8_t cmd[2] = { RTD_WR(reg), val };
	struct spi_buf tx = { .buf = cmd, .len = sizeof(cmd) };
	const struct spi_buf_set txs = { .buffers = &tx, .count = 1 };

	return spi_write_dt(&rtd_raw, &txs);
}

static int rtd_reg_read(uint8_t reg, uint8_t *val)
{
	uint8_t addr = reg;
	struct spi_buf tx = { .buf = &addr, .len = 1 };
	const struct spi_buf_set txs = { .buffers = &tx, .count = 1 };
	struct spi_buf rx = { .buf = val, .len = 1 };
	const struct spi_buf_set rxs = { .buffers = &rx, .count = 1 };

	return spi_transceive_dt(&rtd_raw, &txs, &rxs);
}

/*
 * Run one conversion of our own and return the fault byte. Only called when
 * the driver has already reported a fault, so the extra 66 ms is paid on the
 * broken path and never on a healthy channel.
 *
 * Bit meanings are the MAX31865 datasheet's fault status register. They are
 * worth naming rather than printing as hex, because they distinguish "no
 * probe" from "the probe is there and one lead is open" from "the reference
 * resistor network is wrong" - three very different things to be told while
 * standing at a terminal block with a screwdriver.
 */
static uint8_t rtd_read_fault(void)
{
	uint8_t cfg = 0;
	uint8_t fault = 0;

	if (rtd_reg_read(RTD_REG_CONFIG, &cfg) != 0) {
		return 0;
	}

	/*
	 * RAISING VBIAS IS NOT ENOUGH - IT HAS TO RUN A DETECTION CYCLE.
	 *
	 * Zephyr configures this part with fault_cycle =
	 * MAX31865_FAULT_DETECTION_NONE and one_shot = true, so nothing
	 * converts and nothing tests the leads unless asked. An earlier
	 * version of this function set VBIAS, waited, and read the register -
	 * and got zero every time, because no cycle had run and the driver had
	 * just cleared the bits. Zero then read as "no fault" on a channel
	 * that was plainly open, which is the same kind of confident silence
	 * the raw word exists to prevent.
	 *
	 * The sequence below is the datasheet's automatic fault detection:
	 * VBIAS on, let the input network settle, write 01 into D3:D2, and
	 * poll until the chip clears those bits to signal it has finished.
	 */
	const uint8_t base = (uint8_t)((cfg | BIT(7)) & ~0x0Cu);

	if (rtd_reg_write(RTD_REG_CONFIG, base) != 0) {
		return 0;
	}
	/* VBIAS settling: the datasheet wants 10 time constants of the input
	 * RC, and a long probe lead is the slow case. 10 ms is generous. */
	k_msleep(10);

	if (rtd_reg_write(RTD_REG_CONFIG, (uint8_t)(base | 0x04u)) != 0) {
		(void)rtd_reg_write(RTD_REG_CONFIG, cfg);
		return 0;
	}

	/* The cycle is specified in microseconds; poll rather than guess, and
	 * give up rather than spin if the chip never clears the bits. */
	for (int i = 0; i < 20; i++) {
		uint8_t now = 0;

		k_msleep(1);
		if (rtd_reg_read(RTD_REG_CONFIG, &now) != 0) {
			break;
		}
		if ((now & 0x0Cu) == 0) {
			break;
		}
	}

	(void)rtd_reg_read(RTD_REG_FAULT, &fault);
	(void)rtd_reg_write(RTD_REG_CONFIG, cfg);
	return fault;
}

const char *pmc_temp_rtd_fault_str(uint8_t f)
{
	/* Most specific first: several bits can be set at once, and the
	 * narrowest true statement is the useful one. */
	if (f & BIT(2)) {
		return "over/under voltage on an input";
	}
	if (f & BIT(3)) {
		return "RTDIN- open - check the lead on TN";
	}
	if (f & BIT(4)) {
		return "REFIN- open - check the lead on TP";
	}
	if (f & BIT(5)) {
		return "REFIN- high - no probe, or an open circuit";
	}
	if (f & BIT(6)) {
		return "below the low threshold";
	}
	if (f & BIT(7)) {
		return "above the high threshold";
	}
	return NULL;
}

/*
 * THE DWELL. The MAX31855 free-runs, converting about every 100 ms with no
 * way to trigger or to ask whether the conversion in the register started
 * before or after the switches moved. Its datasheet gives 70..100 ms typical
 * and 100 ms maximum. So: wait out one guaranteed-complete conversion that
 * began after the mux settled, which needs two conversion periods, not one -
 * the first may have been in flight across the switch.
 *
 * 220 ms is that, with margin. A sweep of three channels therefore takes
 * about 2/3 of a second, which is why this is a thread and not something the
 * HTTP handler does while a browser waits.
 *
 * The MAX31865 is one-shot in Zephyr's driver - sample_fetch triggers a
 * conversion and waits for it - so it needs only the switch settling time,
 * which is nanoseconds. It gets a short dwell anyway because the RC on a long
 * probe lead is not nanoseconds.
 */
#define DWELL_THERMOCOUPLE_MS 220
#define DWELL_RTD_MS           20

/*
 * J ON A K-TYPE PART. The datasheet's own description of what this board
 * offers:
 *
 *   "Thermocouple K, non-grounded, front-end MAX31855KASA+T"
 *   "Thermocouple J, non-grounded, front-end MAX31855KASA+T with software
 *    multiplication coefficient"
 *
 * One converter, linearized for type K, and J is arithmetic applied after it.
 * The silkscreen prints J and K as if they were equals. They are not, and the
 * panel says so.
 *
 * The coefficient multiplies the DELTA FROM THE COLD JUNCTION, not the
 * reported temperature. The converter computes
 *
 *     T_report = T_cold + V_measured / S_K
 *
 * so the measured thermo-EMF is V = S_K * (T_report - T_cold). A type J
 * junction producing that same EMF sat at
 *
 *     T_real = T_cold + V / S_J = T_cold + (T_report - T_cold) * S_K / S_J
 *
 * Multiplying the absolute reading instead - the obvious reading of
 * "multiplication coefficient" - is wrong by the cold junction temperature
 * scaled, which is about 5 C of error at room temperature and goes to zero
 * only at 0 C. That is small enough to look like calibration and large enough
 * to matter, which is the worst size for an error.
 *
 * S_K = 41.0 uV/C and S_J = 51.7 uV/C near 0..100 C. Both Seebeck
 * coefficients are curves, not constants, so this is good close to the cold
 * junction and drifts as the span widens. DERIVED, NOT MEASURED: it has never
 * been checked against a J probe on this bench, the raw K reading is kept and
 * rendered beside it, and the panel marks J as approximate. Arduino's own
 * library applies some coefficient here too; this one is not copied from it,
 * it is derived from the Seebeck ratio, because a constant whose origin is
 * unknown cannot be corrected later.
 */
#define S_K_NV_PER_C 41000  /* 41.0 uV/C, in nV to keep the ratio integral */
#define S_J_NV_PER_C 51700  /* 51.7 uV/C */

static struct pmc_temp_chan chans[PMC_TEMP_COUNT];
static struct k_mutex chans_lock;

static atomic_t mode = (atomic_t)PMC_TEMP_K;
static atomic_t sweeps;

/*
 * HOW MANY TIMES THE MODE HAS BEEN SET SINCE BOOT.
 *
 * Not a feature - an instrument. On 0.48.0 this board reported PT100 25
 * seconds after a flash, with K compiled in as the initial value and no
 * request able to change it without a token. A clean reset gave K, the ELF's
 * .data holds 02 at that address, and the cause was never established.
 *
 * pmc_temp_mode_set() is the only writer, so a non-zero count here means
 * something called it and the question becomes who; a zero count beside a
 * mode of PT100 would mean the variable changed without the setter, which is
 * a different and much worse problem. Guessing between those two costs more
 * than one counter.
 */
static atomic_t mode_sets;
static atomic_t faults;
static atomic_t ready;

const char *pmc_temp_mode_name(enum pmc_temp_mode m)
{
	switch (m) {
	case PMC_TEMP_PT100: return "PT100";
	case PMC_TEMP_J:     return "J";
	case PMC_TEMP_K:     return "K";
	default:             return "?";
	}
}

enum pmc_temp_mode pmc_temp_mode_get(void)
{
	return (enum pmc_temp_mode)atomic_get(&mode);
}

bool pmc_temp_ready(void)
{
	return atomic_get(&ready) != 0;
}

uint32_t pmc_temp_sweeps(void)
{
	return (uint32_t)atomic_get(&sweeps);
}

uint32_t pmc_temp_mode_sets(void)
{
	return (uint32_t)atomic_get(&mode_sets);
}

uint32_t pmc_temp_faults(void)
{
	return (uint32_t)atomic_get(&faults);
}

bool pmc_temp_get(uint8_t n, struct pmc_temp_chan *out)
{
	if (n >= PMC_TEMP_COUNT || out == NULL) {
		return false;
	}
	k_mutex_lock(&chans_lock, K_FOREVER);
	*out = chans[n];
	k_mutex_unlock(&chans_lock);
	return true;
}

bool pmc_temp_mode_set(const char *name, char *detail, size_t cap)
{
	enum pmc_temp_mode m;

	if (name == NULL) {
		(void)snprintk(detail, cap, "refused: no mode given\n");
		return false;
	}
	if (strcmp(name, "PT100") == 0) {
		m = PMC_TEMP_PT100;
	} else if (strcmp(name, "J") == 0) {
		m = PMC_TEMP_J;
	} else if (strcmp(name, "K") == 0) {
		m = PMC_TEMP_K;
	} else {
		(void)snprintk(detail, cap,
			       "refused: %s is not a mode - PT100, J or K\n", name);
		return false;
	}

	if ((enum pmc_temp_mode)atomic_get(&mode) == m) {
		(void)snprintk(detail, cap, "already %s\n", pmc_temp_mode_name(m));
		return true;
	}

	atomic_set(&mode, (atomic_t)m);
	atomic_inc(&mode_sets);

	/*
	 * EVERY CHANNEL BECOMES UNREAD, NOW. The numbers on screen were taken
	 * through the other converter; under a new label they would be a
	 * confident lie, and they would persist for up to a full sweep. Better
	 * three dashes for half a second.
	 */
	atomic_set(&ready, 0);
	k_mutex_lock(&chans_lock, K_FOREVER);
	for (uint8_t i = 0; i < PMC_TEMP_COUNT; i++) {
		chans[i].state = PMC_T_UNREAD;
	}
	k_mutex_unlock(&chans_lock);

	LOG_INF("temp mode -> %s", pmc_temp_mode_name(m));
	(void)snprintk(detail, cap, "mode %s - readings clear until the next sweep\n",
		       pmc_temp_mode_name(m));
	pmc_persist_note();
	return true;
}

bool pmc_temp_wires_set(uint8_t wires, char *detail, size_t cap)
{
	if (wires != 2 && wires != 3) {
		(void)snprintk(detail, cap,
			       "refused: wires must be 2 or 3, got %u\n",
			       (unsigned)wires);
		return false;
	}

	const bool want = (wires == 3);

	if ((atomic_get(&three_wire) != 0) == want) {
		(void)snprintk(detail, cap, "already %u-wire\n", (unsigned)wires);
		return true;
	}

	atomic_set(&three_wire, want ? 1 : 0);

	/*
	 * Clear the readings for the same reason a mode change does: the
	 * previous numbers were taken with a different lead-resistance
	 * correction, and that difference is exactly the error this setting
	 * exists to remove. Keeping them under the new label would hide it.
	 */
	atomic_set(&ready, 0);
	k_mutex_lock(&chans_lock, K_FOREVER);
	for (uint8_t i = 0; i < PMC_TEMP_COUNT; i++) {
		chans[i].state = PMC_T_UNREAD;
	}
	k_mutex_unlock(&chans_lock);

	LOG_INF("RTD wiring -> %u-wire", (unsigned)wires);
	(void)snprintk(detail, cap,
		       "%u-wire - readings clear until the next sweep\n",
		       (unsigned)wires);
	pmc_persist_note();
	return true;
}

/*
 * Select exactly one channel. Written as "all off, then one on" rather than
 * as a loop that sets each to its target, because the intermediate state of
 * the second form can have two switches closed at once - which parallels two
 * probes onto one converter. Order matters and the compiler is free to
 * reorder stores it cannot see a reason to keep; these are device writes, so
 * it cannot, but the shape should not depend on that.
 */
static int select_channel(uint8_t n)
{
	int rc;

	for (uint8_t i = 0; i < PMC_TEMP_COUNT; i++) {
		rc = gpio_pin_set(sel_dev[i], sel_pin[i], 0);
		if (rc != 0) {
			return rc;
		}
	}
	return gpio_pin_set(sel_dev[n], sel_pin[n], 1);
}

/*
 * A second, raw view of the same chip. Reading twice does not cost a
 * conversion - the MAX31855 free-runs and this just re-reads the register it
 * already latched - and it is the only way to see the die temperature when
 * the fault bit is set, because the driver refuses every channel in that
 * case. Same CS, same bus: Zephyr serializes per controller and both reads
 * happen on this thread, so there is no interleaving to worry about.
 */
static const struct spi_dt_spec tc_raw =
	SPI_DT_SPEC_GET(DT_NODELABEL(pmc_tc),
			SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);

static uint32_t read_tc_word(void)
{
	uint8_t buf[4] = { 0 };
	struct spi_buf rx = { .buf = buf, .len = sizeof(buf) };
	const struct spi_buf_set rxs = { .buffers = &rx, .count = 1 };

	if (spi_read_dt(&tc_raw, &rxs) != 0) {
		return 0;
	}
	return sys_get_be32(buf);
}

static int read_thermocouple(struct pmc_temp_chan *c, enum pmc_temp_mode m)
{
	struct sensor_value hot, cold;
	int rc;

	rc = sensor_sample_fetch(tc_dev);
	if (rc != 0) {
		return rc;
	}

	c->raw_word = read_tc_word();

	rc = sensor_channel_get(tc_dev, SENSOR_CHAN_AMBIENT_TEMP, &hot);
	if (rc == -EIO) {
		/*
		 * The driver returns -EIO for the fault bit, which on a
		 * MAX31855 means open circuit, short to GND or short to VCC.
		 * With nothing screwed to the terminals that is the normal
		 * state, so it is reported as OPEN rather than as a failure:
		 * an empty channel is not a broken one.
		 */
		c->state = PMC_T_OPEN;

		/*
		 * Decode the die temperature out of the raw word by hand,
		 * since the driver will not hand it over once the fault bit is
		 * set. Bits 4..15, 12-bit signed, 0.0625 C per count. This is
		 * what makes "open" falsifiable: a plausible board temperature
		 * here means the chip is alive and the terminals are simply
		 * empty.
		 */
		if (c->raw_word != 0xFFFFFFFFu && c->raw_word != 0) {
			int32_t d = (int32_t)((c->raw_word >> 4) & 0xFFF);

			if (d & BIT(11)) {
				d -= 4096;
			}
			c->cold_milli_c = d * 625 / 10;
		}
		return 0;
	}
	if (rc != 0) {
		return rc;
	}

	rc = sensor_channel_get(tc_dev, SENSOR_CHAN_DIE_TEMP, &cold);
	if (rc != 0) {
		return rc;
	}

	c->raw_milli_c = hot.val1 * 1000 + hot.val2 / 1000;
	c->cold_milli_c = cold.val1 * 1000 + cold.val2 / 1000;

	if (m == PMC_TEMP_J) {
		int32_t delta = c->raw_milli_c - c->cold_milli_c;
		/* 64-bit so a 1000 C delta cannot overflow the multiply. */
		int64_t scaled = ((int64_t)delta * S_K_NV_PER_C) / S_J_NV_PER_C;
		c->milli_c = c->cold_milli_c + (int32_t)scaled;
	} else {
		c->milli_c = c->raw_milli_c;
	}

	c->state = PMC_T_OK;
	return 0;
}

static int read_rtd(struct pmc_temp_chan *c)
{
	const struct device *dev = rtd_now();
	struct sensor_value t;
	int rc;

	rc = sensor_sample_fetch(dev);
	if (rc == -EIO) {
		/*
		 * The driver saw the RTD register's fault flag, logged a cause
		 * and then cleared the register. Take our own conversion to
		 * recover which cause it was - this is the whole reason the
		 * PT100 path is more informative than the thermocouple one.
		 */
		c->rtd_fault = rtd_read_fault();
		c->state = PMC_T_OPEN;
		return 0;
	}
	if (rc != 0) {
		return rc;
	}
	rc = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &t);
	if (rc != 0) {
		return rc;
	}

	c->raw_milli_c = t.val1 * 1000 + t.val2 / 1000;
	c->milli_c = c->raw_milli_c;
	c->cold_milli_c = 0;   /* no cold junction in an RTD measurement */

	/*
	 * An unconnected PT100 input reads at the top of the range rather than
	 * raising a fault, because the bias current into an open circuit
	 * drives the input to the reference. Anything beyond the PT100's own
	 * specified span is not a temperature.
	 */
	if (c->milli_c > 850000 || c->milli_c < -200000) {
		c->state = PMC_T_OPEN;
	} else {
		c->state = PMC_T_OK;
	}
	return 0;
}

static void sweep(void)
{
	enum pmc_temp_mode m = pmc_temp_mode_get();
	bool tc = (m != PMC_TEMP_PT100);
	int rc;

	/* Throw the front-end switch once per sweep, not once per channel. */
	rc = gpio_pin_set(th_dev, TH_PIN, tc ? 1 : 0);
	if (rc != 0) {
		LOG_ERR("front-end select: %d", rc);
		return;
	}

	for (uint8_t n = 0; n < PMC_TEMP_COUNT; n++) {
		struct pmc_temp_chan c = { 0 };

		rc = select_channel(n);
		if (rc != 0) {
			LOG_ERR("channel select %u: %d", (unsigned)n, rc);
			c.state = PMC_T_ERR;
		} else {
			k_msleep(tc ? DWELL_THERMOCOUPLE_MS : DWELL_RTD_MS);

			rc = tc ? read_thermocouple(&c, m) : read_rtd(&c);
			if (rc != 0) {
				c.state = PMC_T_ERR;
				atomic_inc(&faults);
				LOG_WRN("temp%u read: %d", (unsigned)n, rc);
			}
		}

		k_mutex_lock(&chans_lock, K_FOREVER);
		chans[n] = c;
		k_mutex_unlock(&chans_lock);
	}

	atomic_inc(&sweeps);
	atomic_set(&ready, 1);
}

static void temp_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		sweep();
		k_msleep(500);
	}
}

K_THREAD_STACK_DEFINE(temp_stack, 2048);
static struct k_thread temp_tid;

int pmc_temp_init(void)
{
	int rc;

	k_mutex_init(&chans_lock);

	for (uint8_t i = 0; i < PMC_TEMP_COUNT; i++) {
		if (!device_is_ready(sel_dev[i])) {
			LOG_ERR("SEL%u port not ready", (unsigned)i);
			return -ENODEV;
		}
		rc = gpio_pin_configure(sel_dev[i], sel_pin[i], GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			LOG_ERR("SEL%u configure: %d", (unsigned)i, rc);
			return rc;
		}
	}

	if (!device_is_ready(th_dev)) {
		LOG_ERR("TH port not ready");
		return -ENODEV;
	}
	rc = gpio_pin_configure(th_dev, TH_PIN, GPIO_OUTPUT_INACTIVE);
	if (rc != 0) {
		LOG_ERR("TH configure: %d", rc);
		return rc;
	}

	if (!device_is_ready(tc_dev)) {
		LOG_ERR("MAX31855 not ready");
		return -ENODEV;
	}
	if (!device_is_ready(rtd_2w) || !device_is_ready(rtd_3w)) {
		LOG_ERR("MAX31865 not ready");
		return -ENODEV;
	}

	k_thread_create(&temp_tid, temp_stack, K_THREAD_STACK_SIZEOF(temp_stack),
			temp_thread, NULL, NULL, NULL, 7, 0, K_NO_WAIT);
	k_thread_name_set(&temp_tid, "pmc_temp");

	LOG_INF("temperature ready: 3 channels, mode %s, %d ms dwell",
		pmc_temp_mode_name(pmc_temp_mode_get()), DWELL_THERMOCOUPLE_MS);
	return 0;
}
