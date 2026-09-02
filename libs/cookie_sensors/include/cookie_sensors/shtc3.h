/*
 * SHTC3 temperature/humidity wrapper.
 *
 * Resolves the DT alias "temp-humid" once at first use and caches the
 * device pointer. On boards without an SHTC3 (Dongle) the alias is
 * absent and present() returns false; the main loop never calls read().
 */

#ifndef COOKIE_SENSORS_SHTC3_H_
#define COOKIE_SENSORS_SHTC3_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @return true if SHTC3 is declared in DT and ready to be sampled.
 */
bool cookie_shtc3_present(void);

/**
 * One-shot read. Wakes the sensor from sleep mode, fetches T+RH, and
 * returns it to sleep mode (built-in low power, ~0.7 µA idle).
 *
 * @return 0 on success, negative errno on bus / driver / readiness error.
 */
int cookie_shtc3_read(float *temp_c, float *humid_pct);

#ifdef __cplusplus
}
#endif

#endif /* COOKIE_SENSORS_SHTC3_H_ */
