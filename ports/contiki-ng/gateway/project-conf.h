/*
 * Contiki-NG gateway configuration. Same 802.15.4 PHY/MAC parameters as the
 * sensor node so the two associate; this node is additionally the RPL root.
 */

#ifndef PROJECT_CONF_H
#define PROJECT_CONF_H

#define IEEE802154_CONF_DEFAULT_CHANNEL   15
#define IEEE802154_CONF_PANID             0xABCD

/* Must match the sensor node: coap_parse_message clamps incoming payloads to
 * the RECEIVER's chunk size, so at the old 256 every pad256/pad512 frame was
 * truncated before the handler saw it. */
#define COAP_MAX_CHUNK_SIZE               768

/* Same TX-queue headroom as the sensor (DIO/DAO-ACK bursts + safety). */
#define QUEUEBUF_CONF_NUM                 16

/* The root advertises the DAO lifetime in its DIO DAG_CONF option and the
 * leaf ADOPTS it — setting it on the sensor node would be a no-op. 0xFF is
 * RPL infinite lifetime: the duty-cycled leaf never has to wake just to
 * refresh its DAO registration. */
#define RPL_CONF_DEFAULT_LIFETIME         0xFF

#endif /* PROJECT_CONF_H */
