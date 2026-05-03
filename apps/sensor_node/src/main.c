#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include "thread_setup.h"
#include "node_loop.h"
#include "sed_loop.h"

#include <cookie_proto/coap_client.h>

LOG_MODULE_REGISTER(sensor_node, LOG_LEVEL_INF);

static const struct gpio_dt_spec led_red =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_red), gpios, {0});
static const struct gpio_dt_spec led_green =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_green), gpios, {0});

static void bsp_init(void)
{
	if (led_red.port) {
		gpio_pin_configure_dt(&led_red,   GPIO_OUTPUT_INACTIVE);
	}
	if (led_green.port) {
		gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	}
}

int main(void)
{
	LOG_INF("sensor_node: board=%s profile=%s",
		CONFIG_BOARD,
#if defined(CONFIG_NODE_PROFILE_SED)
		"SED"
#else
		"AUTO"
#endif
	);

	bsp_init();

	if (cookie_thread_start() != 0) {
		LOG_ERR("OpenThread bring-up failed");
		return -1;
	}

	if (cookie_coap_init() != 0) {
		LOG_ERR("CoAP init failed");
		return -1;
	}

#if defined(CONFIG_NODE_PROFILE_SED)
	return cookie_sed_loop_run();
#else
	return cookie_node_loop_start();
#endif
}
