/*
 * AUTO-profile main loop.
 *
 * k_timer fires every CONFIG_NODE_REPORT_INTERVAL_SEC, submits work,
 * the work handler samples sensors and pushes a CoAP frame. main()
 * returns to the Zephyr idle thread; scheduler keeps OT + timer + work
 * alive.
 */

#include "node_loop.h"
#include "thread_setup.h"

#include <cookie_proto/coap_client.h>
#include <cookie_proto/frame.h>
#include <cookie_sensors/shtc3.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(node_loop, LOG_LEVEL_INF);

static struct k_timer report_timer;
static struct k_work  report_work;

static void report_work_handler(struct k_work *w)
{
	ARG_UNUSED(w);

	if (cookie_thread_wait_attached(K_NO_WAIT) != 0) {
		LOG_DBG("not attached, skipping push");
		return;
	}

	struct sensor_frame f = { 0 };
	f.ts_ms     = k_uptime_get_32();
	f.role      = cookie_thread_role_str();
	f.rssi_dbm  = cookie_thread_parent_rssi();
	f.hops      = cookie_thread_hops_to_leader();
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

	int rc = cookie_coap_push_frame(&f);
	if (rc < 0) {
		LOG_DBG("push: %d", rc);
	}
}

static void report_timer_cb(struct k_timer *t)
{
	ARG_UNUSED(t);
	k_work_submit(&report_work);
}

int cookie_node_loop_start(void)
{
	k_work_init(&report_work, report_work_handler);
	k_timer_init(&report_timer, report_timer_cb, NULL);
	k_timer_start(&report_timer,
		      K_SECONDS(CONFIG_NODE_REPORT_INTERVAL_SEC),
		      K_SECONDS(CONFIG_NODE_REPORT_INTERVAL_SEC));
	LOG_INF("AUTO loop running, interval=%ds", CONFIG_NODE_REPORT_INTERVAL_SEC);
	return 0;
}
