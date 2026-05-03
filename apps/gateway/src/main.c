/*
 * Cookie gateway entry point.
 *
 * OpenThread is auto-started by the Zephyr glue (no MANUAL_START), with the
 * dataset taken from CONFIG_OPENTHREAD_NETWORK_NAME / NETWORKKEY / PANID /
 * XPANID / CHANNEL in prj.conf. Once the L2 is up, this module starts the
 * Zephyr CoAP server bound on UDP 5683 and toggles the green LED as a
 * heartbeat. Frame payloads are passed through verbatim to USB-CDC.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/openthread.h>

#include <openthread/instance.h>
#include <openthread/thread.h>

#include "coap_server.h"
#include "usb_print.h"

LOG_MODULE_REGISTER(gateway, LOG_LEVEL_INF);

static const struct gpio_dt_spec led_red =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_red), gpios, {0});
static const struct gpio_dt_spec led_green =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_green), gpios, {0});

static void on_thread_state_changed(otChangedFlags flags, void *user_data)
{
	ARG_UNUSED(user_data);

	if (flags & OT_CHANGED_THREAD_ROLE) {
		otDeviceRole role = otThreadGetDeviceRole(openthread_get_default_instance());
		LOG_INF("Thread role: %s", otThreadDeviceRoleToString(role));
		if (led_red.port) {
			bool attached = (role == OT_DEVICE_ROLE_LEADER ||
					 role == OT_DEVICE_ROLE_ROUTER ||
					 role == OT_DEVICE_ROLE_CHILD);
			gpio_pin_set_dt(&led_red, attached ? 1 : 0);
		}
	}
}

static struct openthread_state_changed_callback ot_state_cb = {
	.otCallback = on_thread_state_changed,
};

int main(void)
{
	LOG_INF("Cookie gateway: board=%s", CONFIG_BOARD);

	if (led_red.port) {
		gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	}
	if (led_green.port) {
		gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	}

	gateway_usb_print_init();

	openthread_state_changed_callback_register(&ot_state_cb);

	int orc = openthread_run();
	if (orc) {
		LOG_ERR("openthread_run: %d", orc);
		return -1;
	}

	if (gateway_coap_server_start() != 0) {
		LOG_ERR("CoAP server start failed");
		return -1;
	}

	uint32_t tick = 0;
	while (1) {
		if (led_green.port) {
			gpio_pin_toggle_dt(&led_green);
		}
		k_sleep(K_MSEC(1000));
		if ((++tick % 5) == 0) {
			otInstance *inst = openthread_get_default_instance();
			if (inst) {
				LOG_INF("heartbeat tick=%u role=%s",
					tick,
					otThreadDeviceRoleToString(otThreadGetDeviceRole(inst)));
			}
		}
	}
	return 0;
}
