/*
 * JSON codec for struct sensor_frame.
 *
 * Encoder: hand-written snprintf chain. Optional fields are appended only
 * when their has_* flag is true. Output is a single-line JSON object.
 *
 * Decoder: minimal key-scanning parser. Only the fields produced by the
 * encoder are recognised; anything else is ignored. Strict whitespace
 * handling is intentionally lax (we control both ends of the wire).
 */

#include "cookie_proto/frame.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(cookie_proto_frame, CONFIG_COOKIE_PROTO_LOG_LEVEL);

/* -------------------------------------------------------------------------- */
/* Encoder                                                                    */
/* -------------------------------------------------------------------------- */

#define APPEND(...) \
	do { \
		int _n = snprintf(buf + off, buf_len - off, __VA_ARGS__); \
		if (_n < 0 || (size_t)_n >= buf_len - off) { \
			return -ENOSPC; \
		} \
		off += (size_t)_n; \
	} while (0)

int cookie_frame_to_json(const struct sensor_frame *f, char *buf, size_t buf_len)
{
	if (!f || !buf || buf_len < 2) {
		return -EINVAL;
	}

	size_t off = 0;
	const char *role = (f->role && f->role[0]) ? f->role : "DETACHED";

	APPEND("{\"ts\":%u,\"src\":\"%s\",\"role\":\"%s\",\"rssi\":%d,\"hops\":%u",
	       (unsigned int)f->ts_ms, f->src, role,
	       (int)f->rssi_dbm, (unsigned int)f->hops);

	if (f->has_temp) {
		APPEND(",\"temp_c\":%.2f", (double)f->temp_c);
	}
	if (f->has_humid) {
		APPEND(",\"humid_pct\":%.2f", (double)f->humid_pct);
	}
	if (f->has_t_active) {
		APPEND(",\"t_active_ms\":%u", (unsigned int)f->t_active_ms);
	}
	if (f->has_accel) {
		APPEND(",\"accel_g\":[%.3f,%.3f,%.3f]",
		       (double)f->accel_g[0],
		       (double)f->accel_g[1],
		       (double)f->accel_g[2]);
	}
	if (f->has_gyro) {
		APPEND(",\"gyro_dps\":[%.2f,%.2f,%.2f]",
		       (double)f->gyro_dps[0],
		       (double)f->gyro_dps[1],
		       (double)f->gyro_dps[2]);
	}
	if (f->has_i_avg) {
		APPEND(",\"i_avg_ma\":%.3f", (double)f->i_avg_ma);
	}
	if (f->has_i_pk) {
		APPEND(",\"i_pk_ma\":%.3f", (double)f->i_pk_ma);
	}
	if (f->has_vbat) {
		APPEND(",\"vbat_mv\":%u", (unsigned int)f->vbat_mv);
	}

	APPEND("}");
	return (int)off;
}

#undef APPEND

/* -------------------------------------------------------------------------- */
/* Decoder                                                                    */
/*                                                                            */
/* Recognises a flat JSON object with keys that map to sensor_frame fields.   */
/* Strings are unquoted in place; output role pointer points into the input   */
/* buffer (or to a static literal if missing). Numbers are parsed with        */
/* strtoul / strtof. Whitespace is skipped between tokens.                    */
/* -------------------------------------------------------------------------- */

static const char *role_intern(const char *s, size_t len)
{
	static const char *const known[] = {
		"LEADER", "ROUTER", "REED", "CHILD", "DETACHED", "SED"
	};
	for (size_t i = 0; i < ARRAY_SIZE(known); i++) {
		if (strlen(known[i]) == len && strncmp(known[i], s, len) == 0) {
			return known[i];
		}
	}
	return "UNKNOWN";
}

static const char *skip_ws(const char *p, const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
		p++;
	}
	return p;
}

/* Parses a JSON string (already past opening quote). Sets *out_start, *out_len
 * to the contents (no quotes). Returns pointer past the closing quote, or NULL
 * on malformed input. Does not handle escape sequences (none are produced). */
static const char *parse_str(const char *p, const char *end,
			     const char **out_start, size_t *out_len)
{
	*out_start = p;
	while (p < end && *p != '"') {
		p++;
	}
	if (p >= end) {
		return NULL;
	}
	*out_len = (size_t)(p - *out_start);
	return p + 1; /* past closing quote */
}

/* Parses a number into long. Sets *out. Returns position after number. */
static const char *parse_long(const char *p, const char *end, long *out)
{
	char *endp;
	long v = strtol(p, &endp, 10);
	if (endp == p) {
		return NULL;
	}
	if (endp > end) {
		endp = (char *)end;
	}
	*out = v;
	return endp;
}

static const char *parse_float(const char *p, const char *end, float *out)
{
	char *endp;
	float v = strtof(p, &endp);
	if (endp == p) {
		return NULL;
	}
	if (endp > end) {
		endp = (char *)end;
	}
	*out = v;
	return endp;
}

/* Parses a JSON array of exactly `n` floats into out[]. Caller must have
 * advanced past the opening '['. Returns position after closing ']'. */
static const char *parse_float_array(const char *p, const char *end,
				     float *out, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		p = skip_ws(p, end);
		if (p >= end) {
			return NULL;
		}
		p = parse_float(p, end, &out[i]);
		if (!p) {
			return NULL;
		}
		p = skip_ws(p, end);
		if (p >= end) {
			return NULL;
		}
		if (i < n - 1) {
			if (*p != ',') {
				return NULL;
			}
			p++;
		}
	}
	p = skip_ws(p, end);
	if (p >= end || *p != ']') {
		return NULL;
	}
	return p + 1;
}

int cookie_frame_from_json(const char *buf, size_t buf_len, struct sensor_frame *out)
{
	if (!buf || !out || buf_len < 2) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));
	out->role = "DETACHED";

	const char *p = buf;
	const char *end = buf + buf_len;

	p = skip_ws(p, end);
	if (p >= end || *p != '{') {
		return -EINVAL;
	}
	p++;

	while (p < end) {
		p = skip_ws(p, end);
		if (p >= end) {
			return -EINVAL;
		}
		if (*p == '}') {
			return 0;
		}
		if (*p != '"') {
			return -EINVAL;
		}
		p++;

		const char *key;
		size_t klen;
		p = parse_str(p, end, &key, &klen);
		if (!p) {
			return -EINVAL;
		}
		p = skip_ws(p, end);
		if (p >= end || *p != ':') {
			return -EINVAL;
		}
		p++;
		p = skip_ws(p, end);
		if (p >= end) {
			return -EINVAL;
		}

		/* Dispatch on key. */
		if (klen == 2 && strncmp(key, "ts", 2) == 0) {
			long v;
			p = parse_long(p, end, &v);
			if (!p) {
				return -EINVAL;
			}
			out->ts_ms = (uint32_t)v;
		} else if (klen == 3 && strncmp(key, "src", 3) == 0) {
			if (*p != '"') {
				return -EINVAL;
			}
			p++;
			const char *vs;
			size_t vl;
			p = parse_str(p, end, &vs, &vl);
			if (!p) {
				return -EINVAL;
			}
			size_t copy = MIN(vl, sizeof(out->src) - 1);
			memcpy(out->src, vs, copy);
			out->src[copy] = '\0';
		} else if (klen == 4 && strncmp(key, "role", 4) == 0) {
			if (*p != '"') {
				return -EINVAL;
			}
			p++;
			const char *vs;
			size_t vl;
			p = parse_str(p, end, &vs, &vl);
			if (!p) {
				return -EINVAL;
			}
			out->role = role_intern(vs, vl);
		} else if (klen == 4 && strncmp(key, "rssi", 4) == 0) {
			long v;
			p = parse_long(p, end, &v);
			if (!p) {
				return -EINVAL;
			}
			out->rssi_dbm = (int8_t)v;
		} else if (klen == 4 && strncmp(key, "hops", 4) == 0) {
			long v;
			p = parse_long(p, end, &v);
			if (!p) {
				return -EINVAL;
			}
			out->hops = (uint8_t)v;
		} else if (klen == 6 && strncmp(key, "temp_c", 6) == 0) {
			p = parse_float(p, end, &out->temp_c);
			if (!p) {
				return -EINVAL;
			}
			out->has_temp = true;
		} else if (klen == 9 && strncmp(key, "humid_pct", 9) == 0) {
			p = parse_float(p, end, &out->humid_pct);
			if (!p) {
				return -EINVAL;
			}
			out->has_humid = true;
		} else if (klen == 11 && strncmp(key, "t_active_ms", 11) == 0) {
			long v;
			p = parse_long(p, end, &v);
			if (!p) {
				return -EINVAL;
			}
			out->t_active_ms = (uint32_t)v;
			out->has_t_active = true;
		} else if (klen == 7 && strncmp(key, "accel_g", 7) == 0) {
			if (*p != '[') {
				return -EINVAL;
			}
			p++;
			p = parse_float_array(p, end, out->accel_g, 3);
			if (!p) {
				return -EINVAL;
			}
			out->has_accel = true;
		} else if (klen == 8 && strncmp(key, "gyro_dps", 8) == 0) {
			if (*p != '[') {
				return -EINVAL;
			}
			p++;
			p = parse_float_array(p, end, out->gyro_dps, 3);
			if (!p) {
				return -EINVAL;
			}
			out->has_gyro = true;
		} else if (klen == 8 && strncmp(key, "i_avg_ma", 8) == 0) {
			p = parse_float(p, end, &out->i_avg_ma);
			if (!p) {
				return -EINVAL;
			}
			out->has_i_avg = true;
		} else if (klen == 7 && strncmp(key, "i_pk_ma", 7) == 0) {
			p = parse_float(p, end, &out->i_pk_ma);
			if (!p) {
				return -EINVAL;
			}
			out->has_i_pk = true;
		} else if (klen == 7 && strncmp(key, "vbat_mv", 7) == 0) {
			long v;
			p = parse_long(p, end, &v);
			if (!p) {
				return -EINVAL;
			}
			out->vbat_mv = (uint16_t)v;
			out->has_vbat = true;
		} else {
			/* Unknown key — skip the value. Supports number, string,
			 * array (one level), object (one level). */
			int depth = 0;
			bool in_str = false;
			while (p < end) {
				char c = *p;
				if (in_str) {
					if (c == '"') {
						in_str = false;
					}
				} else if (c == '"') {
					in_str = true;
				} else if (c == '[' || c == '{') {
					depth++;
				} else if (c == ']' || c == '}') {
					if (depth == 0) {
						break; /* end of containing object */
					}
					depth--;
				} else if ((c == ',' || c == '}') && depth == 0) {
					break;
				}
				p++;
			}
		}

		p = skip_ws(p, end);
		if (p >= end) {
			return -EINVAL;
		}
		if (*p == ',') {
			p++;
			continue;
		}
		if (*p == '}') {
			return 0;
		}
		return -EINVAL;
	}

	return -EINVAL;
}
