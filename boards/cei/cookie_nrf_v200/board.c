/*
 * Cookie nRF V2.00 board early init.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <hal/nrf_power.h>

void board_early_init_hook(void)
{
	/* The Cookie board is always powered from USB-C (the SWD probe does not
	 * supply the whole board), so the nRF52840 runs in high-voltage mode.
	 * In that mode the GPIO output stage (REGOUT0) defaults to 1.8 V, which
	 * is not enough to turn the green LED on and leaves the red one dim.
	 * Raise REGOUT0 to 3.0 V. The value lives in UICR, so it is written once
	 * and a reset is issued for it to take effect; later boots find it
	 * already set and skip the write. */
	if ((nrf_power_mainregstatus_get(NRF_POWER) ==
	     NRF_POWER_MAINREGSTATUS_HIGH) &&
	    ((NRF_UICR->REGOUT0 & UICR_REGOUT0_VOUT_Msk) ==
	     (UICR_REGOUT0_VOUT_DEFAULT << UICR_REGOUT0_VOUT_Pos))) {

		NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
			;
		}

		NRF_UICR->REGOUT0 =
		    (NRF_UICR->REGOUT0 & ~((uint32_t)UICR_REGOUT0_VOUT_Msk)) |
		    (UICR_REGOUT0_VOUT_3V0 << UICR_REGOUT0_VOUT_Pos);

		NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
		while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
			;
		}

		/* A reset is required for the new REGOUT0 value to take effect. */
		NVIC_SystemReset();
	}
}
