/*
 * RS485/RS232 terminal.
 *
 * WHY THE DRIVER-ENABLE PIN IS THE WHOLE STORY.
 *
 * An STM32 USART can run RS485 turnaround in hardware: the DEM bit drives a
 * driver-enable pin, and DEAT/DEDT set how long it leads the first bit and
 * lags the last one, in register fields, exactly. Zephyr exposes it -
 * `de-enable`, `de-assert-time`, `de-deassert-time`, UART_CFG_FLOW_CTRL_RS485.
 *
 * This carrier cannot use any of it. UART4's hardware DE exists only on PA15
 * or PB14 in this package; the board routes DE to PI_13 and RE to PI_10, which
 * are ordinary GPIOs. So the turnaround is software's problem, and software
 * turnaround is the single most reported cause of a silent Modbus bus on this
 * hardware - the stock library's default post-delay of 50 is reported to get
 * no answer at all from some devices.
 *
 * Two things follow, and they are the reason this file is careful rather than
 * short:
 *
 *   DE MUST BE HELD UNTIL THE LAST BIT HAS LEFT THE SHIFT REGISTER, not until
 *   the last byte has been handed to the UART. Those differ by one character
 *   time, and dropping DE early truncates the final byte - producing a frame
 *   whose CRC fails at the far end while the sender believes it sent it. This
 *   waits on the transmission-complete flag, not on the write returning.
 *
 *   THE TURNAROUND IS COMPUTED FROM THE BAUD RATE, never a constant. It is
 *   also reported, so a person can see it.
 */
#include "rs485.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <string.h>

LOG_MODULE_REGISTER(pmc_485, LOG_LEVEL_INF);

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(uart4));

/*
 * The transceiver's control pins. This is a multiprotocol part - it does
 * RS485 half duplex, RS485 full duplex and RS232 depending on how these are
 * driven - and no example anywhere touches most of them. Termination and
 * duplex in particular are the classic "works on the bench, fails on a long
 * cable" pair, so they are controls here rather than assumptions.
 */
#define PIN(port, n) { DEVICE_DT_GET(DT_NODELABEL(port)), n }
struct pin { const struct device *dev; gpio_pin_t pin; };

/*
 * RE IS ACTIVE LOW, AND GETTING THIS WRONG MADE THE BOARD DEAF.
 *
 * The transceiver is an SP335ECR1-L (datasheet 3-x: "SP335ECR1-L with onboard
 * termination resistors. RS-485 is configurable to be half duplex or full
 * duplex"). Like every RS485 transceiver, its receiver enable is /RE: LOW
 * enables the receiver, HIGH disables it. The driver enable DE is the
 * opposite, active high, which is exactly why the two are usually tied
 * together and driven as one signal.
 *
 * This file originally drove RE HIGH and called it "receiver on". The
 * receiver was therefore off from the moment the board booted, and every
 * reading of zero received bytes in this project was a deaf radio rather than
 * a quiet bus. Nothing detected it, because an idle RS485 line and a disabled
 * receiver produce identical evidence: nothing.
 *
 * It was found by sending a frame with echo enabled and expecting to hear it
 * come back. tx 8, rx 0. That test is the only thing in the session that
 * could have distinguished the two, which is an argument for building the
 * loopback demonstration early rather than last.
 */
#define RE_ENABLE  0   /* /RE low  = receiver listening */
#define RE_DISABLE 1   /* /RE high = receiver muted     */

static const struct pin p_de     = PIN(gpioi, 13);  /* driver enable, active high */
static const struct pin p_re     = PIN(gpioi, 10);  /* /RE, ACTIVE LOW            */
static const struct pin p_en     = PIN(gpiog, 9);   /* transceiver on     */
/*
 * PROTOCOL SELECT, AND IT IS THE OPPOSITE OF ITS NAME.
 *
 * MEASURED on this board with a jumper across the TX and RX pairs, sweeping
 * every control pin and sending the same frame each time:
 *
 *     rs232 hf de | rx | hex
 *     ------------+----+------------------
 *     0     0  0  |  1 | 00
 *     0     0  1  |  0 |
 *     1     0  1  |  8 | 010300000001840a   <- the frame, exactly
 *     1     1  1  |  8 | 010300000001840a
 *
 * HIGH selects RS485. LOW selects RS232. The pin was named for RS232 in the
 * pin list this was transcribed from, and the obvious reading - drive it high
 * for RS232 - is backwards. Driven low "for RS485", the transceiver sat in
 * RS232 the entire time and nothing was ever put on the 485 pair, which is
 * why a transmitter that was working perfectly produced a receiver that heard
 * nothing.
 *
 * Note what the sweep also settles, since both were suspected and neither was
 * guilty: DE is active HIGH (de=1 is the only column that works) and EN is
 * active HIGH (en=0 produced no bytes at all in an earlier sweep). Only /RE
 * and this pin were inverted.
 */
#define PROTO_RS485 1
#define PROTO_RS232 0

static const struct pin p_rs232  = PIN(gpioa, 10);  /* protocol select, see above */
static const struct pin p_term   = PIN(gpioi, 14);  /* 120R termination   */
static const struct pin p_fdterm = PIN(gpioi, 15);  /* full-duplex term   */
static const struct pin p_slew   = PIN(gpiog, 14);  /* slew limiting      */
static const struct pin p_hf     = PIN(gpioa, 9);   /* half / full duplex */

static struct pmc_485_cfg cfg = {
	.baud = 19200, .data_bits = 8, .parity = 'N', .stop_bits = 1,
	.mode = PMC_485_HALF, .term = false, .slew = false, .echo = true,
};
static struct k_mutex cfg_lock;

/* --- the receive ring ---------------------------------------------------
 * Absolute positions, not indices, so a reader can tell the difference
 * between "nothing new" and "so much arrived that your window is gone".
 */
static uint8_t  ring[PMC_485_RING];
static uint64_t rx_total;
static uint64_t tx_total;
/* Uptime of the last byte each way, 0 = never. Drives the panel's TX/RX
   lamps: a total cannot say "recently", and a lamp needs that. */
static int64_t  tx_stamp;
static int64_t  rx_stamp;
static atomic_t rx_errors;
static struct k_spinlock ring_lock;

/* --- the repeater ------------------------------------------------------- */
static uint8_t  rep_buf[PMC_485_TX_MAX];
static size_t   rep_len;
static uint32_t rep_period_ms;
static struct k_mutex rep_lock;

bool pmc_rs485_pin_set(const char *name, int value, char *detail, size_t cap)
{
	static const struct { const char *n; const struct pin *p; } tbl[] = {
		{ "de", &p_de },         { "re", &p_re },
		{ "en", &p_en },         { "rs232", &p_rs232 },
		{ "term", &p_term },     { "fdterm", &p_fdterm },
		{ "slew", &p_slew },     { "hf", &p_hf },
	};

	if (name == NULL) {
		(void)snprintk(detail, cap, "refused: no pin named\n");
		return false;
	}
	for (size_t i = 0; i < ARRAY_SIZE(tbl); i++) {
		if (strcmp(name, tbl[i].n) == 0) {
			int rc = gpio_pin_set(tbl[i].p->dev, tbl[i].p->pin,
					      value ? 1 : 0);

			(void)snprintk(detail, cap, "%s <- %d (rc %d)\n",
				       tbl[i].n, value ? 1 : 0, rc);
			return rc == 0;
		}
	}
	(void)snprintk(detail, cap,
		       "refused: '%s' is not a control pin - de, re, en, "
		       "rs232, term, fdterm, slew, hf\n", name);
	return false;
}

static int pin_set(const struct pin *p, int v)
{
	return gpio_pin_set(p->dev, p->pin, v);
}

uint32_t pmc_rs485_char_us(void)
{
	/*
	 * Bits on the wire per character: one start, the data bits, parity if
	 * any, and the stop bits. Counting this properly is the difference
	 * between a turnaround that works and one that is close enough to look
	 * right at 9600 and fail at 115200.
	 */
	uint32_t bits = 1U + cfg.data_bits + (cfg.parity != 'N' ? 1U : 0U) +
			cfg.stop_bits;

	if (cfg.baud == 0) {
		return 0;
	}
	return (bits * 1000000U) / cfg.baud;
}

uint32_t pmc_rs485_turnaround_us(void)
{
	/* Modbus RTU calls a frame boundary 3.5 character times. */
	return (pmc_rs485_char_us() * 7U) / 2U;
}

static void apply_pins(void)
{
	const bool rs232 = (cfg.mode == PMC_485_RS232);
	const bool full = (cfg.mode == PMC_485_FULL);

	(void)pin_set(&p_en, 1);
	(void)pin_set(&p_rs232, rs232 ? PROTO_RS232 : PROTO_RS485);
	(void)pin_set(&p_hf, full ? 1 : 0);
	(void)pin_set(&p_slew, cfg.slew ? 1 : 0);
	(void)pin_set(&p_term, cfg.term && !full ? 1 : 0);
	(void)pin_set(&p_fdterm, cfg.term && full ? 1 : 0);

	/* Idle: driver off, receiver listening. */
	(void)pin_set(&p_de, 0);
	(void)pin_set(&p_re, RE_ENABLE);
}

static int apply_uart(char *detail, size_t cap)
{
	struct uart_config uc = {
		.baudrate = cfg.baud,
		.data_bits = (cfg.data_bits == 7) ? UART_CFG_DATA_BITS_7
						  : UART_CFG_DATA_BITS_8,
		.stop_bits = (cfg.stop_bits == 2) ? UART_CFG_STOP_BITS_2
						  : UART_CFG_STOP_BITS_1,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
	int rc;

	switch (cfg.parity) {
	case 'E': uc.parity = UART_CFG_PARITY_EVEN; break;
	case 'O': uc.parity = UART_CFG_PARITY_ODD;  break;
	default:  uc.parity = UART_CFG_PARITY_NONE; break;
	}

	rc = uart_configure(uart, &uc);
	if (rc != 0 && detail != NULL) {
		(void)snprintk(detail, cap,
			       "uart_configure refused %u %u%c%u (%d)",
			       (unsigned)cfg.baud, (unsigned)cfg.data_bits,
			       cfg.parity, (unsigned)cfg.stop_bits, rc);
	}
	return rc;
}

void pmc_rs485_cfg_get(struct pmc_485_cfg *out)
{
	k_mutex_lock(&cfg_lock, K_FOREVER);
	*out = cfg;
	k_mutex_unlock(&cfg_lock);
}

bool pmc_rs485_cfg_set(const struct pmc_485_cfg *in, char *detail, size_t cap)
{
	/*
	 * Bounded here rather than trusted. A baud the clock cannot divide to
	 * is refused by uart_configure, which is the honest place for it to
	 * fail, but the obviously silly values are caught first so the message
	 * names the field.
	 */
	/*
	 * THE CEILING IS THE HARDWARE'S TO SET, NOT THIS FUNCTION'S.
	 *
	 * 921600 was a guess written when nothing could be tested. The SP335
	 * is specified to 20 Mbps on RS-485, and what actually limits this
	 * board is the UART kernel clock and its oversampling, which
	 * uart_configure() already knows and this code does not. So the range
	 * is opened to the transceiver's own figure and the driver is allowed
	 * to refuse what it cannot clock - a refusal from the peripheral is a
	 * fact, where a refusal from a constant here is only an opinion.
	 *
	 * Configuring successfully is not the same as working. With the TX
	 * and RX pairs jumpered, every rate can be checked by sending a frame
	 * and seeing whether the same bytes come back, which is the
	 * difference between a baud the UART accepts and a baud the link
	 * survives.
	 */
	if (in->baud < 1200 || in->baud > 20000000) {
		(void)snprintk(detail, cap,
			       "refused: baud %u outside 1200..20000000\n",
			       (unsigned)in->baud);
		return false;
	}
	if (in->data_bits != 7 && in->data_bits != 8) {
		(void)snprintk(detail, cap, "refused: data bits must be 7 or 8\n");
		return false;
	}
	if (in->parity != 'N' && in->parity != 'E' && in->parity != 'O') {
		(void)snprintk(detail, cap, "refused: parity must be N, E or O\n");
		return false;
	}
	if (in->stop_bits != 1 && in->stop_bits != 2) {
		(void)snprintk(detail, cap, "refused: stop bits must be 1 or 2\n");
		return false;
	}

	k_mutex_lock(&cfg_lock, K_FOREVER);
	const struct pmc_485_cfg prev = cfg;

	cfg = *in;
	if (apply_uart(detail, cap) != 0) {
		cfg = prev;
		(void)apply_uart(NULL, 0);
		k_mutex_unlock(&cfg_lock);
		return false;
	}
	apply_pins();
	k_mutex_unlock(&cfg_lock);

	LOG_INF("485: %u %u%c%u %s term=%d slew=%d echo=%d turnaround=%u us",
		(unsigned)cfg.baud, (unsigned)cfg.data_bits, cfg.parity,
		(unsigned)cfg.stop_bits,
		cfg.mode == PMC_485_RS232 ? "rs232" :
		cfg.mode == PMC_485_FULL ? "full" : "half",
		(int)cfg.term, (int)cfg.slew, (int)cfg.echo,
		(unsigned)pmc_rs485_turnaround_us());

	(void)snprintk(detail, cap,
		       "%u %u%c%u %s, turnaround %u us\n",
		       (unsigned)cfg.baud, (unsigned)cfg.data_bits, cfg.parity,
		       (unsigned)cfg.stop_bits,
		       cfg.mode == PMC_485_RS232 ? "RS232" :
		       cfg.mode == PMC_485_FULL ? "full duplex" : "half duplex",
		       (unsigned)pmc_rs485_turnaround_us());
	return true;
}

static void ring_put(uint8_t b)
{
	k_spinlock_key_t k = k_spin_lock(&ring_lock);

	ring[rx_total % PMC_485_RING] = b;
	rx_total++;
	rx_stamp = k_uptime_get();
	k_spin_unlock(&ring_lock, k);
}

size_t pmc_rs485_rx_read(uint64_t from, uint8_t *out, size_t cap,
			 uint64_t *next, bool *lost)
{
	k_spinlock_key_t k = k_spin_lock(&ring_lock);
	const uint64_t total = rx_total;
	uint64_t oldest = (total > PMC_485_RING) ? total - PMC_485_RING : 0;
	size_t n = 0;

	*lost = false;
	if (from < oldest) {
		/* The window asked for has been overwritten. Say so. */
		*lost = true;
		from = oldest;
	}
	while (from < total && n < cap) {
		out[n++] = ring[from % PMC_485_RING];
		from++;
	}
	*next = from;
	k_spin_unlock(&ring_lock, k);
	return n;
}

uint64_t pmc_rs485_rx_total(void)
{
	k_spinlock_key_t k = k_spin_lock(&ring_lock);
	uint64_t v = rx_total;

	k_spin_unlock(&ring_lock, k);
	return v;
}

uint64_t pmc_rs485_tx_total(void)
{
	return tx_total;
}

uint32_t pmc_rs485_errors(void)
{
	return (uint32_t)atomic_get(&rx_errors);
}

void pmc_rs485_clear(void)
{
	k_spinlock_key_t k = k_spin_lock(&ring_lock);

	rx_total = 0;
	rx_stamp = 0;
	k_spin_unlock(&ring_lock, k);
	tx_total = 0;
	tx_stamp = 0;
	atomic_set(&rx_errors, 0);
}

void pmc_rs485_activity(int32_t *tx_age_ms, int32_t *rx_age_ms)
{
	const int64_t now = k_uptime_get();
	k_spinlock_key_t k = k_spin_lock(&ring_lock);
	const int64_t rx = rx_stamp;

	k_spin_unlock(&ring_lock, k);

	*tx_age_ms = (tx_stamp == 0) ? -1 : (int32_t)(now - tx_stamp);
	*rx_age_ms = (rx == 0) ? -1 : (int32_t)(now - rx);
}

static void rx_isr(const struct device *dev, void *user)
{
	ARG_UNUSED(user);

	/* Returns void in this Zephyr; the `if (!uart_irq_update(...))` idiom
	 * in older samples does not compile against it. */
	uart_irq_update(dev);

	while (uart_irq_rx_ready(dev) > 0) {
		uint8_t b;

		if (uart_fifo_read(dev, &b, 1) == 1) {
			ring_put(b);
		}
	}
}

int pmc_rs485_send_raw(const uint8_t *buf, size_t len, char *detail, size_t cap)
{
	if (len == 0 || len > PMC_485_TX_MAX) {
		(void)snprintk(detail, cap, "refused: 1..%u bytes\n",
			       (unsigned)PMC_485_TX_MAX);
		return -EINVAL;
	}

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart, buf[i]);
	}

	const uint32_t ct = pmc_rs485_char_us() ? pmc_rs485_char_us() : 100;

	for (int i = 0; i < 64; i++) {
		if (uart_irq_tx_complete(uart) > 0) {
			break;
		}
		k_busy_wait(ct);
	}

	tx_total += len;
	tx_stamp = k_uptime_get();
	(void)snprintk(detail, cap,
		       "raw: %u bytes shifted out, control pins untouched\n",
		       (unsigned)len);
	return (int)len;
}

int pmc_rs485_send(const uint8_t *buf, size_t len, char *detail, size_t cap)
{
	if (len == 0 || len > PMC_485_TX_MAX) {
		(void)snprintk(detail, cap,
			       "refused: 1..%u bytes, got %u\n",
			       (unsigned)PMC_485_TX_MAX, (unsigned)len);
		return -EINVAL;
	}

	k_mutex_lock(&cfg_lock, K_FOREVER);

	/*
	 * THE RECEIVER STAYS ON BY DEFAULT. On a half-duplex pair the driver's
	 * own bytes come straight back, and that echo is the single most
	 * useful diagnostic this terminal has: seeing your own frame in the
	 * receive window proves the driver actually drove the bus, which is
	 * exactly what "Connection timed out" never tells you. Turn it off to
	 * read a bus the way a quiet listener would.
	 */
	(void)pin_set(&p_re, cfg.echo ? RE_ENABLE : RE_DISABLE);

	/* Driver on, then the bus needs a moment before the first bit. */
	(void)pin_set(&p_de, 1);
	k_busy_wait(pmc_rs485_turnaround_us());

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart, buf[i]);
	}

	/*
	 * WAIT FOR THE SHIFT REGISTER, NOT FOR THE WRITE. uart_poll_out
	 * returns once the byte is in the transmit register; the last bit is
	 * still a character time from being on the wire. Dropping DE here
	 * truncates the final byte, and the far end sees a CRC failure while
	 * this end believes the frame was sent.
	 *
	 * uart_irq_tx_complete() reads the transmission-complete flag. The
	 * bounded loop is there because a hung peripheral must not hang the
	 * web server.
	 */
	const uint32_t ct = pmc_rs485_char_us() ? pmc_rs485_char_us() : 100;

	for (int i = 0; i < 64; i++) {
		if (uart_irq_tx_complete(uart) > 0) {
			break;
		}
		k_busy_wait(ct);
	}
	k_busy_wait(pmc_rs485_turnaround_us());

	(void)pin_set(&p_de, 0);
	(void)pin_set(&p_re, RE_ENABLE);

	tx_total += len;
	tx_stamp = k_uptime_get();
	k_mutex_unlock(&cfg_lock);

	(void)snprintk(detail, cap, "sent %u bytes\n", (unsigned)len);
	return (int)len;
}

bool pmc_rs485_repeat_set(const uint8_t *buf, size_t len, uint32_t period_ms,
			  char *detail, size_t cap)
{
	if (period_ms != 0 && (period_ms < 10 || period_ms > 60000)) {
		(void)snprintk(detail, cap,
			       "refused: period must be 0 (off) or 10..60000 ms\n");
		return false;
	}
	if (period_ms != 0 && (len == 0 || len > PMC_485_TX_MAX)) {
		(void)snprintk(detail, cap,
			       "refused: 1..%u bytes to repeat, got %u\n",
			       (unsigned)PMC_485_TX_MAX, (unsigned)len);
		return false;
	}

	k_mutex_lock(&rep_lock, K_FOREVER);
	if (len > 0) {
		memcpy(rep_buf, buf, len);
		rep_len = len;
	}
	rep_period_ms = period_ms;
	k_mutex_unlock(&rep_lock);

	if (period_ms == 0) {
		(void)snprintk(detail, cap, "repeat off\n");
	} else {
		(void)snprintk(detail, cap,
			       "repeating %u bytes every %u ms\n",
			       (unsigned)rep_len, (unsigned)period_ms);
	}
	return true;
}

void pmc_rs485_repeat_get(uint8_t *buf, size_t cap, size_t *len,
			  uint32_t *period_ms)
{
	k_mutex_lock(&rep_lock, K_FOREVER);
	size_t n = (rep_len < cap) ? rep_len : cap;

	memcpy(buf, rep_buf, n);
	*len = n;
	*period_ms = rep_period_ms;
	k_mutex_unlock(&rep_lock);
}

static void repeat_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		uint8_t buf[PMC_485_TX_MAX];
		size_t len = 0;
		uint32_t period = 0;
		char detail[48];

		pmc_rs485_repeat_get(buf, sizeof(buf), &len, &period);

		if (period == 0 || len == 0) {
			k_msleep(100);
			continue;
		}
		(void)pmc_rs485_send(buf, len, detail, sizeof(detail));
		k_msleep(period);
	}
}

K_THREAD_STACK_DEFINE(rep_stack, 1536);
static struct k_thread rep_tid;

int pmc_rs485_init(void)
{
	static const struct pin *const pins[] = {
		&p_de, &p_re, &p_en, &p_rs232, &p_term, &p_fdterm, &p_slew, &p_hf,
	};
	int rc;

	k_mutex_init(&cfg_lock);
	k_mutex_init(&rep_lock);

	if (!device_is_ready(uart)) {
		LOG_ERR("uart4 not ready");
		return -ENODEV;
	}

	for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
		if (!device_is_ready(pins[i]->dev)) {
			LOG_ERR("485 control port not ready");
			return -ENODEV;
		}
		rc = gpio_pin_configure(pins[i]->dev, pins[i]->pin,
					GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			LOG_ERR("485 pin configure: %d", rc);
			return rc;
		}
	}

	rc = apply_uart(NULL, 0);
	if (rc != 0) {
		LOG_ERR("485 uart configure: %d", rc);
		return rc;
	}
	apply_pins();

	uart_irq_callback_user_data_set(uart, rx_isr, NULL);
	uart_irq_rx_enable(uart);

	k_thread_create(&rep_tid, rep_stack, K_THREAD_STACK_SIZEOF(rep_stack),
			repeat_thread, NULL, NULL, NULL, 8, 0, K_NO_WAIT);
	k_thread_name_set(&rep_tid, "pmc_485rep");

	LOG_INF("485 ready: %u %u%c%u, turnaround %u us (software DE - this "
		"carrier does not wire the UART's own DE pin)",
		(unsigned)cfg.baud, (unsigned)cfg.data_bits, cfg.parity,
		(unsigned)cfg.stop_bits, (unsigned)pmc_rs485_turnaround_us());
	return 0;
}
