/*
 * cookie_power implementation: ADC burst on the INA333 channel + one-shot
 * battery read on the internal VDD divider.
 *
 * Channels are picked up from DT through the conventional zephyr,user node:
 *
 *     / {
 *         zephyr,user {
 *             io-channels      = <&adc 6>, <&adc 7>;
 *             io-channel-names = "self_current", "vbat";
 *         };
 *     };
 *
 * Boards without that node compile the no-op fallback at the bottom of the
 * file so sensor_node binaries link cleanly on the Dongle.
 */

#include "cookie_power/power.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(cookie_power, CONFIG_COOKIE_POWER_LOG_LEVEL);

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#if DT_NODE_HAS_PROP(ZEPHYR_USER_NODE, io_channels)
#define COOKIE_POWER_AVAILABLE 1

static const struct adc_dt_spec self_i_spec =
	ADC_DT_SPEC_GET_BY_NAME(ZEPHYR_USER_NODE, self_current);
static const struct adc_dt_spec vbat_spec   =
	ADC_DT_SPEC_GET_BY_NAME(ZEPHYR_USER_NODE, vbat);

#else
#define COOKIE_POWER_AVAILABLE 0
#endif

#if COOKIE_POWER_AVAILABLE

static bool initialised;

static int initialise_if_needed(void)
{
	if (initialised) {
		return 0;
	}

	if (!adc_is_ready_dt(&self_i_spec)) {
		LOG_DBG("ADC not ready (self-current)");
		return -ENODEV;
	}
	if (!adc_is_ready_dt(&vbat_spec)) {
		LOG_DBG("ADC not ready (vbat)");
		return -ENODEV;
	}

	int rc = adc_channel_setup_dt(&self_i_spec);
	if (rc < 0) {
		LOG_WRN("self_current channel setup: %d", rc);
		return rc;
	}
	rc = adc_channel_setup_dt(&vbat_spec);
	if (rc < 0) {
		LOG_WRN("vbat channel setup: %d", rc);
		return rc;
	}

	initialised = true;
	return 0;
}

bool cookie_power_present(void)
{
	return initialise_if_needed() == 0;
}

/* Convert one signed 12-bit ADC reading on the self-current channel (AIN6,
 * the INA333 output) to load current in milliamperes:
 *
 *   v_amp_mV  = raw * 0.8789 mV/LSB            (gain 1/6, ref 0.6 V, 12-bit)
 *   i_load_mA = v_amp_mV / (R_shunt_ohm * gain_INA)        (mV/Ohm = mA)
 *
 * The 0.8789 mV/LSB factor comes from this channel's own DT configuration and
 * is kept separate from the VBAT channel (which reads VDDH/5 on a 5x larger
 * scale). Keeping the two calibrations independent means changing one never
 * silently shifts the other. */
static float raw_to_milliamps(int16_t raw)
{
	if (raw < 0) {
		raw = 0;  /* small negative excursions on a single-ended channel */
	}
	const float uv_per_lsb_input    =
		879.0f;  /* AIN6: gain 1/6, ref 0.6V, 12-bit -> 3600/4096 = 0.8789 mV/LSB = 879 uV/LSB. Self-current scale, independent of the VBAT channel. */
	const float v_amp_uv            = (float)raw * uv_per_lsb_input;
	const float v_amp_mv            = v_amp_uv / 1000.0f;
	const float r_shunt_ohm         = (float)CONFIG_COOKIE_POWER_SHUNT_MOHM / 1000.0f;
	const float gain                = (float)CONFIG_COOKIE_POWER_INA333_GAIN;
	if (r_shunt_ohm <= 0.0f || gain <= 0.0f) {
		return 0.0f;
	}
	return v_amp_mv / (r_shunt_ohm * gain);  /* mV / Ohm = mA */
}

static int read_one(const struct adc_dt_spec *spec, int16_t *raw_out)
{
	int16_t raw = 0;
	struct adc_sequence seq = {
		.buffer      = &raw,
		.buffer_size = sizeof(raw),
	};
	int rc = adc_sequence_init_dt(spec, &seq);
	if (rc < 0) {
		return rc;
	}
	rc = adc_read_dt(spec, &seq);
	if (rc < 0) {
		return rc;
	}
	*raw_out = raw;
	return 0;
}

static int read_vbat_mv(uint16_t *out_mv)
{
	int16_t raw;
	int rc = read_one(&vbat_spec, &raw);
	if (rc < 0) {
		return rc;
	}
	if (raw < 0) {
		raw = 0;
	}
	uint32_t mv =
		((uint32_t)raw * (uint32_t)CONFIG_COOKIE_POWER_VBAT_MV_PER_LSB_X1000)
		/ 1000U;
	if (mv > UINT16_MAX) {
		mv = UINT16_MAX;
	}
	*out_mv = (uint16_t)mv;
	return 0;
}

int cookie_power_sample_burst(struct cookie_power_sample *out)
{
	if (!out) {
		return -EINVAL;
	}

	int rc = initialise_if_needed();
	if (rc < 0) {
		return rc;
	}

	float sum_ma  = 0.0f;
	float peak_ma = 0.0f;
	const int n = CONFIG_COOKIE_POWER_BURST_SAMPLES;

	for (int i = 0; i < n; i++) {
		int16_t raw;
		rc = read_one(&self_i_spec, &raw);
		if (rc < 0) {
			LOG_DBG("self_current sample %d: %d", i, rc);
			return rc;
		}
		float ma = raw_to_milliamps(raw);
		sum_ma += ma;
		if (ma > peak_ma) {
			peak_ma = ma;
		}
	}
	out->i_avg_ma = (n > 0) ? (sum_ma / (float)n) : 0.0f;
	out->i_pk_ma  = peak_ma;

	uint16_t vbat = 0;
	rc = read_vbat_mv(&vbat);
	if (rc < 0) {
		LOG_DBG("vbat read: %d", rc);
		return rc;
	}
	out->vbat_mv = vbat;
	return 0;
}

int cookie_power_read_vbat_mv(uint16_t *vbat_mv)
{
	if (!vbat_mv) {
		return -EINVAL;
	}
	int rc = initialise_if_needed();
	if (rc < 0) {
		return rc;
	}
	return read_vbat_mv(vbat_mv);
}

#else  /* !COOKIE_POWER_AVAILABLE */

bool cookie_power_present(void)
{
	return false;
}

int cookie_power_sample_burst(struct cookie_power_sample *out)
{
	ARG_UNUSED(out);
	return -ENODEV;
}

int cookie_power_read_vbat_mv(uint16_t *vbat_mv)
{
	ARG_UNUSED(vbat_mv);
	return -ENODEV;
}

#endif /* COOKIE_POWER_AVAILABLE */
