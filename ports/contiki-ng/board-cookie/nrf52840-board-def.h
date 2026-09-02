/*
 * Platform feature configuration for the Cookie nRF V2.00 (Contiki-NG).
 * Based on the in-tree nrf52840 "dk" board definition, retargeted to the
 * Cookie pinout: UART console on P0.26/P0.27, two status LEDs on port 1.
 */
#ifndef NRF52840_BOARD_DEF_H_
#define NRF52840_BOARD_DEF_H_

#include "boards.h"

#define PLATFORM_HAS_BATTERY                    0
#define PLATFORM_HAS_RADIO                      0
#define PLATFORM_HAS_TEMPERATURE                1

/* Two LEDs (red D2, green D3), both active-high on port 1. */
#define PLATFORM_HAS_LEDS                       1
#define LEDS_CONF_COUNT                         2
#define LEDS_CONF_RED                           1
#define LEDS_CONF_GREEN                         2

/* One user button (SW1, P1.13) wired into the Contiki button HAL. */
#define PLATFORM_HAS_BUTTON                     1
#define PLATFORM_SUPPORTS_BUTTON_HAL            1

#define PLATFORM_RTC_INSTANCE_ID                0
#define PLATFORM_TIMER_INSTANCE_ID              0

/* UART0 console pins (Cookie header). */
#define NRF_UART0_TX_PIN                        26   /* P0.26 */
#define NRF_UART0_RX_PIN                        27   /* P0.27 */

#endif /* NRF52840_BOARD_DEF_H_ */
