/*
 * Button table for the Cookie nRF V2.00 (Contiki-NG). The board has one user
 * button, SW1 on P1.13, active-low with a pull-up. Contiki's
 * button-hal is compiled by the platform, so this provides the required
 * button_hal_buttons[] / button_hal_button_count symbols.
 */
#include "contiki.h"
#include "boards.h"
#include "dev/gpio-hal.h"
#include "dev/button-hal.h"

/* name, description, port, pin, pull, unique-id, negative-logic.
 * SW1 = P1.13 (port 1, pin 13), active low. */
BUTTON_HAL_BUTTON(btn_user, "User SW1", 1, 13,
                  GPIO_HAL_PIN_CFG_PULL_UP, BUTTON_HAL_ID_BUTTON_ZERO, true);

BUTTON_HAL_BUTTONS(&btn_user);
