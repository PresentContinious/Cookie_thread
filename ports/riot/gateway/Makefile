# RIOT gateway/collector for the Cookie sensor node — the receive side of the
# Cookie-only protocol test (one Cookie runs sensor_node, one runs this). It is
# a CoAP server that registers the "sensors/data" resource and prints each
# frame it receives. Mirrors apps/gateway (Zephyr) for the RIOT branch.
#
# Build (Linux env): RIOTBASE=~/RIOT BOARD=cookie_nrf_v200 make
# (or BOARD=nrf52840dk to develop without the custom board)

APPLICATION = cookie_gateway_riot
BOARD ?= nrf52840dk
RIOTBASE ?= $(CURDIR)/../../../RIOT
EXTERNAL_BOARD_DIRS += $(CURDIR)/../boards

# Native RIOT 6LoWPAN stack + CoAP server. Channel 15 / PAN 0xABCD to match
# the sensor node (and the other two OSes' RF environment).
CFLAGS += -DCONFIG_IEEE802154_DEFAULT_CHANNEL=15
CFLAGS += -DCONFIG_IEEE802154_DEFAULT_PANID=0xABCD
USEMODULE += netdev_default
USEMODULE += auto_init_gnrc_netif
USEMODULE += gnrc_ipv6_default
USEMODULE += gnrc_icmpv6_echo
USEMODULE += gcoap
# gcoap's server receive buffer defaults to 128 B; every sensor frame is
# bigger (base JSON ~150-190 B, pad sweeps up to ~700 B), so without this the
# gateway truncates each POST, answers 4.13 and never calls the handler —
# no RX print, no red-LED toggle. Match the sensor node's PDU size.
CFLAGS += -DCONFIG_GCOAP_PDU_BUF_SIZE=768

# Shell, so the node's addresses can be inspected during bring-up.
USEMODULE += shell
USEMODULE += shell_cmds_default
USEMODULE += ps

USEMODULE += ztimer
USEMODULE += ztimer_msec
USEMODULE += fmt

DEVELHELP ?= 1

include $(RIOTBASE)/Makefile.include
