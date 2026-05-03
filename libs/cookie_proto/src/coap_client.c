/*
 * CoAP push client.
 *
 * Discovery: first push after init or after a cache miss is a NON POST
 * to ff03::1 (Realm-Local All-Nodes). The first responder's source
 * address is cached and used as the unicast peer for all subsequent
 * pushes.
 *
 * Cached path: CON POST to the cached unicast IPv6, with manual
 * exponential-backoff retransmission until ACK or budget exhaustion.
 *
 * Threading: blocking from the caller's context. Internal mutex
 * serialises concurrent pushes (workqueue handler and SED loop never
 * race in practice but the mutex makes the contract explicit).
 */

#include "cookie_proto/coap_client.h"
#include "cookie_proto/resources.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(cookie_coap, CONFIG_COOKIE_PROTO_LOG_LEVEL);

#define COAP_PORT          5683
#define DISCOVERY_GROUP    "ff03::1"
#define ACK_TIMEOUT_MS     2000
#define MAX_RETRANSMIT     4
#define POLL_GRANULARITY   100

struct ctx {
	int                  sock;
	bool                 initialised;
	bool                 have_gateway;
	struct sockaddr_in6  gateway;
	struct k_mutex       lock;
};

static struct ctx ctx;

static void zero_gateway(void)
{
	memset(&ctx.gateway, 0, sizeof(ctx.gateway));
	ctx.have_gateway = false;
}

int cookie_coap_init(void)
{
	if (ctx.initialised) {
		return 0;
	}

	k_mutex_init(&ctx.lock);
	zero_gateway();

	ctx.sock = zsock_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (ctx.sock < 0) {
		LOG_ERR("socket(): %d", errno);
		return -errno;
	}

	struct sockaddr_in6 local = {
		.sin6_family = AF_INET6,
		.sin6_port   = htons(0),  /* ephemeral */
		.sin6_addr   = IN6ADDR_ANY_INIT,
	};
	if (zsock_bind(ctx.sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
		LOG_ERR("bind(): %d", errno);
		zsock_close(ctx.sock);
		ctx.sock = -1;
		return -errno;
	}

	ctx.initialised = true;
	LOG_INF("CoAP transport ready");
	return 0;
}

void cookie_coap_invalidate_gateway(void)
{
	k_mutex_lock(&ctx.lock, K_FOREVER);
	zero_gateway();
	k_mutex_unlock(&ctx.lock);
}

void cookie_coap_quiesce(void)
{
	/* Blocking-send model: nothing pending across calls.  Hook left in
	 * place so SED loop's intent is documented, and so a future
	 * non-blocking variant has a clear cancel point. */
}

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static int build_post(struct coap_packet *out,
		      uint8_t *buf, size_t buf_len,
		      uint8_t type, const uint8_t *token, uint8_t token_len,
		      uint16_t msg_id, const char *json, size_t json_len)
{
	int rc;

	rc = coap_packet_init(out, buf, buf_len,
			      COAP_VERSION_1, type, token_len, token,
			      COAP_METHOD_POST, msg_id);
	if (rc < 0) {
		return rc;
	}

	rc = coap_packet_append_option(out, COAP_OPTION_URI_PATH,
				       (const uint8_t *)COOKIE_RESOURCE_SENSORS_SEG_0,
				       strlen(COOKIE_RESOURCE_SENSORS_SEG_0));
	if (rc < 0) {
		return rc;
	}
	rc = coap_packet_append_option(out, COAP_OPTION_URI_PATH,
				       (const uint8_t *)COOKIE_RESOURCE_SENSORS_SEG_1,
				       strlen(COOKIE_RESOURCE_SENSORS_SEG_1));
	if (rc < 0) {
		return rc;
	}

	uint8_t ct = COAP_CONTENT_FORMAT_APP_JSON;
	rc = coap_packet_append_option(out, COAP_OPTION_CONTENT_FORMAT,
				       &ct, sizeof(ct));
	if (rc < 0) {
		return rc;
	}

	rc = coap_packet_append_payload_marker(out);
	if (rc < 0) {
		return rc;
	}
	rc = coap_packet_append_payload(out, (const uint8_t *)json, json_len);
	if (rc < 0) {
		return rc;
	}
	return 0;
}

static int wait_for_reply(uint8_t *buf, size_t buf_len,
			  uint8_t *out_token, uint8_t *out_token_len,
			  struct sockaddr_in6 *peer,
			  k_timeout_t timeout)
{
	struct zsock_pollfd pfd = {
		.fd     = ctx.sock,
		.events = ZSOCK_POLLIN,
	};

	int64_t deadline = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);

	while (true) {
		int64_t now = k_uptime_get();
		int64_t left = deadline - now;
		if (left <= 0) {
			return -ETIMEDOUT;
		}
		int n = zsock_poll(&pfd, 1, (int)MIN(left, POLL_GRANULARITY));
		if (n < 0) {
			return -errno;
		}
		if (n == 0) {
			continue;
		}
		socklen_t plen = sizeof(*peer);
		int rx = zsock_recvfrom(ctx.sock, buf, buf_len, 0,
					(struct sockaddr *)peer, &plen);
		if (rx < 0) {
			return -errno;
		}

		struct coap_packet rsp;
		if (coap_packet_parse(&rsp, buf, rx, NULL, 0) < 0) {
			LOG_WRN("malformed CoAP reply (%d B), ignoring", rx);
			continue;
		}

		uint8_t tok[COAP_TOKEN_MAX_LEN];
		uint8_t tlen = coap_header_get_token(&rsp, tok);
		memcpy(out_token, tok, tlen);
		*out_token_len = tlen;
		return rx;
	}
}

static bool token_matches(const uint8_t *a, uint8_t alen,
			  const uint8_t *b, uint8_t blen)
{
	return alen == blen && (alen == 0 || memcmp(a, b, alen) == 0);
}

/* -------------------------------------------------------------------------- */
/* Discovery                                                                  */
/* -------------------------------------------------------------------------- */

static int discover(const char *json, size_t json_len)
{
	uint8_t buf[CONFIG_COOKIE_PROTO_COAP_BLOCK_SIZE];
	struct coap_packet pkt;
	uint8_t token[4];
	sys_rand_get(token, sizeof(token));
	uint16_t mid = coap_next_id();

	int rc = build_post(&pkt, buf, sizeof(buf),
			    COAP_TYPE_NON_CON, token, sizeof(token), mid,
			    json, json_len);
	if (rc < 0) {
		LOG_ERR("build_post(NON): %d", rc);
		return rc;
	}

	struct sockaddr_in6 mcast = {
		.sin6_family = AF_INET6,
		.sin6_port   = htons(COAP_PORT),
	};
	if (zsock_inet_pton(AF_INET6, DISCOVERY_GROUP, &mcast.sin6_addr) != 1) {
		return -EINVAL;
	}

	int sent = zsock_sendto(ctx.sock, pkt.data, pkt.offset, 0,
				(struct sockaddr *)&mcast, sizeof(mcast));
	if (sent < 0) {
		LOG_ERR("sendto(mcast): %d", errno);
		return -errno;
	}
	LOG_INF("discovery NON sent to " DISCOVERY_GROUP " (%d B)", sent);

	uint8_t rxbuf[CONFIG_COOKIE_PROTO_COAP_BLOCK_SIZE];
	uint8_t rx_tok[COAP_TOKEN_MAX_LEN];
	uint8_t rx_tlen;
	struct sockaddr_in6 peer;

	int rx = wait_for_reply(rxbuf, sizeof(rxbuf), rx_tok, &rx_tlen, &peer,
				K_MSEC(CONFIG_COOKIE_PROTO_DISCOVERY_TIMEOUT_MS));
	if (rx < 0) {
		LOG_INF("discovery: no responder (%d)", rx);
		return rx;
	}
	if (!token_matches(rx_tok, rx_tlen, token, sizeof(token))) {
		LOG_WRN("discovery reply token mismatch, ignoring");
		return -EAGAIN;
	}

	memcpy(&ctx.gateway, &peer, sizeof(peer));
	ctx.gateway.sin6_port = htons(COAP_PORT);
	ctx.have_gateway = true;

	char addrbuf[INET6_ADDRSTRLEN];
	zsock_inet_ntop(AF_INET6, &peer.sin6_addr, addrbuf, sizeof(addrbuf));
	LOG_INF("gateway discovered: %s", addrbuf);
	return 0;
}

/* -------------------------------------------------------------------------- */
/* Unicast CON push                                                           */
/* -------------------------------------------------------------------------- */

static int push_con(const char *json, size_t json_len)
{
	uint8_t buf[CONFIG_COOKIE_PROTO_COAP_BLOCK_SIZE];
	struct coap_packet pkt;
	uint8_t token[4];
	sys_rand_get(token, sizeof(token));
	uint16_t mid = coap_next_id();

	int rc = build_post(&pkt, buf, sizeof(buf),
			    COAP_TYPE_CON, token, sizeof(token), mid,
			    json, json_len);
	if (rc < 0) {
		LOG_ERR("build_post(CON): %d", rc);
		return rc;
	}

	int timeout_ms = ACK_TIMEOUT_MS;

	for (int attempt = 0; attempt <= MAX_RETRANSMIT; attempt++) {
		int sent = zsock_sendto(ctx.sock, pkt.data, pkt.offset, 0,
					(struct sockaddr *)&ctx.gateway,
					sizeof(ctx.gateway));
		if (sent < 0) {
			LOG_ERR("sendto(unicast): %d", errno);
			return -errno;
		}

		uint8_t rxbuf[CONFIG_COOKIE_PROTO_COAP_BLOCK_SIZE];
		uint8_t rx_tok[COAP_TOKEN_MAX_LEN];
		uint8_t rx_tlen;
		struct sockaddr_in6 peer;
		int rx = wait_for_reply(rxbuf, sizeof(rxbuf), rx_tok, &rx_tlen,
					&peer, K_MSEC(timeout_ms));
		if (rx >= 0 && token_matches(rx_tok, rx_tlen, token, sizeof(token))) {
			LOG_DBG("ACK after %d retries", attempt);
			return 0;
		}
		LOG_DBG("no ACK (try %d/%d, t=%d ms)",
			attempt + 1, MAX_RETRANSMIT + 1, timeout_ms);
		timeout_ms *= 2;
	}

	LOG_WRN("CON exhausted, invalidating gateway");
	zero_gateway();
	return -ETIMEDOUT;
}

/* -------------------------------------------------------------------------- */
/* Public push                                                                */
/* -------------------------------------------------------------------------- */

int cookie_coap_push_frame(const struct sensor_frame *f)
{
	if (!ctx.initialised) {
		return -ENODEV;
	}

	char json[CONFIG_COOKIE_PROTO_FRAME_BUF_SIZE];
	int n = cookie_frame_to_json(f, json, sizeof(json));
	if (n < 0) {
		return n;
	}

	int rc;
	k_mutex_lock(&ctx.lock, K_FOREVER);

	if (!ctx.have_gateway) {
		rc = discover(json, (size_t)n);
		if (rc < 0) {
			k_mutex_unlock(&ctx.lock);
			return rc;
		}
		/* Discovery already delivered the frame as a NON; no follow-up. */
		k_mutex_unlock(&ctx.lock);
		return 0;
	}

	rc = push_con(json, (size_t)n);
	k_mutex_unlock(&ctx.lock);
	return rc;
}
