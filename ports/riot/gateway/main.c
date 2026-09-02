/*
 * RIOT gateway/collector — receive side of the Cookie-only protocol test.
 *
 * Registers a CoAP server resource at "sensors/data" and prints every frame
 * POSTed to it. Run this on one Cookie and the sensor_node on another, both on
 * the same channel/PAN: they form a link-local 6LoWPAN and the sensor's frames
 * arrive here, no Dongle or border router involved. This is the RIOT
 * counterpart of apps/gateway and the V&V step for the RIOT branch.
 *
 * FIRST CUT — verify the gcoap resource/listener struct fields and the
 * response-builder calls against the pinned RIOT release in the first build.
 */

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "net/gcoap.h"
#include "net/utils.h"
#include "shell.h"

#define SENSORS_PATH   "/sensors/data"

static ssize_t sensors_post(coap_pkt_t *pdu, uint8_t *buf, size_t len,
                            coap_request_ctx_t *ctx)
{
    (void)ctx;

    unsigned plen = pdu->payload_len;
    if (plen) {
        /* The payload is the single-line JSON frame the sensor node built. */
        printf("RX %.*s\n", (int)plen, (char *)pdu->payload);
#if defined(LED0_PIN)
        /* Bench indication with no console attached: red toggles on every
         * received frame, so delivery is visible on the board itself. */
        LED0_TOGGLE;
#endif
    }

    /* Acknowledge with 2.04 Changed (no body). */
    gcoap_resp_init(pdu, buf, len, COAP_CODE_CHANGED);
    return coap_opt_finish(pdu, COAP_OPT_FINISH_NONE);
}

static const coap_resource_t resources[] = {
    { SENSORS_PATH, COAP_POST, sensors_post, NULL },
};

static gcoap_listener_t listener = {
    &resources[0],
    ARRAY_SIZE(resources),
    GCOAP_SOCKET_TYPE_UDP,
    NULL,
    NULL,
    NULL,
};

int main(void)
{
    puts("cookie gateway (RIOT): CoAP server on " SENSORS_PATH);

#if defined(LED1_PIN)
    /* Steady green: the gateway is up (no console needed on the bench). */
    LED1_ON;
#endif

    gcoap_register_listener(&listener);

    /* Drop into the shell so addresses/neighbours can be inspected
     * (ifconfig, nib neigh) while the server runs in the background. */
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(NULL, line_buf, sizeof(line_buf));
    return 0;
}
