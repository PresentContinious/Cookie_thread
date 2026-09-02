/*
 * Board init for the Cookie nRF V2.00 (RIOT port).
 * Raises the GPIO supply rail for USB-powered operation, then configures the
 * two status LEDs as outputs, off. board_init() runs from the reset handler
 * (cpu/cortexm_common/vectors_cortexm.c), early enough for the UICR write.
 */

#include "board.h"
#include "cpu.h"
#include "periph/gpio.h"

/* The Cookie board is powered from USB-C through VDDH, so the nRF52840 runs
 * in high-voltage mode and the GPIO output stage follows UICR REGOUT0, which
 * defaults to 1.8 V — not enough to light the green LED and marginal for the
 * red one. Raise it to 3.0 V, exactly like the Zephyr board definition does.
 * The value lives in UICR: written once, then a reset applies it; later boots
 * (and reflashes that do not erase UICR) find it set and skip the write. */
static void cookie_regout0_init(void)
{
    if (((NRF_POWER->MAINREGSTATUS & POWER_MAINREGSTATUS_MAINREGSTATUS_Msk)
             == (POWER_MAINREGSTATUS_MAINREGSTATUS_High
                 << POWER_MAINREGSTATUS_MAINREGSTATUS_Pos)) &&
        ((NRF_UICR->REGOUT0 & UICR_REGOUT0_VOUT_Msk)
             == (UICR_REGOUT0_VOUT_DEFAULT << UICR_REGOUT0_VOUT_Pos))) {

        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

        NRF_UICR->REGOUT0 =
            (NRF_UICR->REGOUT0 & ~((uint32_t)UICR_REGOUT0_VOUT_Msk)) |
            (UICR_REGOUT0_VOUT_3V0 << UICR_REGOUT0_VOUT_Pos);

        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

        NVIC_SystemReset();
    }
}

void board_init(void)
{
    cookie_regout0_init();

    gpio_init(LED0_PIN, GPIO_OUT);
    gpio_init(LED1_PIN, GPIO_OUT);
    LED0_OFF;
    LED1_OFF;
}
