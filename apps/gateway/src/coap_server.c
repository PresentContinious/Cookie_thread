/*
 * Stub CoAP server (#2a).
 *
 * Listens on UDP 5683, accepts POST /sensors/data, prints the payload
 * as a JSON line on the host console, and replies with ACK 2.04.
 *
 * #4 will replace this handler with one that parses the frame, decorates
 * with ts_host, emits structured event lines, etc. The interface stays
 * compatible (single-resource registration is the public surface).
 */

#include "coap_server.h"
#include "usb_print.h"

#include <cookie_proto/resources.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(coap_server, LOG_LEVEL_INF);

#define COAP_PORT       5683
#define RX_BUF_LEN      512
#define ACK_BUF_LEN     64
#define MAX_OPTIONS     16
#define STACK_SIZE      4096

static int srv_sock = -1;

static int send_ack(struct coap_packet *req,
		    const struct sockaddr *addr, socklen_t addr_len)
{
	uint8_t buf[ACK_BUF_LEN];
	struct coap_packet ack;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint8_t tlen = coap_header_get_token(req, token);
	uint16_t mid = coap_header_get_id(req);

	int rc = coap_packet_init(&ack, buf, sizeof(buf),
				  COAP_VERSION_1, COAP_TYPE_ACK,
				  tlen, token,
				  COAP_RESPONSE_CODE_CHANGED, mid);
	if (rc < 0) {
		LOG_ERR("ack init: %d", rc);
		return rc;
	}

	int sent = zsock_sendto(srv_sock, ack.data, ack.offset, 0, addr, addr_len);
	if (sent < 0) {
		LOG_ERR("ack sendto: %d", errno);
		return -errno;
	}
	return 0;
}

static int sensors_post(struct coap_resource *r,
			struct coap_packet *req,
			struct sockaddr *addr, socklen_t addr_len)
{
	ARG_UNUSED(r);

	uint16_t plen = 0;
	const uint8_t *payload = coap_packet_get_payload(req, &plen);
	if (payload && plen > 0) {
		gateway_usb_print_line(payload, plen);
	} else {
		LOG_WRN("empty payload, ignoring");
	}

	uint8_t type = coap_header_get_type(req);
	if (type == COAP_TYPE_CON) {
		return send_ack(req, addr, addr_len);
	}
	return 0;
}

static struct coap_resource resources[] = {
	{
		.path = COOKIE_RESOURCE_SENSORS_SEGS,
		.post = sensors_post,
	},
	{ .path = NULL },
};

static void server_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	uint8_t buf[RX_BUF_LEN];
	struct coap_option opts[MAX_OPTIONS];

	while (true) {
		struct sockaddr_in6 peer;
		socklen_t plen = sizeof(peer);
		int rx = zsock_recvfrom(srv_sock, buf, sizeof(buf), 0,
					(struct sockaddr *)&peer, &plen);
		if (rx < 0) {
			if (errno == EAGAIN || errno == EINTR) {
				continue;
			}
			LOG_ERR("recvfrom: %d", errno);
			k_sleep(K_MSEC(100));
			continue;
		}

		struct coap_packet pkt;
		if (coap_packet_parse(&pkt, buf, rx, NULL, 0) < 0) {
			LOG_WRN("malformed CoAP from peer (%d B)", rx);
			continue;
		}

		uint8_t nopts = coap_find_options(&pkt, COAP_OPTION_URI_PATH,
						  opts, MAX_OPTIONS);

		int rc = coap_handle_request(&pkt, resources, opts, nopts,
					     (struct sockaddr *)&peer, plen);
		if (rc < 0) {
			LOG_DBG("coap_handle_request: %d", rc);
		}
	}
}

K_THREAD_STACK_DEFINE(srv_stack, STACK_SIZE);
static struct k_thread srv_thread_data;

int gateway_coap_server_start(void)
{
	srv_sock = zsock_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (srv_sock < 0) {
		LOG_ERR("socket: %d", errno);
		return -errno;
	}

	struct sockaddr_in6 local = {
		.sin6_family = AF_INET6,
		.sin6_port   = htons(COAP_PORT),
		.sin6_addr   = IN6ADDR_ANY_INIT,
	};
	if (zsock_bind(srv_sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
		LOG_ERR("bind: %d", errno);
		zsock_close(srv_sock);
		srv_sock = -1;
		return -errno;
	}

	k_thread_create(&srv_thread_data, srv_stack, STACK_SIZE,
			server_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_name_set(&srv_thread_data, "coap_srv");

	LOG_INF("CoAP server listening on UDP %d", COAP_PORT);
	return 0;
}
