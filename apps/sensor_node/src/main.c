/*
 * Sensor-node entry point.
 *
 * OpenThread auto-starts via Zephyr glue (SYS_INIT) with the dataset
 * baked into prj.conf. main() blinks the LED, waits for first attach,
 * brings up CoAP, and dispatches to the AUTO or SED loop.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/net/openthread.h>
#include <openthread/instance.h>
#include <openthread/thread.h>

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

	/* DIAG: wipe persistent OT state on every boot so the Kconfig
	 * dataset (channel/key/PAN) takes effect fresh. Without this, two
	 * Dongles refuse to merge because each loads its own saved partition
	 * from NVS and remains Leader of its own partition. */
	{
		otInstance *inst_for_reset = openthread_get_default_instance();
		if (inst_for_reset) {
			otThreadSetEnabled(inst_for_reset, false);
			otIp6SetEnabled(inst_for_reset, false);
			(void)otInstanceErasePersistentInfo(inst_for_reset);
			LOG_INF("OT factory-reset on boot");
		}
	}

	/* MANUAL_START defaults to y in this NCS configuration, so we run
	 * OT explicitly. The Kconfig dataset has already been loaded by
	 * openthread_init() in SYS_INIT; openthread_run() brings the
	 * interface up and starts the protocol. */
	int orc = openthread_run();
	if (orc) {
		LOG_ERR("openthread_run: %d", orc);
		return -1;
	}

	/* Wait for first attach. Up to 60 s. If we time out we still
	 * proceed — first CoAP push will trigger discovery once attachment
	 * finally happens. */
	(void)cookie_thread_wait_attached(K_SECONDS(60));

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
