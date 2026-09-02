/*
 * Project configuration for the Contiki-NG Cookie sensor node.
 *
 * Radio settings mirror the Zephyr/OpenThread dataset where they map across
 * stacks: channel 15 and PAN ID 0xABCD (43981). Contiki-NG runs its own uIP
 * 6LoWPAN/RPL stack, not Thread, so only the 802.15.4 PHY/MAC parameters are
 * shared — the comparison is the same workload, not the same network.
 *
 * Low-power note: the nrf52840 platform defaults to CSMA with the radio kept
 * on. The measurement builds duty-cycle the radio manually (RADIO_SLEEP in
 * cookie-sensor-node.c: join the DAG radio-on, then MAC off between cycles),
 * the application-level counterpart of the Zephyr Thread SED. A protocol-
 * level alternative is TSCH (MAKE_MAC=MAKE_MAC_TSCH); noted as future work.
 */

#ifndef PROJECT_CONF_H
#define PROJECT_CONF_H

#define IEEE802154_CONF_DEFAULT_CHANNEL   15
#define IEEE802154_CONF_PANID             0xABCD

/* Leaf-only RPL: the sensor node never multicasts DIOs of its own. Trickle
 * transmissions between reporting cycles re-latch the radio into RX for
 * seconds at a time — a leaf keeps its parent and stays silent unless it
 * has data to send. Both spellings so it holds for RPL-Lite and
 * RPL-Classic builds. */
#define RPL_CONF_DEFAULT_LEAF_ONLY        1
#define RPL_CONF_LEAF_ONLY                1

/* CoAP client only — no server resources on the node. Sized for the padded
 * frame of the packet-size sweep (up to 512 pad bytes on top of the sensor
 * set); 6LoWPAN fragments the resulting datagram, which is the effect the
 * sweep measures. */
#define COAP_MAX_CHUNK_SIZE               768

/* The pad512 datagram needs ~9 6LoWPAN fragments, all enqueued into CSMA
 * before the first one transmits. The default queuebuf pool of 8 makes
 * sicslowpan abort the whole datagram (silent LOG_WARN, nothing on air). */
#define QUEUEBUF_CONF_NUM                 16

/* RPL-Lite probing unicasts a DIO to the parent every 45-135 s — a
 * between-cycle transmission that re-latches the radio into RX. The data
 * frame every cycle keeps the parent's link stats fresh, so probing buys
 * nothing on this bench. */
#define RPL_CONF_WITH_PROBING             0

/* With the radio off between cycles the leaf rarely hears DIOs, so its DAG
 * lifetime (8 h default, refreshed only by DIO reception) would decay and
 * the node would poison and leave the DAG mid-soak. Give long captures a
 * full day of margin (value in minutes). */
#define RPL_CONF_DAG_LIFETIME             (24 * 60)

#endif /* PROJECT_CONF_H */
