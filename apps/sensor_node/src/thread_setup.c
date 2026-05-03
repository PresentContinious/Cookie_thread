/*
 * OpenThread helpers.
 *
 * The OT stack auto-starts in SYS_INIT (no MANUAL_START), with the dataset
 * loaded by Zephyr glue from CONFIG_OPENTHREAD_NETWORK_NAME / NETWORKKEY /
 * PANID / XPANID / CHANNEL. SED-specific link-mode (rxOnWhenIdle=false,
 * poll period) is applied automatically by the glue when
 * CONFIG_OPENTHREAD_MTD_SED=y.
 *
 * This file therefore exposes only read-side helpers — role string, EUI-64
 * source short form, parent RSSI, hop estimate — and a wait-attached
 * convenience used by main() before opening the CoAP socket.
 */

#include "thread_setup.h"

#include <zephyr/kernel.h>
#include <zephyr/net/openthread.h>

#include <openthread/instance.h>
#include <openthread/link.h>
#include <openthread/platform/radio.h>
#include <openthread/thread.h>

#include <stdio.h>

int cookie_thread_wait_attached(k_timeout_t timeout)
{
	otInstance *inst = openthread_get_default_instance();
	int64_t deadline = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);
	while (k_uptime_get() < deadline) {
		if (inst != NULL &&
		    otThreadGetDeviceRole(inst) >= OT_DEVICE_ROLE_CHILD) {
			return 0;
		}
		k_sleep(K_MSEC(200));
	}
	return -ETIMEDOUT;
}

const char *cookie_thread_role_str(void)
{
#if defined(CONFIG_NODE_PROFILE_SED)
	return "SED";
#else
	otInstance *inst = openthread_get_default_instance();
	if (!inst) {
		return "UNKNOWN";
	}
	switch (otThreadGetDeviceRole(inst)) {
	case OT_DEVICE_ROLE_LEADER:   return "LEADER";
	case OT_DEVICE_ROLE_ROUTER:   return "ROUTER";
	case OT_DEVICE_ROLE_CHILD:    return "CHILD";
	case OT_DEVICE_ROLE_DETACHED: return "DETACHED";
	case OT_DEVICE_ROLE_DISABLED: return "DISABLED";
	default:                      return "UNKNOWN";
	}
#endif
}

void cookie_thread_format_src(char src_out[5])
{
	otInstance *inst = openthread_get_default_instance();
	otExtAddress eui64;
	if (!inst) {
		src_out[0] = '\0';
		return;
	}
	otPlatRadioGetIeeeEui64(inst, eui64.m8);
	snprintf(src_out, 5, "%02x%02x", eui64.m8[6], eui64.m8[7]);
}

int8_t cookie_thread_parent_rssi(void)
{
	otInstance *inst = openthread_get_default_instance();
	if (!inst) {
		return 0;
	}
	int8_t avg;
	if (otThreadGetParentAverageRssi(inst, &avg) == OT_ERROR_NONE) {
		return avg;
	}
	return 0;
}

uint8_t cookie_thread_hops_to_leader(void)
{
	otInstance *inst = openthread_get_default_instance();
	if (!inst) {
		return 0;
	}
	switch (otThreadGetDeviceRole(inst)) {
	case OT_DEVICE_ROLE_LEADER:
		return 0;
	case OT_DEVICE_ROLE_ROUTER:
		return 1;
	case OT_DEVICE_ROLE_CHILD:
		return 2;
	default:
		return 0;
	}
}
