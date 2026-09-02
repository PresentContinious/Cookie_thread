/*
 * Peripheral configuration for the Cookie nRF V2.00 (RIOT port).
 *
 * Only the buses the sensor node uses are described: UART0 for the console,
 * I2C for the SHTC3 / ICM-20648, and the default clock/timer/RTT config shared
 * by all nRF52 boards. The SAADC channels for the INA333 self-current path and
 * the VDDH/5 battery sense are left for the optional power-telemetry pass.
 *
 * FIRST CUT — verify the i2c_conf_t / uart_conf_t field names and the shared
 * cfg_* header paths against the pinned RIOT release.
 */

#ifndef PERIPH_CONF_H
#define PERIPH_CONF_H

#include "periph_cpu.h"
#include "cfg_clock_32_1.h"
#include "cfg_rtt_default.h"
#include "cfg_timer_default.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UART0: TX P0.26, RX P0.27 (Cookie console). */
static const uart_conf_t uart_config[] = {
    {
        .dev    = NRF_UARTE0,
        .rx_pin = GPIO_PIN(0, 27),
        .tx_pin = GPIO_PIN(0, 26),
#ifdef MODULE_PERIPH_UART_HW_FC
        .rts_pin = GPIO_UNDEF,
        .cts_pin = GPIO_UNDEF,
#endif
        .irqn   = UARTE0_UART0_IRQn,
    },
};
#define UART_0_ISR          (isr_uart0)
#define UART_NUMOF          ARRAY_SIZE(uart_config)

/* I2C0: SCL P1.08, SDA P0.11. Fast mode (400 kHz) to match the Zephyr config
 * (clock-frequency = I2C_BITRATE_FAST). SHTC3 sits at 0x70, ICM-20648 at 0x68.
 * RIOT maps the nRF TWIM to NRF_TWIM1 to avoid the SPI0/TWIM0 conflict. */
static const i2c_conf_t i2c_config[] = {
    {
        .dev   = NRF_TWIM1,
        .scl   = GPIO_PIN(1, 8),
        .sda   = GPIO_PIN(0, 11),
        .speed = I2C_SPEED_FAST,
    },
};
#define I2C_NUMOF           ARRAY_SIZE(i2c_config)

#ifdef __cplusplus
}
#endif

#endif /* PERIPH_CONF_H */
