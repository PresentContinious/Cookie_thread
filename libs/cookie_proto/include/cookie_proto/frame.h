/*
 * Sensor frame schema, common to producer (sensor_node) and consumer (gateway).
 * Optional fields gated by has_* flags; encoder skips them when false.
 */

#ifndef COOKIE_PROTO_FRAME_H_
#define COOKIE_PROTO_FRAME_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sensor_frame {
	/* always-present */
	uint32_t    ts_ms;          /* node uptime in ms (k_uptime_get_32) */
	char        src[5];         /* last 4 hex chars of EUI-64, null-terminated */
	const char *role;           /* "LEADER"|"ROUTER"|"REED"|"CHILD"|"DETACHED"|"SED" */
	int8_t      rssi_dbm;       /* RSSI to parent; 0 if Leader/no parent */
	uint8_t     hops;           /* hops to leader; 0 if self is Leader */

	/* optional (filled by producer when source is available) */
	bool  has_temp;
	float temp_c;

	bool  has_humid;
	float humid_pct;

	bool     has_t_active;
	uint32_t t_active_ms;       /* SED only: wake-window duration */
};

/**
 * Encode frame to a JSON object.
 * @return bytes written (excluding null terminator), or -ENOSPC if buf too small.
 */
int cookie_frame_to_json(const struct sensor_frame *f, char *buf, size_t buf_len);

/**
 * Decode JSON object into frame.
 * Best-effort: missing optional fields leave has_* = false; missing required
 * fields set sentinel values. The role pointer in the output points into the
 * caller-supplied buffer or to a static string; do not free it.
 *
 * @return 0 on success, negative errno on malformed input or unknown structure.
 */
int cookie_frame_from_json(const char *buf, size_t buf_len, struct sensor_frame *out);

#ifdef __cplusplus
}
#endif

#endif /* COOKIE_PROTO_FRAME_H_ */
