/*
 * LED driver table for the Cookie nRF V2.00 (Contiki-NG).
 * Red D2 = P1.10, green D3 = P1.15, both active-high (positive logic).
 */
#include "contiki.h"
#include "dev/leds.h"
#include "dev/gpio-hal.h"
#include "dev/gpio-hal-arch.h"

#include <stdbool.h>

const leds_t leds_arch_leds[] = {
    {
        .port = 1,
        .pin = 10,                 /* P1.10 red D2 */
        .negative_logic = false,
    },
    {
        .port = 1,
        .pin = 15,                 /* P1.15 green D3 */
        .negative_logic = false,
    },
};
