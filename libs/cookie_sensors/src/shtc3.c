/*
 * SHTC3 wrapper.
 *
 * The Zephyr SHTC3 driver issues SLEEP automatically between samples
 * when the application drives one-shot reads (no continuous mode is
 * configured in DT). So sample_fetch + channel_get is enough; the
 * sensor parks itself at ~0.7 µA after each transaction completes.
 */

#include "cookie_sensors/shtc3.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(cookie_shtc3, CONFIG_COOKIE_SENSORS_LOG_LEVEL);

#define SHTC3_NODE DT_ALIAS(temp_humid)

#if DT_NODE_EXISTS(SHTC3_NODE)
static const struct device *const shtc3 = DEVICE_DT_GET(SHTC3_NODE);
#else
static const struct device *const shtc3 = NULL;
#endif

bool cookie_shtc3_present(void)
{
	if (!shtc3) {
		return false;
	}
	if (!device_is_ready(shtc3)) {
		LOG_DBG("SHTC3 device not ready");
		return false;
	}
	return true;
}

int cookie_shtc3_read(float *temp_c, float *humid_pct)
{
	if (!cookie_shtc3_present()) {
		return -ENODEV;
	}
	if (!temp_c || !humid_pct) {
		return -EINVAL;
	}

	int rc = sensor_sample_fetch(shtc3);
	if (rc < 0) {
		LOG_WRN("sample_fetch: %d", rc);
		return rc;
	}

	struct sensor_value v_t, v_h;
	rc = sensor_channel_get(shtc3, SENSOR_CHAN_AMBIENT_TEMP, &v_t);
	if (rc < 0) {
		return rc;
	}
	rc = sensor_channel_get(shtc3, SENSOR_CHAN_HUMIDITY, &v_h);
	if (rc < 0) {
		return rc;
	}

	*temp_c    = (float)sensor_value_to_double(&v_t);
	*humid_pct = (float)sensor_value_to_double(&v_h);
	return 0;
}
