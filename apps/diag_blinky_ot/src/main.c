/*
 * Same blinker as diag_blinky but with OpenThread linked in (manual
 * start). If LEDs blink, OT SYS_INIT is fine. If LEDs do NOT blink, OT
 * init crashes on Dongle and the symptom is independent of any
 * application-level config.
 *
 * Pattern is intentionally distinct from diag_blinky so we can tell which
 * image is actually flashed: 200 ms both ON, then alternating at 4 Hz
 * (~125 ms each).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec led_red =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_red), gpios, {0});
static const struct gpio_dt_spec led_green =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_green), gpios, {0});

int main(void)
{
	if (led_red.port) {
		gpio_pin_configure_dt(&led_red,   GPIO_OUTPUT_ACTIVE);
	}
	if (led_green.port) {
		gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_ACTIVE);
	}
	k_sleep(K_MSEC(200));
	if (led_red.port)   gpio_pin_set_dt(&led_red, 0);
	if (led_green.port) gpio_pin_set_dt(&led_green, 0);

	while (1) {
		if (led_red.port)   gpio_pin_set_dt(&led_red, 1);
		if (led_green.port) gpio_pin_set_dt(&led_green, 0);
		k_sleep(K_MSEC(125));
		if (led_red.port)   gpio_pin_set_dt(&led_red, 0);
		if (led_green.port) gpio_pin_set_dt(&led_green, 1);
		k_sleep(K_MSEC(125));
	}
	return 0;
}
