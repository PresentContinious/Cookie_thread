/*
 * cookie_power: self-current and battery-voltage telemetry.
 *
 * Reads the INA333 amplifier output (instrumentation amplifier sensing the
 * voltage across the in-line shunt) and the internal VDD divider to compute
 * a (i_avg, i_pk, vbat) triple per call. The numbers feed the sensor_frame's
 * optional power fields and underpin Chapter 6 of the thesis.
 *
 * Boards that do not declare the matching ADC channels (Dongle) compile a
 * no-op fallback: present() returns false, sample_burst() returns -ENODEV.
 */

#ifndef COOKIE_POWER_POWER_H_
#define COOKIE_POWER_POWER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cookie_power_sample {
	float    i_avg_ma;     /* mean current over the burst */
	float    i_pk_ma;      /* peak current observed in the burst */
	uint16_t vbat_mv;      /* one VDD reading taken alongside the burst */
};

/**
 * Probe the ADC channels described in DT. Lazy-initialises on the first
 * call and caches the ready state.
 *
 * Boards without the `zephyr,user/io-channels` hookup return false.
 */
bool cookie_power_present(void);

/**
 * Run one CONFIG_COOKIE_POWER_BURST_SAMPLES-sized burst on the self-current
 * channel, then sample the VBAT channel once. Computes mean and peak in
 * milliamperes using the Kconfig calibration constants.
 *
 * @return 0 on success, negative errno on hardware failure.
 */
int cookie_power_sample_burst(struct cookie_power_sample *out);

/**
 * VBAT-only convenience: one sample of the internal VDD divider, in mV.
 * Skips the self-current burst — useful when the caller only needs battery
 * state of charge.
 */
int cookie_power_read_vbat_mv(uint16_t *vbat_mv);

#ifdef __cplusplus
}
#endif

#endif /* COOKIE_POWER_POWER_H_ */
