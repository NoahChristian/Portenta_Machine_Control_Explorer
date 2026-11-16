/*
 * Shell commands for the three analog inputs, and for the SYSCFG analog
 * switches that decide whether they mean anything.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Written by Noah Christian (C) 2026
 *
 * `pmc syscfg` can OPEN or CLOSE the switches at runtime, which exists for one
 * experiment: read all three channels with them closed, then with them open,
 * and compare. The shield opens them at PRE_KERNEL_1 (see pmc_syscfg.c), so
 * open is the normal state and closed is the deliberately broken one.
 *
 * Worth being concrete about what "broken" means here, because it is not
 * noise in the ordinary sense. With a switch closed, the _C analog pad is
 * electrically common with its conventional twin, and on this board those
 * twins are driven:
 *
 *     AI2 = PA_1C  <-> PA1  = eth_ref_clk, the 50 MHz RMII reference clock
 *     AI1 = PC_2C  <-> PC2  = spi2_miso
 *     AI0 = PC_3C  <-> PC3  = spi2_mosi
 *
 * and spi2 is the bus the Portenta datasheet calls SPI1, whose NSS is PI_0 -
 * MC_TC_CS_PIN, the carrier's thermocouple chip select. So a closed switch
 * couples a voltage measurement to the temperature bus in both directions.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/shell/shell.h>

#include <stm32_ll_bus.h>
#include <stm32_ll_system.h>

/*
 * The three channels, by INDEX into the shield's single zephyr,user node.
 *
 * Index order is the contract with the overlay: 0 = AI0 (adc3 ch1),
 * 1 = AI1 (adc3 ch0), 2 = AI2 (adc1 ch1). They are not all on one ADC, which
 * is deliberate - ADC3 carries two of them, so putting AI2 on ADC1 means one
 * conversion does not wait on the other.
 *
 * BUILD_ASSERT below is what keeps the two halves of that contract together:
 * add a channel to the overlay without adding its name here and the build
 * stops, rather than the shell quietly mislabeling a reading.
 */
static const struct adc_dt_spec ai[] = {
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),
};

BUILD_ASSERT(DT_PROP_LEN(DT_PATH(zephyr_user), io_channels) == 3,
	     "the shield overlay no longer declares exactly three analog "
	     "inputs; shell_analog.c names three and would mislabel them");

static const char *const ai_name[] = { "AI0", "AI1", "AI2" };
static const char *const ai_pad[]  = { "PC_3C", "PC_2C", "PA_1C" };
static const char *const ai_twin[] = {
	"PC3 / spi2_mosi (temperature bus)",
	"PC2 / spi2_miso (temperature bus)",
	"PA1 / eth_ref_clk (50 MHz RMII)",
};

static const uint32_t ai_switch[] = {
	LL_SYSCFG_ANALOG_SWITCH_PC3,
	LL_SYSCFG_ANALOG_SWITCH_PC2,
	LL_SYSCFG_ANALOG_SWITCH_PA1,
};

#define AI_ALL (LL_SYSCFG_ANALOG_SWITCH_PA1 | \
		LL_SYSCFG_ANALOG_SWITCH_PC2 | \
		LL_SYSCFG_ANALOG_SWITCH_PC3)

static int cmd_pmc_ai(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	const uint32_t pmcr = SYSCFG->PMCR & AI_ALL;

	shell_print(sh, "SYSCFG_PMCR analog-switch bits: 0x%08x of 0x%08x",
		    pmcr, (uint32_t)AI_ALL);
	shell_print(sh, "  a set bit means OPEN, i.e. the _C pad is isolated and usable");
	shell_print(sh, "");
	shell_print(sh, "%-5s %-7s %-7s %9s %9s  %s",
		    "chan", "pad", "switch", "raw", "mV", "twin pad if closed");

	for (size_t i = 0; i < ARRAY_SIZE(ai); i++) {
		int16_t buf = 0;
		struct adc_sequence seq = {
			.buffer = &buf,
			.buffer_size = sizeof(buf),
		};

		if (!adc_is_ready_dt(&ai[i])) {
			shell_print(sh, "%-5s %-7s %-7s %9s %9s  %s",
				    ai_name[i], ai_pad[i],
				    (pmcr & ai_switch[i]) ? "open" : "CLOSED",
				    "no dev", "-", ai_twin[i]);
			continue;
		}

		int rc = adc_channel_setup_dt(&ai[i]);

		if (rc != 0) {
			shell_error(sh, "%-5s channel setup failed (%d)", ai_name[i], rc);
			continue;
		}

		rc = adc_sequence_init_dt(&ai[i], &seq);
		if (rc != 0) {
			shell_error(sh, "%-5s sequence init failed (%d)", ai_name[i], rc);
			continue;
		}

		rc = adc_read_dt(&ai[i], &seq);
		if (rc != 0) {
			shell_error(sh, "%-5s read failed (%d)", ai_name[i], rc);
			continue;
		}

		int32_t mv = buf;

		(void)adc_raw_to_millivolts_dt(&ai[i], &mv);

		shell_print(sh, "%-5s %-7s %-7s %9d %9d  %s",
			    ai_name[i], ai_pad[i],
			    (pmcr & ai_switch[i]) ? "open" : "CLOSED",
			    buf, mv, ai_twin[i]);
	}

	shell_print(sh, "");
	shell_print(sh, "NOTE: these are RAW ADC readings at the MCU pad, not engineering");
	shell_print(sh, "units. Each channel's 0-10V / 4-20mA / NTC mode is a 4-bit switch");
	shell_print(sh, "network on four more GPIOs, none of which is driven yet, so the");
	shell_print(sh, "front end is in an undefined mode and these numbers mean nothing");
	shell_print(sh, "about any signal on the terminal block.");

	return 0;
}

static int cmd_pmc_syscfg(const struct shell *sh, size_t argc, char **argv)
{
	LL_APB4_GRP1_EnableClock(LL_APB4_GRP1_PERIPH_SYSCFG);

	if (argc == 2 && strcmp(argv[1], "open") == 0) {
		LL_SYSCFG_OpenAnalogSwitch(AI_ALL);
	} else if (argc == 2 && strcmp(argv[1], "close") == 0) {
		/*
		 * Deliberately available, because an experiment that can only
		 * be run in the good direction proves nothing. Closing these
		 * is what the silicon does at reset, so this is a return to
		 * the default rather than damage - but it does tie the analog
		 * front end to a running 50 MHz clock and to the temperature
		 * SPI bus, so do not leave it here.
		 */
		LL_SYSCFG_CloseAnalogSwitch(AI_ALL);
		shell_warn(sh, "switches CLOSED: AI0/AI1 are now common with the");
		shell_warn(sh, "temperature SPI bus and AI2 with the Ethernet clock.");
		shell_warn(sh, "Run `pmc syscfg open` when the comparison is done.");
	} else if (argc != 1) {
		shell_print(sh, "usage: pmc syscfg [open|close]");
		return -EINVAL;
	}

	const uint32_t pmcr = SYSCFG->PMCR & AI_ALL;

	shell_print(sh, "SYSCFG_PMCR analog-switch bits: 0x%08x of 0x%08x (%s)",
		    pmcr, (uint32_t)AI_ALL,
		    pmcr == AI_ALL ? "all open" : (pmcr == 0 ? "all closed" : "mixed"));
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_pmc_analog,
	SHELL_CMD(ai, NULL, "Read the three analog inputs and the switch state.",
		  cmd_pmc_ai),
	SHELL_CMD_ARG(syscfg, NULL, "Show or set the SYSCFG analog switches.",
		      cmd_pmc_syscfg, 1, 1),
	SHELL_SUBCMD_SET_END
);

/*
 * Registered as its own root command rather than folded into `pmc`, because
 * SHELL_STATIC_SUBCMD_SET_CREATE cannot be extended from a second translation
 * unit - the set is one static array. `pmc parts` lives in main.c; these live
 * here; so this is `pmca` until there is a reason to centralize the set.
 */
SHELL_CMD_REGISTER(pmca, &sub_pmc_analog,
		   "Portenta Machine Control analog inputs", NULL);
