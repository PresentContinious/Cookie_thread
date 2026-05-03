#ifndef GATEWAY_COAP_SERVER_H_
#define GATEWAY_COAP_SERVER_H_

/**
 * Open a UDP socket on the CoAP port, register the /sensors/data
 * resource, and start a dedicated thread that runs the recv loop.
 *
 * @return 0 on success, negative errno on failure.
 */
int gateway_coap_server_start(void);

#endif /* GATEWAY_COAP_SERVER_H_ */
