/**
 * Board support header for the Cookie nRF V2.00, used by the Nordic Open
 * Bootloader (nRF5 SDK 17.1.0 open_bootloader / USB serial DFU).
 *
 * Derived from components/boards/pca10059.h (nRF52840 Dongle), which is the
 * board the open_bootloader example targets. Two things differ on the Cookie
 * and both matter to the bootloader's user interface:
 *
 *   - The Dongle drives its LEDs LOW-side (LEDS_ACTIVE_STATE 0). The Cookie
 *     drives them HIGH-side, so LEDS_ACTIVE_STATE is 1 and LEDS_INV_MASK is 0.
 *     Getting this backwards leaves the DFU indication permanently on.
 *   - Both Cookie LEDs and the user button sit on port P1, not P0.
 *
 * Pin facts are taken from the board's Zephyr devicetree, which is the
 * authoritative description of the fabricated hardware.
 *
 * Selected by -DBOARD_CUSTOM (see components/boards/boards.h).
 */
#ifndef COOKIE_NRF_V200_BOARD_H
#define COOKIE_NRF_V200_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nrf_gpio.h"

/* --------------------------------------------------------------- LEDs ------
 * D2 red   = P1.10, active HIGH
 * D3 green = P1.15, active HIGH
 *
 * Ordering is not arbitrary. open_bootloader/main.c hard-codes BSP_LED_1 and
 * BSP_BOARD_LED_1, that is LED index 1, for every DFU indication (the softblink
 * breathing and the download-progress blink). On the nRF52840 Dongle that index
 * lands on the red channel of the RGB LED. Putting the red D2 at index 1 here
 * reproduces the same behaviour a user expects from a Dongle: red means the
 * device is sitting in DFU.
 */
#define LEDS_NUMBER    2

#define LED_RED        NRF_GPIO_PIN_MAP(1, 10)
#define LED_GREEN      NRF_GPIO_PIN_MAP(1, 15)

#define LED_1          LED_GREEN
#define LED_2          LED_RED

/* Cookie LEDs source current from the GPIO: a HIGH level lights the LED. */
#define LEDS_ACTIVE_STATE 1

#define LEDS_LIST { LED_1, LED_2 }

/* No LED is inverted. (The pca10059 header sets this to LEDS_MASK because its
 * LEDs are active low.) LEDS_MASK / LEDS_INV_MASK only describe port 0 anyway,
 * and both Cookie LEDs are on port 1, so the LEDS_ON/LEDS_OFF register macros
 * in boards.h must not be used on this board. bsp_board_led_on/off, which the
 * bootloader uses, work from LEDS_LIST and are port-agnostic.
 */
#define LEDS_INV_MASK  0

#define BSP_LED_0      LED_1    /* green D3, P1.15 */
#define BSP_LED_1      LED_2    /* red   D2, P1.10 - the DFU indicator */

/* --------------------------------------------------------------- Buttons ---
 * SW1 user button = P1.13, active LOW, no external pull, so the internal
 * pull-up is required. This is button index 0, the one the bootloader samples
 * at start-up for the "hold the button while plugging in USB" DFU entry.
 *
 * SW2 is wired to the dedicated reset pin (P0.18 by Nordic convention) and is
 * never a GPIO. It is not listed here.
 */
#define BUTTONS_NUMBER 1

#define BUTTON_1       NRF_GPIO_PIN_MAP(1, 13)
#define BUTTON_PULL    NRF_GPIO_PIN_PULLUP

#define BUTTONS_ACTIVE_STATE 0

#define BUTTONS_LIST { BUTTON_1 }

#define BSP_BUTTON_0   BUTTON_1

/* Pin the SoC uses as hardware reset once UICR PSELRESET[0..1] = 0x12.
 * Kept for parity with pca10059.h, which exposes it for self-reset helpers.
 */
#define BSP_SELF_PINRESET_PIN NRF_GPIO_PIN_MAP(0, 18)

/* ----------------------------------------------------------------- UART ----
 * The open_bootloader in its USB configuration never brings up a UART: its
 * transport is USB CDC ACM and its logging backend is disabled. These pins are
 * defined only so that any BSP or example header expecting the symbols still
 * compiles. They are the Cookie's unused P0.28/P0.29 debug pads and are never
 * configured by this firmware.
 */
#define RX_PIN_NUMBER  NRF_GPIO_PIN_MAP(0, 28)
#define TX_PIN_NUMBER  NRF_GPIO_PIN_MAP(0, 29)
#define CTS_PIN_NUMBER NRF_GPIO_PIN_MAP(0, 30)
#define RTS_PIN_NUMBER NRF_GPIO_PIN_MAP(0, 31)
#define HWFC           false

#ifdef __cplusplus
}
#endif

#endif // COOKIE_NRF_V200_BOARD_H
