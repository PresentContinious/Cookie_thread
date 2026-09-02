/*
 * cookie_sensors: ICM-20648 6-axis IMU implementation.
 *
 * Bank-switched register access:
 *   - REG_BANK_SEL (0x7F) selects the active register bank (0..3)
 *   - All sensor data registers live in bank 0
 *   - Range / sample-rate / DLPF config lives in bank 2
 *
 * Power flow:
 *   - reset_and_wake(): soft DEVICE_RESET, then PWR_MGMT_1=CLK_AUTO,
 *     then PWR_MGMT_2=ALL_ON
 *   - configure_ranges(): writes ACCEL_CONFIG and GYRO_CONFIG_1 in bank 2,
 *     returns to bank 0
 *   - cookie_icm20648_sleep(): re-asserts SLEEP bit in PWR_MGMT_1
 *
 * Conversion:
 *   raw int16 / sensitivity table -> physical units (g, dps)
 *   Sensitivity is selected by accel-fs / gyro-fs DT properties
 *   (defaults +/- 4 g, +/- 500 dps).
 */

#include "cookie_sensors/icm20648.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <errno.h>

LOG_MODULE_REGISTER(cookie_icm20648, CONFIG_COOKIE_SENSORS_LOG_LEVEL);

#define IMU_NODE DT_ALIAS(imu)

#if DT_NODE_HAS_STATUS(IMU_NODE, okay)
#define IMU_AVAILABLE 1
static const struct i2c_dt_spec imu_i2c   = I2C_DT_SPEC_GET(IMU_NODE);
static const uint8_t accel_fs_cfg         = DT_PROP_OR(IMU_NODE, accel_fs, 1);
static const uint8_t gyro_fs_cfg          = DT_PROP_OR(IMU_NODE, gyro_fs, 1);
#else
#define IMU_AVAILABLE 0
#endif

/* ----- registers ---------------------------------------------------------- */

#define REG_BANK_SEL          0x7F
#define BANK_0                0x00
#define BANK_2                0x20  /* (2 << 4) */

#define REG_WHO_AM_I          0x00
#define WHO_AM_I_VALUE        0xE0

#define REG_USER_CTRL         0x03

#define REG_PWR_MGMT_1        0x06
#define PWR_MGMT_1_DEVICE_RST 0x80
#define PWR_MGMT_1_SLEEP      0x40
#define PWR_MGMT_1_CLK_AUTO   0x01

#define REG_PWR_MGMT_2        0x07
#define PWR_MGMT_2_ALL_ON     0x00

/* sensor data, bank 0 */
#define REG_ACCEL_XOUT_H      0x2D  /* 12 bytes: ACC[3] + GYRO[3] */

/* bank 2 config */
#define REG_GYRO_CONFIG_1     0x01
#define REG_ACCEL_CONFIG      0x14

/* sensitivity tables: index by FS_SEL (0..3) */
static const float accel_lsb_per_g[4]   = { 16384.0f, 8192.0f, 4096.0f, 2048.0f };
static const float gyro_lsb_per_dps[4]  = { 131.0f,   65.5f,   32.8f,   16.4f  };

#if IMU_AVAILABLE

static bool initialised;
static uint8_t dev_addr;   /* resolved by probe: 0x68 (AD0 low) or 0x69 (high) */

static int wr(uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(imu_i2c.bus, dev_addr, reg, val);
}

static int rd(uint8_t reg, uint8_t *val)
{
	return i2c_reg_read_byte(imu_i2c.bus, dev_addr, reg, val);
}

static int select_bank(uint8_t bank)
{
	return wr(REG_BANK_SEL, bank);
}

/* The ICM-20648 sits at 0x68 or 0x69 depending on the AD0 strap. On this
 * board the interface-select resistors are NM, so the strap is not guaranteed
 * to match the DT address. Try the DT address first, then the alternate, and
 * log the WHO_AM_I seen at each so a silent failure is diagnosable over RTT. */
static int probe_addr(void)
{
	uint8_t cands[2] = { (uint8_t)imu_i2c.addr,
			     (imu_i2c.addr == 0x68) ? 0x69 : 0x68 };

	for (int i = 0; i < 2; i++) {
		dev_addr = cands[i];
		(void)select_bank(BANK_0);
		uint8_t who = 0;
		int rc = rd(REG_WHO_AM_I, &who);
		LOG_INF("ICM probe @0x%02x: rc=%d who=0x%02x", dev_addr, rc, who);
		if (rc == 0 && who == WHO_AM_I_VALUE) {
			return 0;
		}
	}
	return -ENODEV;
}

static int reset_and_wake(void)
{
	int rc = select_bank(BANK_0);
	if (rc < 0) {
		return rc;
	}

	rc = wr(REG_PWR_MGMT_1, PWR_MGMT_1_DEVICE_RST);
	if (rc < 0) {
		return rc;
	}
	k_sleep(K_MSEC(50));

	rc = wr(REG_PWR_MGMT_1, PWR_MGMT_1_CLK_AUTO);
	if (rc < 0) {
		return rc;
	}
	k_sleep(K_MSEC(10));

	rc = wr(REG_PWR_MGMT_2, PWR_MGMT_2_ALL_ON);
	if (rc < 0) {
		return rc;
	}
	return 0;
}

static int configure_ranges(uint8_t accel_fs, uint8_t gyro_fs)
{
	int rc = select_bank(BANK_2);
	if (rc < 0) {
		return rc;
	}

	/* GYRO_CONFIG_1 layout:
	 *   bit 0     FCHOICE (1 = filter on)
	 *   bits 2:1  FS_SEL
	 *   bits 5:3  DLPFCFG (0..7)
	 * Pick DLPFCFG = 1 for ~12 Hz noise floor, FCHOICE = 1. */
	uint8_t gyro_cfg = (uint8_t)((1u << 3) | ((gyro_fs & 0x3) << 1) | 0x01);
	rc = wr(REG_GYRO_CONFIG_1, gyro_cfg);
	if (rc < 0) {
		goto out;
	}

	uint8_t accel_cfg = (uint8_t)((1u << 3) | ((accel_fs & 0x3) << 1) | 0x01);
	rc = wr(REG_ACCEL_CONFIG, accel_cfg);

out:
	(void)select_bank(BANK_0);
	return rc;
}

static int initialise_if_needed(void)
{
	if (initialised) {
		return 0;
	}

	if (!device_is_ready(imu_i2c.bus)) {
		LOG_DBG("I2C bus not ready");
		return -ENODEV;
	}

	int rc = probe_addr();
	if (rc < 0) {
		LOG_WRN("ICM-20648 not answering at 0x68 or 0x69");
		return rc;
	}

	rc = reset_and_wake();
	if (rc < 0) {
		LOG_WRN("reset_and_wake: %d", rc);
		return rc;
	}

	rc = configure_ranges(accel_fs_cfg, gyro_fs_cfg);
	if (rc < 0) {
		LOG_WRN("configure_ranges: %d", rc);
		return rc;
	}

	LOG_INF("ICM-20648 ready @0x%02x: accel=+/-%dg gyro=+/-%ddps",
		dev_addr, 2 << accel_fs_cfg,
		(int)(250 << gyro_fs_cfg));
	initialised = true;
	return 0;
}

bool cookie_icm20648_present(void)
{
	return initialise_if_needed() == 0;
}

int cookie_icm20648_read(float accel_g[3], float gyro_dps[3])
{
	if (!accel_g || !gyro_dps) {
		return -EINVAL;
	}

	int rc = initialise_if_needed();
	if (rc < 0) {
		return rc;
	}

	uint8_t buf[12];
	rc = i2c_burst_read(imu_i2c.bus, dev_addr, REG_ACCEL_XOUT_H, buf, sizeof(buf));
	if (rc < 0) {
		LOG_DBG("burst_read: %d", rc);
		return rc;
	}

	float a_scale = accel_lsb_per_g[accel_fs_cfg & 0x3];
	float g_scale = gyro_lsb_per_dps[gyro_fs_cfg & 0x3];

	for (int i = 0; i < 3; i++) {
		int16_t raw_a = (int16_t)((buf[i*2]   << 8) | buf[i*2 + 1]);
		int16_t raw_g = (int16_t)((buf[6 + i*2] << 8) | buf[6 + i*2 + 1]);
		accel_g[i]  = (float)raw_a / a_scale;
		gyro_dps[i] = (float)raw_g / g_scale;
	}
	return 0;
}

int cookie_icm20648_sleep(void)
{
	if (!initialised) {
		return 0;
	}
	int rc = select_bank(BANK_0);
	if (rc < 0) {
		return rc;
	}
	return wr(REG_PWR_MGMT_1, PWR_MGMT_1_CLK_AUTO | PWR_MGMT_1_SLEEP);
}

#else  /* !IMU_AVAILABLE */

bool cookie_icm20648_present(void)
{
	return false;
}

int cookie_icm20648_read(float accel_g[3], float gyro_dps[3])
{
	ARG_UNUSED(accel_g);
	ARG_UNUSED(gyro_dps);
	return -ENODEV;
}

int cookie_icm20648_sleep(void)
{
	return 0;
}

#endif /* IMU_AVAILABLE */
