/*
 * OpenThread bring-up: dataset from Kconfig credentials, profile-specific
 * link-mode, start, attach helpers.
 */

#include "thread_setup.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/openthread.h>

#include <openthread/dataset.h>
#include <openthread/instance.h>
#include <openthread/link.h>
#include <openthread/platform/radio.h>
#include <openthread/thread.h>
#if defined(CONFIG_OPENTHREAD_FTD)
#include <openthread/thread_ftd.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(thread_setup, LOG_LEVEL_INF);

static int cookie_hex2bin(const char *s, uint8_t *out, size_t out_len)
{
	if (strlen(s) != out_len * 2) {
		return -EINVAL;
	}
	for (size_t i = 0; i < out_len; i++) {
		char hi = s[2 * i];
		char lo = s[2 * i + 1];
		if (!isxdigit((unsigned char)hi) || !isxdigit((unsigned char)lo)) {
			return -EINVAL;
		}
		uint8_t hv = (hi <= '9') ? hi - '0'
				: (uint8_t)((tolower((unsigned char)hi) - 'a') + 10);
		uint8_t lv = (lo <= '9') ? lo - '0'
				: (uint8_t)((tolower((unsigned char)lo) - 'a') + 10);
		out[i] = (hv << 4) | lv;
	}
	return 0;
}

static int install_dataset(otInstance *inst)
{
	otOperationalDataset ds;
	memset(&ds, 0, sizeof(ds));

	/* Network name */
	const char *name = CONFIG_COOKIE_THREAD_NETWORK_NAME;
	size_t nlen = strlen(name);
	if (nlen >= OT_NETWORK_NAME_MAX_SIZE) {
		nlen = OT_NETWORK_NAME_MAX_SIZE - 1;
	}
	memcpy(ds.mNetworkName.m8, name, nlen);
	ds.mNetworkName.m8[nlen] = '\0';
	ds.mComponents.mIsNetworkNamePresent = true;

	/* Network key */
	if (cookie_hex2bin(CONFIG_COOKIE_THREAD_NETWORK_KEY,
		    ds.mNetworkKey.m8, OT_NETWORK_KEY_SIZE) < 0) {
		LOG_ERR("network key: bad hex");
		return -EINVAL;
	}
	ds.mComponents.mIsNetworkKeyPresent = true;

	/* PAN ID */
	ds.mPanId = (otPanId)CONFIG_COOKIE_THREAD_PAN_ID;
	ds.mComponents.mIsPanIdPresent = true;

	/* Extended PAN ID */
	if (cookie_hex2bin(CONFIG_COOKIE_THREAD_EXT_PAN_ID,
		    ds.mExtendedPanId.m8, OT_EXT_PAN_ID_SIZE) < 0) {
		LOG_ERR("ext PAN ID: bad hex");
		return -EINVAL;
	}
	ds.mComponents.mIsExtendedPanIdPresent = true;

	/* Channel */
	ds.mChannel = CONFIG_COOKIE_THREAD_CHANNEL;
	ds.mComponents.mIsChannelPresent = true;

	otError err = otDatasetSetActive(inst, &ds);
	if (err != OT_ERROR_NONE) {
		LOG_ERR("otDatasetSetActive: %d", err);
		return -EIO;
	}
	return 0;
}

int cookie_thread_start(void)
{
	otInstance *inst = openthread_get_default_instance();
	if (!inst) {
		LOG_ERR("openthread_get_default_instance returned NULL");
		return -ENODEV;
	}

	int rc = install_dataset(inst);
	if (rc < 0) {
		return rc;
	}

#if defined(CONFIG_NODE_PROFILE_SED)
	/* SED: rxOnWhenIdle = false, then ask Thread to behave as a sleepy
	 * end device. Poll period bound 4 min; supervision per spec §8.3. */
	otLinkModeConfig mode = {
		.mRxOnWhenIdle = false,
		.mDeviceType   = false,  /* MTD */
		.mNetworkData  = false,  /* full netdata not required for SED */
	};
	(void)otThreadSetLinkMode(inst, mode);
	(void)otLinkSetPollPeriod(inst, 240000);
#else
	otLinkModeConfig mode = {
		.mRxOnWhenIdle = true,
		.mDeviceType   = true,   /* FTD */
		.mNetworkData  = true,
	};
	(void)otThreadSetLinkMode(inst, mode);
#endif

	otError err = otIp6SetEnabled(inst, true);
	if (err != OT_ERROR_NONE) {
		LOG_ERR("otIp6SetEnabled: %d", err);
		return -EIO;
	}
	err = otThreadSetEnabled(inst, true);
	if (err != OT_ERROR_NONE) {
		LOG_ERR("otThreadSetEnabled: %d", err);
		return -EIO;
	}

	LOG_INF("OpenThread started, channel=%u panid=0x%04x",
		(unsigned)CONFIG_COOKIE_THREAD_CHANNEL,
		(unsigned)CONFIG_COOKIE_THREAD_PAN_ID);
	return 0;
}

int cookie_thread_wait_attached(k_timeout_t timeout)
{
	otInstance *inst = openthread_get_default_instance();
	int64_t deadline = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);
	while (k_uptime_get() < deadline) {
		otDeviceRole role = otThreadGetDeviceRole(inst);
		if (role >= OT_DEVICE_ROLE_CHILD) {
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
	otPlatRadioGetIeeeEui64(inst, eui64.m8);
	snprintf(src_out, 5, "%02x%02x", eui64.m8[6], eui64.m8[7]);
}

int8_t cookie_thread_parent_rssi(void)
{
	otInstance *inst = openthread_get_default_instance();
	int8_t avg;
	if (otThreadGetParentAverageRssi(inst, &avg) == OT_ERROR_NONE) {
		return avg;
	}
	return 0;
}

uint8_t cookie_thread_hops_to_leader(void)
{
	otInstance *inst = openthread_get_default_instance();
	otDeviceRole role = otThreadGetDeviceRole(inst);
	switch (role) {
	case OT_DEVICE_ROLE_LEADER:
		return 0;
	case OT_DEVICE_ROLE_ROUTER:
		return 1;  /* Routers are 1 hop from Leader by definition */
	case OT_DEVICE_ROLE_CHILD:
		return 2;  /* CHILD -> parent (Router or Leader) -> ... -> Leader */
	default:
		return 0;  /* DETACHED / DISABLED */
	}
}
