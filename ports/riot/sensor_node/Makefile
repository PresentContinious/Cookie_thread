# RIOT port of the Cookie sensor node — minimal sense -> CoAP -> sleep cycle,
# the cross-OS comparison counterpart of apps/sensor_node (Zephyr).
#
# Build (in a Linux env, e.g. WSL/Ubuntu, with RIOTBASE pointing at a RIOT
# checkout and gcc-arm-none-eabi installed):
#
#   RIOTBASE=$HOME/RIOT BOARD=nrf52840dk make
#
# BOARD note: the Cookie nRF V2.00 has no RIOT board definition yet, so we
# develop against nrf52840dk (same SoC, same radio). A custom RIOT board for
# the Cookie pinout (SDA=P0.11, SCL=P1.08, SHTC3@0x70, LEDs) is added later;
# the application code does not change, only the board's periph_conf.h.

APPLICATION = cookie_sensor_node_riot
BOARD ?= nrf52840dk
RIOTBASE ?= $(CURDIR)/../../../RIOT

# Out-of-tree Cookie board lives in ports/riot/boards. Build for the real
# hardware with: BOARD=cookie_nrf_v200 make
EXTERNAL_BOARD_DIRS += $(CURDIR)/../boards

# Reporting cadence in seconds — matches CONFIG_NODE_REPORT_INTERVAL_SEC on
# the Zephyr side so the two ports run the same duty cycle when measured.
REPORT_INTERVAL_SEC ?= 30
CFLAGS += -DREPORT_INTERVAL_SEC=$(REPORT_INTERVAL_SEC)

# Identify the producing OS in the JSON frame (additive field; the Zephyr
# parser and the PC tool ignore/preserve unknown keys).
CFLAGS += -DNODE_OS_TAG=\"riot\"

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
# node deep-sleeps like the Zephyr Thread SED. RADIO_SLEEP=0 keeps the GNRC
# default (radio always in RX) — that build is the RX-current image.
RADIO_SLEEP ?= 1
CFLAGS += -DRADIO_SLEEP=$(RADIO_SLEEP)

# Room for the padded frame in the gcoap PDU (default is far smaller).
CFLAGS += -DCONFIG_GCOAP_PDU_BUF_SIZE=768

# THE HardFault cause (bench 2026-07-15, confirmed on hardware + nm): the
# frame builder uses %f, but RIOT/newlib-nano has NO float printf unless this
# module is added — without it _svfprintf_r is aliased to the integer-only
# _svfiprintf_r and %f jumps through an unlinked _printf_float pointer to a
# wild address (stacked PC 0xC000108C). The gateway never formats a float,
# which is why only the sensor crashed. This one line is the actual fix.
USEMODULE += printf_float

# Main-thread stack. printf_float's _dtoa path is stack-heavy, so keep real
# headroom (the earlier HardFault was the wild jump above, not depth, but the
# float formatter genuinely needs the room). 256 KiB RAM, cost is trivial.
CFLAGS += -DTHREAD_STACKSIZE_MAIN=12288

# --- Networking: native RIOT 6LoWPAN stack (GNRC), not Thread ---------------
# The cross-OS comparison measures the same workload, not the same network.
# Channel 15 / PAN 0xABCD to match the Zephyr Thread dataset and the
# Contiki-NG port, so all three OSes share the same RF environment (RIOT's
# own defaults are channel 26 / PAN 0x0023).
CFLAGS += -DCONFIG_IEEE802154_DEFAULT_CHANNEL=15
CFLAGS += -DCONFIG_IEEE802154_DEFAULT_PANID=0xABCD
USEMODULE += netdev_default
USEMODULE += auto_init_gnrc_netif
USEMODULE += gnrc_ipv6_default
USEMODULE += gnrc_icmpv6_echo

# --- CoAP client (gcoap) ----------------------------------------------------
USEMODULE += gcoap

# --- Sensors ------------------------------------------------------------------
# SHTC3 via RIOT's shtcx driver; ICM-20648 accelerometer via a minimal raw
# periph_i2c read in main.c (RIOT has no ICM-20648 driver).
USEMODULE += shtcx
FEATURES_REQUIRED += periph_i2c

# --- Timing -----------------------------------------------------------------
USEMODULE += ztimer
USEMODULE += ztimer_msec
# Stop the 16 MHz TIMER peripheral when no microsecond timer is pending
# (the submac's ack timers), instead of letting it free-run through the
# sleep phase. Lowers the between-cycles floor toward the Zephyr SED.
USEMODULE += ztimer_ondemand
# Note: the nRF52 idle thread already enters a low-power state via WFI between
# reporting cycles. Explicit pm_layered tuning is a later step (it needs
# PM_NUM_MODES wired for this CPU) and is not required for the protocol bring-up.

# --- Helpers ----------------------------------------------------------------
USEMODULE += fmt
USEMODULE += od

# Keep the build quiet about not-yet-wired peripherals during bring-up.
DEVELHELP ?= 1

include $(RIOTBASE)/Makefile.include
