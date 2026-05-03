/*
 * SED-profile main loop.
 *
 * Each cycle: k_sleep (PM kicks in), wake, optionally re-attach with a
 * 10 s budget, sample sensors, build frame, push CoAP, quiesce. The
 * Zephyr PM subsystem maps k_sleep to System ON deep sleep on nRF52840
 * when no thread is runnable and devices have suspended.
 */

#include "sed_loop.h"
#include "thread_setup.h"

#include <cookie_proto/coap_client.h>
#include <cookie_proto/frame.h>
#include <cookie_sensors/shtc3.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sed_loop, LOG_LEVEL_INF);

int cookie_sed_loop_run(void)
{
	while (true) {
		k_sleep(K_SECONDS(CONFIG_NODE_REPORT_INTERVAL_SEC));

		uint32_t t_start = k_uptime_get_32();

		if (cookie_thread_wait_attached(K_SECONDS(10)) != 0) {
			cookie_coap_quiesce();
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

		f.has_t_active = true;
		f.t_active_ms  = k_uptime_get_32() - t_start;

		(void)cookie_coap_push_frame(&f);

		/* Refresh after the (blocking) CoAP exchange so the frame's
		 * t_active_ms reflects the full active window — but we already
		 * sent it; this updated value is only useful for logging if
		 * the build re-enables logging in debug overlay. */
		uint32_t t_end = k_uptime_get_32();
		LOG_DBG("wake window %u ms", t_end - t_start);

		cookie_coap_quiesce();
	}
	return 0;
}
