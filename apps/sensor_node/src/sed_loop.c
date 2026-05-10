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

#include <string.h>

LOG_MODULE_REGISTER(sed_loop, LOG_LEVEL_INF);

int cookie_sed_loop_run(void)
{
	while (true) {
		k_sleep(K_SECONDS(CONFIG_NODE_REPORT_INTERVAL_SEC));

		uint32_t t_start = k_uptime_get_32();

		if (cookie_thread_wait_attached(K_SECONDS(10)) != 0) {
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

		f.has_t_active = true;
		f.t_active_ms  = k_uptime_get_32() - t_start;

		(void)cookie_coap_push_frame(&f);

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
