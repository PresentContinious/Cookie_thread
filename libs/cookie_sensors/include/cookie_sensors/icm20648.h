/*
 * cookie_sensors: ICM-20648 6-axis IMU wrapper.
 *
 * Direct I2C transactions, not registered as a Zephyr sensor device because
 * no upstream Zephyr driver exists for ICM-20648 and the sensor framework
 * would only add boilerplate for our duty cycle (single-shot read every
 * 5..30 seconds). The DTS node still carries the standard binding so
 * accel-fs / gyro-fs properties can be read at compile time.
 *
 * Mirrors the cookie_shtc3 wrapper API: present() probes the bus and the
 * WHO_AM_I register; read() returns SI-style scaled values.
 */

#ifndef COOKIE_SENSORS_ICM20648_H_
#define COOKIE_SENSORS_ICM20648_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Probe the IMU.
 *
 * On boards without an `imu` alias (Dongle), returns false unconditionally.
 * On Cookie, the first call performs WHO_AM_I + soft-reset + range-config;
 * subsequent calls reuse the cached "ready" state.
 *
 * Safe to call repeatedly.
 */
bool cookie_icm20648_present(void);

/**
 * Read one accelerometer + gyroscope sample.
 *
 * Output:
 *   accel_g[3]   X, Y, Z accelerometer in g
 *   gyro_dps[3]  X, Y, Z gyroscope in deg/s
 *
 * Returns 0 on success, negative errno on bus failure or device-not-ready.
 */
int cookie_icm20648_read(float accel_g[3], float gyro_dps[3]);

/**
 * Put the IMU into low-power sleep.
 *
 * SED loop calls this between cycles so the chip's typical 4.4 mA running
 * current drops to ~8 µA standby. No-op on boards without an IMU.
 */
int cookie_icm20648_sleep(void);

#ifdef __cplusplus
}
#endif

#endif /* COOKIE_SENSORS_ICM20648_H_ */
