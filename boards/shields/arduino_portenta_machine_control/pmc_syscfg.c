/*
 * Open the STM32H7 SYSCFG analog switches, so the carrier's three analog
 * inputs are actually analog inputs.
 *
 * Copyright (c) 2026 Noah Christian
 * SPDX-License-Identifier: Apache-2.0
 *
 * WHAT GOES WRONG WITHOUT THIS, and why nothing reports it.
 *
 * The STM32H747 brings four analog pads out twice: PA0/PA0_C, PA1/PA1_C,
 * PC2/PC2_C and PC3/PC3_C. Each pair is joined by an analog switch controlled
 * by the matching PxySO bit in SYSCFG_PMCR, and SYSCFG_PMCR resets to 0, which
 * means every one of those switches is CLOSED. Closed joins the two pads: the
 * _C pad is then electrically common with its conventional twin instead of
 * being a dedicated direct ADC input.
 *
 * The Portenta Machine Control wires its three analog inputs to the _C pads:
 *
 *     AI0 = PC_3C      AI1 = PC_2C      AI2 = PA_1C
 *
 * and the Portenta H7 board definition drives all three conventional twins:
 *
 *     arduino_portenta_h7-common.dtsi:217   &mac  pinctrl-0 = <&eth_ref_clk_pa1 ...>
 *     arduino_portenta_h7-common.dtsi:126   &spi2 pinctrl-0 = <... &spi2_miso_pc2 &spi2_mosi_pc3>
 *
 * So with the switches at their reset value:
 *
 *   - AI2 is tied to PA1, which carries the 50 MHz RMII reference clock. The
 *     Ethernet driver is `default y` on this board whenever networking is on.
 *   - AI0 and AI1 are tied to PC3 and PC2, which are MOSI and MISO of the SPI
 *     bus the datasheet calls SPI1. That is the carrier's TEMPERATURE bus - its
 *     NSS is PI_0, which pins_mc.h calls MC_TC_CS_PIN, the thermocouple chip
 *     select.
 *
 * Both directions are broken, which is the part that makes this expensive to
 * diagnose. An analog reading picks up a 50 MHz clock or SPI traffic to the
 * MAX31865/MAX31855, and the analog front end simultaneously loads a clock and
 * a data line that something else is relying on. Reading a temperature can
 * disturb a voltage measurement and vice versa, intermittently, with every
 * individual subsystem reporting success.
 *
 * NOTHING IN ZEPHYR DOES THIS. LL_SYSCFG_OpenAnalogSwitch() is defined in
 * modules/hal/stm32/stm32cube/stm32h7xx/drivers/include/stm32h7xx_ll_system.h,
 * and a search of soc/st, drivers/adc and drivers/pinctrl - and then of the
 * whole tree and every module - finds no caller for the H7. The only other hit
 * is the unrelated stm32mp1 HAL header. So this is not a setting somebody
 * forgot to turn on; there is no code path that would ever turn it on.
 *
 * WHY IT LIVES IN THE SHIELD. The pads are the carrier's wiring, not any one
 * application's choice, so a second application built against this shield must
 * not have to remember. PRE_KERNEL_1 is early enough that no driver has read an
 * ADC or clocked that SPI bus yet.
 *
 * PA0_C IS DELIBERATELY LEFT ALONE. The carrier uses only three analog inputs,
 * and opening a switch isolates the _C pad from its twin - so opening PA0's
 * switch would change the behavior of PA0 for no reason. Only the three pads
 * this board actually wires are touched.
 *
 * STILL TO MEASURE. That the switches are closed at reset is from ST's AN5419
 * and the reference manual, and the pad conflict above is read out of the board
 * files - neither has been observed on this bench yet. docs/MEASURED.md records
 * the experiment: read all three channels with the switches closed and then
 * open, and compare. A closed PA1 switch should show as noise correlated with
 * Ethernet traffic.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>

#include <stm32_ll_bus.h>
#include <stm32_ll_system.h>

LOG_MODULE_REGISTER(pmc_syscfg, LOG_LEVEL_INF);

static int pmc_syscfg_open_analog_switches(void)
{
	/*
	 * SYSCFG sits on APB4 and its clock gate is NOT on by default. Writing
	 * PMCR with the peripheral unclocked does not fault - the write is
	 * simply dropped, the read-back shows the reset value, and the switches
	 * stay closed. That is the same shape of silent failure this whole file
	 * exists to prevent, so the clock is enabled here rather than assumed.
	 */
	LL_APB4_GRP1_EnableClock(LL_APB4_GRP1_PERIPH_SYSCFG);

	/*
	 * PA1 IS A CHOICE, AND THE OTHER TWO ARE NOT.
	 *
	 * PC2 and PC3 carry AI0 and AI1 against the temperature SPI bus, and
	 * nothing wants that switch closed. PA1 is different: AI2 and the
	 * Ethernet RMII reference clock share that one pad pair, so the switch
	 * cannot serve both at once if the clock arrives on the PA1_C ball.
	 * CONFIG_PMC_OPEN_PA1_ANALOG_SWITCH=n exists to test exactly that - see
	 * the Kconfig help and docs/MEASURED.md.
	 */
	uint32_t open = LL_SYSCFG_ANALOG_SWITCH_PC2 |
			LL_SYSCFG_ANALOG_SWITCH_PC3;
	uint32_t shut = 0;

	/*
	 * BOTH DIRECTIONS, EXPLICITLY. Not opening a switch is not the same as
	 * closing it: LL_SYSCFG_OpenAnalogSwitch is a SET_BIT, so the =n path
	 * used to leave PA1SO at whatever it already held - and it holds 1
	 * across a soft reset. The =n build was a no-op, which made the PA1
	 * experiment look performed when it had never run. docs/MEASURED.md.
	 */
	if (IS_ENABLED(CONFIG_PMC_OPEN_PA1_ANALOG_SWITCH)) {
		open |= LL_SYSCFG_ANALOG_SWITCH_PA1;
	} else {
		shut |= LL_SYSCFG_ANALOG_SWITCH_PA1;
	}

	LL_SYSCFG_OpenAnalogSwitch(open);
	if (shut != 0U) {
		LL_SYSCFG_CloseAnalogSwitch(shut);
	}

	/*
	 * Read back and complain, rather than trusting the write.
	 *
	 * There is no LL getter for these bits, so the register is read
	 * directly. A failure here is not recoverable from software and must
	 * not be silent: every analog reading afterwards would be a plausible
	 * number measured through a 50 MHz clock.
	 */
	const uint32_t want = open;
	const uint32_t got = SYSCFG->PMCR & want;

	/* A switch asked to close and still open is the same class of fault. */
	if ((SYSCFG->PMCR & shut) != 0U) {
		LOG_ERR("SYSCFG analog switch did not close: PMCR 0x%08x, wanted 0x%08x clear",
			(unsigned int)SYSCFG->PMCR, (unsigned int)shut);
		return -EIO;
	}

	if (got != want) {
		LOG_ERR("SYSCFG analog switches did not open: PMCR masked 0x%08x, wanted 0x%08x",
			got, want);
		LOG_ERR("AI0/AI1 are shorted to the temperature SPI bus%s; analog "
			"readings are meaningless",
			IS_ENABLED(CONFIG_PMC_OPEN_PA1_ANALOG_SWITCH)
				? " and AI2 to the Ethernet RMII clock"
				: "");
		return -EIO;
	}

	LOG_INF("SYSCFG analog switches open: AI0=PC3_C AI1=PC2_C AI2=PA1_C isolated "
		"from PC3/PC2/PA1");
	return 0;
}

/*
 * PRE_KERNEL_1, and early within it: before the ADC driver, before SPI, and
 * before the Ethernet MAC starts driving PA1.
 */
SYS_INIT(pmc_syscfg_open_analog_switches, PRE_KERNEL_1, 0);
