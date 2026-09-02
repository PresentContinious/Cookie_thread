/*
 * Board definition for the Cookie nRF V2.00 (CEI-UPM), RIOT port.
 *
 * Pin map taken from the Zephyr board definition (authoritative):
 *   LED red   D2  -> P1.10  (active high)
 *   LED green D3  -> P1.15  (active high)
 *   Button SW1    -> P1.13  (active low, pull-up)
 *   I2C0  SCL     -> P1.08    SDA -> P0.11   (SHTC3@0x70, ICM-20648@0x68)
 *   UART0 TX      -> P0.26    RX  -> P0.27   (115200)
 *
 * FIRST CUT — verify register-macro usage against the pinned RIOT release.
 */

#ifndef BOARD_H
#define BOARD_H

#include "cpu.h"
#include "periph/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LEDs are on GPIO port 1, active high. */
#define LED_PORT            (NRF_P1)

#define LED0_PIN            GPIO_PIN(1, 10)   /* red   D2 */
#define LED1_PIN            GPIO_PIN(1, 15)   /* green D3 */

#define LED0_MASK           (1UL << 10)
#define LED1_MASK           (1UL << 15)

#define LED0_ON             (LED_PORT->OUTSET = LED0_MASK)
#define LED0_OFF            (LED_PORT->OUTCLR = LED0_MASK)
#define LED0_TOGGLE         (LED_PORT->OUT  ^= LED0_MASK)

#define LED1_ON             (LED_PORT->OUTSET = LED1_MASK)
#define LED1_OFF            (LED_PORT->OUTCLR = LED1_MASK)
#define LED1_TOGGLE         (LED_PORT->OUT  ^= LED1_MASK)

/* User button SW1: P1.13, active low, internal pull-up. */
#define BTN0_PIN            GPIO_PIN(1, 13)
#define BTN0_MODE           GPIO_IN_PU

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
