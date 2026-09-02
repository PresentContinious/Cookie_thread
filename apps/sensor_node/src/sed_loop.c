/*
 * SED-profile main loop.
 *
 * Each cycle: k_sleep (PM kicks in), wake, optionally re-attach with a
 * 10 s budget, sample sensors, build frame, push CoAP, put the IMU back
 * to sleep, quiesce CoAP. The Zephyr PM subsystem maps k_sleep to System
 * ON deep sleep on nRF52840 when no thread is runnable and devices have
 * suspended.
 *
 * The SED frame's t_active_ms field reports the wake-window duration, used
 * by the PC-tool battery projection and by the Chapter 6 measurement.
 */

#include "sed_loop.h"
#include "thread_setup.h"

#include <cookie_proto/coap_client.h>
#include <cookie_proto/frame.h>
#include <cookie_sensors/shtc3.h>
#include <cookie_sensors/icm20648.h>
#include <cookie_power/power.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include <string.h>

LOG_MODULE_REGISTER(sed_loop, LOG_LEVEL_INF);

#if defined(CONFIG_NODE_MEAS_LED)
/* Phase markers for the cross-OS timing measurement (same semantics in the
 * RIOT and Contiki-NG ports; enabled by overlays/meas_led.conf):
 *   red   HIGH at wake, LOW right after the transmit call returns
 *         -> width = wake-to-transmit latency, rising edge = scope trigger;
 *   green HIGH while the sensors are read -> the sensor window. */
static const struct gpio_dt_spec mark_tx =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_red), gpios, {0});
static const struct gpio_dt_spec mark_sens =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_green), gpios, {0});

static inline void mark_set(const struct gpio_dt_spec *m, int on)
{
	if (m->port) {
		gpio_pin_set_dt(m, on);
	}
}
#define MARK_TX(on)    mark_set(&mark_tx, (on))
#define MARK_SENS(on)  mark_set(&mark_sens, (on))
#else
#define MARK_TX(on)    do { } while (0)
#define MARK_SENS(on)  do { } while (0)
#endif

int cookie_sed_loop_run(void)
{
#if defined(CONFIG_NODE_MEAS_LED)
	if (mark_tx.port) {
		gpio_pin_configure_dt(&mark_tx, GPIO_OUTPUT_INACTIVE);
	}
	if (mark_sens.port) {
		gpio_pin_configure_dt(&mark_sens, GPIO_OUTPUT_INACTIVE);
	}
#endif

	while (true) {
		k_sleep(K_SECONDS(CONFIG_NODE_REPORT_INTERVAL_SEC));

		uint32_t t_start = k_uptime_get_32();

		/* Rising edge = scope trigger; stays high until the transmit
		 * call returns, so the pulse width is the wake-to-transmit
		 * latency the campaign measures. */
		MARK_TX(1);

		if (cookie_thread_wait_attached(K_SECONDS(10)) != 0) {
			MARK_TX(0);
			cookie_coap_quiesce();
			(void)cookie_icm20648_sleep();
			continue;
		}

		struct sensor_frame f = { 0 };
		f.ts_ms    = t_start;
		f.role     = "SED";
		f.rssi_dbm = cookie_thread_parent_rssi();
		f.hops     = cookie_thread_hops_to_leader();
		cookie_thread_format_src(f.src);
		f.pad_len = CONFIG_NODE_PAYLOAD_PAD_BYTES;

		/* Sensor window: green high across every sensor access. */
		MARK_SENS(1);

		if (cookie_shtc3_present()) {
			float t, h;
			if (cookie_shtc3_read(&t, &h) == 0) {
				f.has_temp  = true;
				f.temp_c    = t;
				f.has_humid = true;
				f.humid_pct = h;
			}
		}

		if (cookie_icm20648_present()) {
			float a[3], g[3];
			if (cookie_icm20648_read(a, g) == 0) {
				f.has_accel = true;
				memcpy(f.accel_g, a, sizeof(a));
				f.has_gyro  = true;
				memcpy(f.gyro_dps, g, sizeof(g));
			}
		}

		if (cookie_power_present()) {
			struct cookie_power_sample s;
			if (cookie_power_sample_burst(&s) == 0) {
				f.has_i_avg  = true;
				f.i_avg_ma   = s.i_avg_ma;
				f.has_i_pk   = true;
				f.i_pk_ma    = s.i_pk_ma;
				f.has_vbat   = true;
				f.vbat_mv    = s.vbat_mv;
			}
		}

		MARK_SENS(0);

		f.has_t_active = true;
		f.t_active_ms  = k_uptime_get_32() - t_start;

		(void)cookie_coap_push_frame(&f);

		/* Falling edge: transmit handed off. Red pulse width = the OS
		 * wake-to-transmit latency. */
		MARK_TX(0);

		uint32_t t_end = k_uptime_get_32();
		LOG_DBG("wake window %u ms", t_end - t_start);

		/* Quiet down the IMU and CoAP machinery before the next deep
		 * sleep. SHTC3 already sleeps automatically after each one-shot
		 * read; SAADC powers down with the kernel idle. */
		(void)cookie_icm20648_sleep();
		cookie_coap_quiesce();
	}
	return 0;
}
