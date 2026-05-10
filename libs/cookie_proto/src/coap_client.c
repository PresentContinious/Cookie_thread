/*
 * CoAP push client on OpenThread native CoAP API.
 *
 * The previous implementation used a Zephyr UDP socket bound to AF_INET6 ANY
 * and sent multicast NON to ff03::1 for discovery. That approach hangs on
 * the OT-backed Zephyr stack because the socket layer does not surface
 * multicast packets even when joined via IPV6_JOIN_GROUP / net_if_ipv6_maddr_add
 * (both return ENOTSUP / NULL). Symptom: first push hangs forever, work
 * queue never advances, no further cycles ever fire.
 *
 * This rewrite uses OT's CoAP layer (otCoapSendRequest) which understands
 * Thread multicast natively and is fire-and-forget — push returns
 * immediately and the response handler caches the gateway address when the
 * gateway answers.
 *
 * Threading: push_frame is called from the workqueue context. It builds an
 * otMessage, hands it to OT, and returns. OT's CoAP layer manages
 * retransmission and timeout independently. The mutex still serialises
 * gateway-address state across pushes.
 */

#include "cookie_proto/coap_client.h"
#include "cookie_proto/frame.h"
#include "cookie_proto/resources.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/openthread.h>

#include <openthread/coap.h>
#include <openthread/error.h>
#include <openthread/instance.h>
#include <openthread/ip6.h>
#include <openthread/message.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(cookie_coap, CONFIG_COOKIE_PROTO_LOG_LEVEL);

#define DISCOVERY_GROUP "ff03::1"

struct ctx {
	otInstance       *ot;
	bool              initialised;
	bool              have_gateway;
	otIp6Address      gateway;
	struct k_mutex    lock;
};

static struct ctx ctx;

int cookie_coap_init(void)
{
	if (ctx.initialised) {
		return 0;
	}

	ctx.ot = openthread_get_default_instance();
	if (!ctx.ot) {
		LOG_ERR("no OT instance");
		return -ENODEV;
	}

	k_mutex_init(&ctx.lock);
	memset(&ctx.gateway, 0, sizeof(ctx.gateway));
	ctx.have_gateway = false;

	/* otCoapStart enables the CoAP layer (server + client). Port 0 lets
	 * OT pick an ephemeral local port for client requests. The gateway
	 * already started its own otCoapStart on 5683 — these are separate
	 * OT instances so there's no conflict. */
	otError err = otCoapStart(ctx.ot, 0);
	if (err != OT_ERROR_NONE && err != OT_ERROR_ALREADY) {
		LOG_ERR("otCoapStart: %d", err);
		return -EIO;
	}

	ctx.initialised = true;
	LOG_INF("OT-CoAP transport ready");
	return 0;
}

void cookie_coap_invalidate_gateway(void)
{
	k_mutex_lock(&ctx.lock, K_FOREVER);
	memset(&ctx.gateway, 0, sizeof(ctx.gateway));
	ctx.have_gateway = false;
	k_mutex_unlock(&ctx.lock);
}

void cookie_coap_quiesce(void)
{
	/* Async send model: nothing to cancel. Hook left in place so the
	 * SED loop's intent is documented. */
}

/* Called by OT CoAP when a response arrives or the request times out.
 * For NON discovery: gateway responds with a NON containing the same token.
 * The source address is the gateway's RLOC; cache it for subsequent
 * unicast CON pushes. */
static void on_response(void *user_ctx,
			otMessage *message,
			const otMessageInfo *info,
			otError result)
{
	ARG_UNUSED(user_ctx);

	if (result != OT_ERROR_NONE || !message || !info) {
		LOG_DBG("response: err=%d", result);
		return;
	}

	k_mutex_lock(&ctx.lock, K_FOREVER);
	memcpy(&ctx.gateway, &info->mPeerAddr, sizeof(ctx.gateway));
	ctx.have_gateway = true;
	k_mutex_unlock(&ctx.lock);

	LOG_INF("gateway discovered, caching address");
}

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

	bool unicast;
	otIp6Address peer_addr;
	k_mutex_lock(&ctx.lock, K_FOREVER);
	unicast = ctx.have_gateway;
	memcpy(&peer_addr, &ctx.gateway, sizeof(peer_addr));
	k_mutex_unlock(&ctx.lock);

	/* Build the CoAP message. */
	otMessage *msg = otCoapNewMessage(ctx.ot, NULL);
	if (!msg) {
		LOG_WRN("otCoapNewMessage out of buffers");
		return -ENOMEM;
	}

	/* Discovery cycle uses NON multicast; cached path uses CON unicast.
	 * Both go through the same SendRequest path; OT handles
	 * retransmission internally for CON. */
	otCoapType type = unicast ? OT_COAP_TYPE_CONFIRMABLE
				  : OT_COAP_TYPE_NON_CONFIRMABLE;

	/* MessageInit / GenerateToken return void in NCS v3.3 OT API. */
	otCoapMessageInit(msg, type, OT_COAP_CODE_POST);
	otCoapMessageGenerateToken(msg, 4);

	otError err = otCoapMessageAppendUriPathOptions(msg, COOKIE_RESOURCE_SENSORS_PATH);
	if (err != OT_ERROR_NONE) goto out_free;

	err = otCoapMessageAppendContentFormatOption(msg,
		OT_COAP_OPTION_CONTENT_FORMAT_JSON);
	if (err != OT_ERROR_NONE) goto out_free;

	err = otCoapMessageSetPayloadMarker(msg);
	if (err != OT_ERROR_NONE) goto out_free;

	err = otMessageAppend(msg, json, (uint16_t)n);
	if (err != OT_ERROR_NONE) goto out_free;

	otMessageInfo info = { 0 };
	info.mPeerPort = OT_DEFAULT_COAP_PORT;
	if (unicast) {
		memcpy(&info.mPeerAddr, &peer_addr, sizeof(otIp6Address));
	} else {
		err = otIp6AddressFromString(DISCOVERY_GROUP, &info.mPeerAddr);
		if (err != OT_ERROR_NONE) goto out_free;
	}

	/* Async send. Response handler runs in OT thread context when reply
	 * arrives (or never, for NON multicast with no responder). Either
	 * way, this call returns quickly. */
	err = otCoapSendRequest(ctx.ot, msg, &info, on_response, NULL);
	if (err != OT_ERROR_NONE) {
		LOG_WRN("otCoapSendRequest: %d", err);
		goto out_free;
	}

	LOG_INF("push: %s POST /%s, %d B",
		unicast ? "CON" : "NON-mcast",
		COOKIE_RESOURCE_SENSORS_PATH, n);
	return 0;

out_free:
	otMessageFree(msg);
	return -EIO;
}
