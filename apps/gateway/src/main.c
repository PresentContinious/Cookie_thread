#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/openthread.h>

#include <openthread/dataset.h>
#include <openthread/instance.h>
#include <openthread/link.h>
#include <openthread/thread.h>

#include "coap_server.h"
#include "usb_print.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(gateway, LOG_LEVEL_INF);

static const struct gpio_dt_spec led_red =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_red), gpios, {0});
static const struct gpio_dt_spec led_green =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_green), gpios, {0});

static int gw_hex2bin(const char *s, uint8_t *out, size_t out_len)
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

static int thread_start(void)
{
	otInstance *inst = openthread_get_default_instance();
	if (!inst) {
		LOG_ERR("openthread_get_default_instance returned NULL");
		return -ENODEV;
	}

	otOperationalDataset ds;
	memset(&ds, 0, sizeof(ds));

	const char *name = CONFIG_COOKIE_THREAD_NETWORK_NAME;
	size_t nlen = strlen(name);
	if (nlen >= OT_NETWORK_NAME_MAX_SIZE) {
		nlen = OT_NETWORK_NAME_MAX_SIZE - 1;
	}
	memcpy(ds.mNetworkName.m8, name, nlen);
	ds.mNetworkName.m8[nlen] = '\0';
	ds.mComponents.mIsNetworkNamePresent = true;

	if (gw_hex2bin(CONFIG_COOKIE_THREAD_NETWORK_KEY,
		    ds.mNetworkKey.m8, OT_NETWORK_KEY_SIZE) < 0) {
		LOG_ERR("network key: bad hex");
		return -EINVAL;
	}
	ds.mComponents.mIsNetworkKeyPresent = true;

	ds.mPanId = (otPanId)CONFIG_COOKIE_THREAD_PAN_ID;
	ds.mComponents.mIsPanIdPresent = true;

	if (gw_hex2bin(CONFIG_COOKIE_THREAD_EXT_PAN_ID,
		    ds.mExtendedPanId.m8, OT_EXT_PAN_ID_SIZE) < 0) {
		LOG_ERR("ext PAN ID: bad hex");
		return -EINVAL;
	}
	ds.mComponents.mIsExtendedPanIdPresent = true;

	ds.mChannel = CONFIG_COOKIE_THREAD_CHANNEL;
	ds.mComponents.mIsChannelPresent = true;

	otError err = otDatasetSetActive(inst, &ds);
	if (err != OT_ERROR_NONE) {
		LOG_ERR("otDatasetSetActive: %d", err);
		return -EIO;
	}

	otLinkModeConfig mode = {
		.mRxOnWhenIdle = true,
		.mDeviceType   = true,
		.mNetworkData  = true,
	};
	(void)otThreadSetLinkMode(inst, mode);

	(void)otIp6SetEnabled(inst, true);
	(void)otThreadSetEnabled(inst, true);

	LOG_INF("OpenThread (FTD/gateway) started, channel=%u panid=0x%04x",
		(unsigned)CONFIG_COOKIE_THREAD_CHANNEL,
		(unsigned)CONFIG_COOKIE_THREAD_PAN_ID);
	return 0;
}

int main(void)
{
	LOG_INF("gateway: board=%s", CONFIG_BOARD);

	if (led_red.port) {
		gpio_pin_configure_dt(&led_red,   GPIO_OUTPUT_INACTIVE);
	}
	if (led_green.port) {
		gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	}

	gateway_usb_print_init();

	if (thread_start() != 0) {
		LOG_ERR("Thread bring-up failed");
		return -1;
	}

	if (gateway_coap_server_start() != 0) {
		LOG_ERR("CoAP server start failed");
		return -1;
	}

	while (1) {
		if (led_green.port) {
			gpio_pin_toggle_dt(&led_green);
		}
		k_sleep(K_MSEC(1000));
	}
	return 0;
}
