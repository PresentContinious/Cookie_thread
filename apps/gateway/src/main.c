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
#include <openthread/link.h>

#include <stdio.h>

#include "coap_server.h"
#include "usb_print.h"

LOG_MODULE_REGISTER(gateway, LOG_LEVEL_INF);

static void ext_last4(const otExtAddress *ext, char out[5])
{
	snprintf(out, 5, "%02x%02x", ext->m8[6], ext->m8[7]);
}

/* Emit the gateway's view of the mesh as a topology event the PC tool draws:
 *   {"event":"topology","tree":{"<gw>":["<neighbour>", ...]}}
 * Node IDs are the EUI-64 last two bytes, matching the `src` field of sensor
 * frames so the tree lines up with the live table. The gateway reports its
 * direct neighbours (one radio hop); for the small demo mesh that is the whole
 * network. printk goes straight to the console (CONFIG_LOG_PRINTK=n) so the
 * line is never truncated by log-buffer pressure. */
static void emit_topology(otInstance *inst)
{
	const otExtAddress *self = otLinkGetExtendedAddress(inst);
	char gw[5];
	ext_last4(self, gw);

	char line[256];
	int off = snprintf(line, sizeof(line),
			   "{\"event\":\"topology\",\"tree\":{\"%s\":[", gw);

	otNeighborInfoIterator it = OT_NEIGHBOR_INFO_ITERATOR_INIT;
	otNeighborInfo info;
	bool first = true;
	while (off < (int)sizeof(line) - 8 &&
	       otThreadGetNextNeighborInfo(inst, &it, &info) == OT_ERROR_NONE) {
		char nb[5];
		ext_last4(&info.mExtAddress, nb);
		off += snprintf(line + off, sizeof(line) - off,
				"%s\"%s\"", first ? "" : ",", nb);
		first = false;
	}
	snprintf(line + off, sizeof(line) - off, "]}}");
	printk("%s\n", line);
}

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

	/* Persistent OT state is kept across reboots so the gateway holds its
	 * partition and the mesh heals correctly per the Thread spec. The
	 * one-time clean wipe lives behind CONFIG_COOKIE_FACTORY_RESET_ON_BOOT
	 * for clearing stale datasets from earlier experiments. */
#if defined(CONFIG_COOKIE_FACTORY_RESET_ON_BOOT)
	{
		otInstance *inst = openthread_get_default_instance();
		if (inst) {
			otThreadSetEnabled(inst, false);
			otIp6SetEnabled(inst, false);
			(void)otInstanceErasePersistentInfo(inst);
			LOG_INF("OT persistent info erased (factory-reset on boot)");
		}
	}
#endif

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
				emit_topology(inst);
			}
		}
	}
	return 0;
}
