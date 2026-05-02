#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(gateway, LOG_LEVEL_INF);

static const struct gpio_dt_spec led_red =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_red), gpios);
static const struct gpio_dt_spec led_green =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_green), gpios);

int main(void)
{
	LOG_INF("gateway skeleton: board=%s", CONFIG_BOARD);

	gpio_pin_configure_dt(&led_red,   GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);

	while (1) {
		gpio_pin_toggle_dt(&led_green);
		k_sleep(K_MSEC(250));
		gpio_pin_toggle_dt(&led_red);
		k_sleep(K_MSEC(250));
	}
	return 0;
}
