#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(sensor_node, LOG_LEVEL_INF);

static const struct gpio_dt_spec led_red =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_red), gpios);
static const struct gpio_dt_spec led_green =
	GPIO_DT_SPEC_GET(DT_ALIAS(led_green), gpios);

static const char *profile_name(void)
{
#if defined(CONFIG_NODE_PROFILE_FTD)
	return "FTD";
#elif defined(CONFIG_NODE_PROFILE_MED)
	return "MED";
#elif defined(CONFIG_NODE_PROFILE_SED)
	return "SED";
#else
	return "UNKNOWN";
#endif
}

int main(void)
{
	LOG_INF("skeleton: board=%s profile=%s interval=%ds",
		CONFIG_BOARD, profile_name(), CONFIG_NODE_REPORT_INTERVAL_SEC);

	gpio_pin_configure_dt(&led_red,   GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);

	while (1) {
		gpio_pin_toggle_dt(&led_red);
		k_sleep(K_MSEC(500));
		gpio_pin_toggle_dt(&led_green);
		k_sleep(K_MSEC(500));
	}
	return 0;
}
