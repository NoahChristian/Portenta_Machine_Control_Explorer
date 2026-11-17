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

static const struct pin p_de     = PIN(gpioi, 13);  /* driver enable      */
static const struct pin p_re     = PIN(gpioi, 10);  /* receiver enable    */
static const struct pin p_en     = PIN(gpiog, 9);   /* transceiver on     */
static const struct pin p_rs232  = PIN(gpioa, 10);  /* protocol select    */
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
static atomic_t rx_errors;
static struct k_spinlock ring_lock;

/* --- the repeater ------------------------------------------------------- */
static uint8_t  rep_buf[PMC_485_TX_MAX];
static size_t   rep_len;
static uint32_t rep_period_ms;
static struct k_mutex rep_lock;

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
	(void)pin_set(&p_rs232, rs232 ? 1 : 0);
	(void)pin_set(&p_hf, full ? 1 : 0);
	(void)pin_set(&p_slew, cfg.slew ? 1 : 0);
	(void)pin_set(&p_term, cfg.term && !full ? 1 : 0);
	(void)pin_set(&p_fdterm, cfg.term && full ? 1 : 0);

	/* Idle: driver off, receiver on. */
	(void)pin_set(&p_de, 0);
	(void)pin_set(&p_re, 1);
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
	if (in->baud < 1200 || in->baud > 921600) {
		(void)snprintk(detail, cap,
			       "refused: baud %u outside 1200..921600\n",
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
	k_spin_unlock(&ring_lock, k);
	tx_total = 0;
	atomic_set(&rx_errors, 0);
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
	(void)pin_set(&p_re, cfg.echo ? 1 : 0);

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
	(void)pin_set(&p_re, 1);

	tx_total += len;
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
