/*
 * SAUL GPIO registry for the Cookie nRF V2.00 (RIOT port). Only compiled when
 * the saul_gpio module is pulled in; the minimal sensor node does not need it,
 * but it lets `saul` list the LEDs and the user button for bring-up checks.
 */

#ifndef GPIO_PARAMS_H
#define GPIO_PARAMS_H

#include "board.h"
#include "saul/periph.h"

#ifdef __cplusplus
extern "C" {
#endif

static const saul_gpio_params_t saul_gpio_params[] =
{
    {
        .name  = "LED red",
        .pin   = LED0_PIN,
        .mode  = GPIO_OUT,
        .flags = SAUL_GPIO_INIT_CLEAR,
    },
    {
        .name  = "LED green",
        .pin   = LED1_PIN,
        .mode  = GPIO_OUT,
        .flags = SAUL_GPIO_INIT_CLEAR,
    },
    {
        .name  = "Button SW1",
        .pin   = BTN0_PIN,
        .mode  = BTN0_MODE,
        .flags = SAUL_GPIO_INVERTED,
    },
};

#ifdef __cplusplus
}
#endif

#endif /* GPIO_PARAMS_H */
