/**
 * Polarity shim for the Cookie nRF V2.00 bootloader build.
 *
 * components/libraries/led_softblink/led_softblink.h hard-codes
 *
 *     #define LED_SB_INIT_PARAMS_ACTIVE_HIGH  false
 *
 * outside of sdk_config.h, and open_bootloader/main.c takes it through
 * LED_SB_INIT_DEFAULT_PARAMS() without overriding it. That default is correct
 * for the nRF52840 Dongle, whose LEDs are wired low-side. The Cookie drives its
 * LEDs high-side, so the stock value inverts the breathing envelope: the LED
 * would sit lit and dip dark instead of pulsing up from off.
 *
 * This header is placed ahead of the SDK's led_softblink directory in the
 * include path. It pulls in the real header with #include_next, then flips the
 * one macro. LED_SB_INIT_DEFAULT_PARAMS is only expanded at its use site in
 * main.c, which is compiled after this substitution, so the override takes
 * effect without touching the SDK tree.
 *
 * Nothing else about the module changes. The struct layout, the API, and
 * led_softblink.c itself are the SDK's.
 */
#include_next <led_softblink.h>

#undef  LED_SB_INIT_PARAMS_ACTIVE_HIGH
#define LED_SB_INIT_PARAMS_ACTIVE_HIGH  true
