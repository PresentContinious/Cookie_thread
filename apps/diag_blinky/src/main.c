/*
 * Minimal LED-blinker. No console, no USB, no networking. The point is
 * to confirm that the Dongle DFU + flash + boot + main() + GPIO path all
 * work end-to-end, isolated from the OpenThread SYS_INIT issue.
 *
 * Pattern:
 *   - both LEDs ON for 1 s (boot indicator)
 *   - then alternating red / green at 2 Hz forever
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

	/* Boot indicator: both LEDs solid for 1 s. */
	k_sleep(K_SECONDS(1));

	if (led_red.port)   gpio_pin_set_dt(&led_red, 0);
	if (led_green.port) gpio_pin_set_dt(&led_green, 0);

	while (1) {
		if (led_red.port) gpio_pin_set_dt(&led_red, 1);
		k_sleep(K_MSEC(250));
		if (led_red.port) gpio_pin_set_dt(&led_red, 0);

		if (led_green.port) gpio_pin_set_dt(&led_green, 1);
		k_sleep(K_MSEC(250));
		if (led_green.port) gpio_pin_set_dt(&led_green, 0);
	}
	return 0;
}
