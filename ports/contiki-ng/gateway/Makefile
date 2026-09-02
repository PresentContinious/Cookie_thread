# Contiki-NG gateway/collector — receive side of the Cookie-only protocol test
# for the Contiki-NG branch. Becomes the RPL DAG root (fd00::1) and runs a CoAP
# server on "sensors/data", printing each frame received. Pair it with the
# Contiki-NG sensor node on a second Cookie; no Dongle needed.
#
# Build (Linux env): CONTIKI=~/contiki-ng TARGET=nrf52840 BOARD=dongle make

CONTIKI_PROJECT = cookie-gateway
all: $(CONTIKI_PROJECT)

CONTIKI ?= $(HOME)/contiki-ng
TARGET  ?= nrf52840
BOARD   ?= dongle

MODULES += os/net/app-layer/coap

include $(CONTIKI)/Makefile.include
