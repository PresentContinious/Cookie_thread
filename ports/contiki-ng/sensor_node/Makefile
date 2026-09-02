# Contiki-NG port of the Cookie sensor node (cross-OS energy comparison).
#
# Build (in a Linux env, with a Contiki-NG checkout):
#   CONTIKI=$HOME/contiki-ng TARGET=nrf52840 BOARD=dongle make
#
# BOARD note: Contiki-NG ships an nrf52840 "dongle" board; the Cookie reuses
# the same SoC. The SHTC3 sits on a board-specific I2C bus (SCL P1.08,
# SDA P0.11), wired in cookie-sensor-node.c via the platform I2C/nrfx.

CONTIKI_PROJECT = cookie-sensor-node
all: $(CONTIKI_PROJECT)

CONTIKI ?= $(HOME)/contiki-ng
TARGET  ?= nrf52840
BOARD   ?= dongle

# CoAP application layer (client side).
MODULES += os/net/app-layer/coap

# Reporting cadence, matched to the Zephyr and RIOT ports.
REPORT_INTERVAL_SEC ?= 30
CFLAGS += -DREPORT_INTERVAL_SEC=$(REPORT_INTERVAL_SEC)
CFLAGS += -DNODE_OS_TAG=\"contiki-ng\"

# --- Measurement instrumentation (Chapter 6 campaign) ------------------------
# PAYLOAD_PAD: bytes of "pad" filler in every frame (packet-size sweep,
# 0 / 64 / 256 / 512). MEAS_MARKERS: phase markers on the LED pads (red =
# wake->transmit-returned, green = sensor window); set 0 for the cleanest
# pure-current build.
PAYLOAD_PAD ?= 0
CFLAGS += -DPAYLOAD_PAD=$(PAYLOAD_PAD)
MEAS_MARKERS ?= 1
CFLAGS += -DMEAS_MARKERS=$(MEAS_MARKERS)
# RADIO_SLEEP: power the radio down between reporting cycles (default), so the
# node deep-sleeps like the Zephyr Thread SED. RADIO_SLEEP=0 keeps the CSMA
# default (radio always on) — that build is the RX-current image.
RADIO_SLEEP ?= 1
CFLAGS += -DRADIO_SLEEP=$(RADIO_SLEEP)

include $(CONTIKI)/Makefile.include
