/*
 * Stub CoAP server (#2a), now on OpenThread native CoAP API.
 *
 * Listens on UDP 5683 via OT's CoAP layer (no Zephyr socket plumbing).
 * Accepts POST /sensors/data, prints the payload as a JSON line on the
 * host console, and replies with ACK 2.04 if the request was CON.
 *
 * Why OT-native CoAP and not Zephyr CoAP-over-socket: the Zephyr UDP
 * socket bound to AF_INET6 ANY does not receive multicast packets even
 * after IPV6_ADD_MEMBERSHIP / IPV6_JOIN_GROUP / net_if_ipv6_maddr_add
 * (all return ENOTSUP / NULL on the OT-backed stack). OT's CoAP layer
 * subscribes to the right Thread groups internally and surfaces every
 * matching request to our handler.
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
#include <zephyr/net/openthread.h>

#include <openthread/coap.h>
#include <openthread/error.h>
#include <openthread/instance.h>
#include <openthread/message.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(coap_server, LOG_LEVEL_INF);

#define COAP_PORT      5683
#define MAX_PAYLOAD    512

static otCoapResource sensors_resource = {
	.mUriPath = COOKIE_RESOURCE_SENSORS_PATH,   /* "sensors/data" */
	.mHandler = NULL,                            /* set in start() */
	.mContext = NULL,
	.mNext    = NULL,
};

/* Send a 2.04 Changed response back to the requester.
 *   - CON request -> piggyback ACK (same message ID)
 *   - NON request -> NON response (fresh message ID)
 * NON responses are required for our discovery flow: when the sensor sends
 * a NON multicast POST, gateway must answer with a NON unicast so the
 * sensor can cache the gateway's address from the response source. */
static otError send_response_changed(otInstance *ot,
				     const otMessage *request,
				     const otMessageInfo *info)
{
	otCoapType req_type = otCoapMessageGetType(request);
	otCoapType rsp_type = (req_type == OT_COAP_TYPE_CONFIRMABLE)
			      ? OT_COAP_TYPE_ACKNOWLEDGMENT
			      : OT_COAP_TYPE_NON_CONFIRMABLE;

	otMessage *response = otCoapNewMessage(ot, NULL);
	if (!response) {
		return OT_ERROR_NO_BUFS;
	}

	otError err = otCoapMessageInitResponse(response, request, rsp_type,
						OT_COAP_CODE_CHANGED);
	if (err != OT_ERROR_NONE) {
		otMessageFree(response);
		return err;
	}

	err = otCoapSendResponse(ot, response, info);
	if (err != OT_ERROR_NONE) {
		otMessageFree(response);
	}
	return err;
}

static void sensors_post_handler(void *ctx,
				 otMessage *message,
				 const otMessageInfo *message_info)
{
	ARG_UNUSED(ctx);

	if (otCoapMessageGetCode(message) != OT_COAP_CODE_POST) {
		LOG_DBG("ignoring non-POST on /sensors/data");
		return;
	}

	uint16_t off = otMessageGetOffset(message);
	uint16_t total = otMessageGetLength(message);
	uint16_t plen = (total > off) ? (total - off) : 0;

	if (plen > 0) {
		uint8_t buf[MAX_PAYLOAD];
		uint16_t copy = (plen <= sizeof(buf)) ? plen : sizeof(buf);
		uint16_t got = otMessageRead(message, off, buf, copy);
		gateway_usb_print_line(buf, got);
	} else {
		LOG_WRN("empty POST /sensors/data, ignoring");
	}

	/* Always respond — NON gets a NON response, CON gets a piggyback
	 * ACK. The sensor's discovery flow needs a NON response back so it
	 * can cache our address from the source field. */
	otInstance *ot = openthread_get_default_instance();
	(void)send_response_changed(ot, message, message_info);
}

static void default_handler(void *ctx,
			    otMessage *message,
			    const otMessageInfo *message_info)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(message);
	ARG_UNUSED(message_info);
	LOG_DBG("unmatched CoAP request");
}

int gateway_coap_server_start(void)
{
	otInstance *ot = openthread_get_default_instance();
	if (!ot) {
		LOG_ERR("no OT instance");
		return -ENODEV;
	}

	sensors_resource.mHandler = sensors_post_handler;
	sensors_resource.mContext = NULL;

	otCoapSetDefaultHandler(ot, default_handler, NULL);
	otCoapAddResource(ot, &sensors_resource);

	otError err = otCoapStart(ot, COAP_PORT);
	if (err != OT_ERROR_NONE) {
		LOG_ERR("otCoapStart: %d", err);
		return -EIO;
	}

	LOG_INF("OT-CoAP server listening on UDP %d, resource %s",
		COAP_PORT, sensors_resource.mUriPath);
	return 0;
}
