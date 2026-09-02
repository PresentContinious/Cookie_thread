/*
 * CoAP push client: sensor frame -> POST /sensors/data on a discovered gateway.
 */

#ifndef COOKIE_PROTO_COAP_CLIENT_H_
#define COOKIE_PROTO_COAP_CLIENT_H_

#include "cookie_proto/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise CoAP transport. Call once after the Thread interface is up
 * (otThreadSetEnabled(true) and at least the IPv6 link-local address ready).
 * @return 0 on success, negative errno on failure.
 */
int cookie_coap_init(void);

/**
 * Push a sensor frame.
 *  - First call after init (or after a cache miss): NON multicast to ff03::1
 *    for gateway discovery. The first responding peer is cached as the gateway.
 *  - Cached path: CON unicast to the cached gateway address.
 *  - On CON timeout (no ACK after retransmits): cache cleared, next call
 *    will rediscover via multicast.
 *
 * Blocking: returns when a response is received, or after the CoAP
 * retransmit budget is exhausted.
 *
 * @return 0 on success, negative errno on hard failure.
 */
int cookie_coap_push_frame(const struct sensor_frame *f);

/**
 * Cancel any pending retransmissions and put the transport into a state
 * suitable for entering deep sleep. SED loop calls this before k_sleep.
 */
void cookie_coap_quiesce(void);

/**
 * Force discovery on the next push (e.g. after a long sleep).
 */
void cookie_coap_invalidate_gateway(void);

#ifdef __cplusplus
}
#endif

#endif /* COOKIE_PROTO_COAP_CLIENT_H_ */
