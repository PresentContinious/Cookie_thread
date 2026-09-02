# Cross-OS measurement instrumentation (Chapter 6 campaign)

The same instrumentation, with the same semantics, exists in all three ports
(Zephyr `apps/sensor_node`, RIOT `ports/riot/sensor_node`, Contiki-NG
`ports/contiki-ng/sensor_node`), so oscilloscope timings and currents are
directly comparable across operating systems.

## Phase markers (scope channels)

Two GPIO markers on the LED pads, active only in measurement builds:

| Marker | Pad | Semantics |
|--------|-----|-----------|
| RED (P1.10, D2) | wake → transmit handed off | Rises the moment the cycle starts (scope trigger), falls right after the transmit call returns. **Pulse width = the OS latency from wake to effective transmit** — the scheduler-overhead figure. |
| GREEN (P1.15, D3) | sensor window | High across every sensor access (SHTC3 + ICM-20648). Width = sensor time; TP3 amplitude during this window = sensor current. |

Scope setup: CH1 on the red LED pad (trigger, rising edge, NORMAL/SINGLE),
CH2 on TP3 (INA333 out, ~10 mV/mA). The red edge fires every cycle, so the
short active window is captured reliably.

The markers sink a known constant current while high (red ~1.1 mA through
R17, green ~0.5 mA through R20). Subtract it from TP3 amplitudes, or use the
marker-free ("clean") build for pure-current numbers.

Enable/disable:
- Zephyr: `CONFIG_NODE_MEAS_LED` (overlay `overlays/meas_led.conf`)
- RIOT / Contiki-NG: `make MEAS_MARKERS=1|0`

## Radio duty-cycling (deep sleep on all three OSes)

Zephyr sleeps the radio through the Thread SED role. RIOT and Contiki-NG get
the same behaviour at application level with `make RADIO_SLEEP=1|0`
(default 1): the radio powers down between reporting cycles and wakes at the
red-marker rising edge, so the wake-to-transmit metric means the same thing
on all three OSes. Details per port:
- RIOT: `NETOPT_STATE_SLEEP` (fallback `STANDBY`) on the GNRC netif after a
  300 ms post-TX linger (GNRC transmits from its own thread); wake sets
  `IDLE`. A boot probe blinks the **red LED 10x fast** if the driver rejects
  both low-power states — a flat ~12 mA baseline then means driver limit,
  not build mix-up.
- Contiki-NG: joins the RPL DAG radio-on first (up to 120 s), then
  `NETSTACK_MAC.off()` between cycles with the same 300 ms post-TX linger.
- The `rxon` images (`RADIO_SLEEP=0`) keep the stack default (radio always
  in RX) — that build is the RX-current measurement.

## Packet-size sweep (6LoWPAN fragmentation)

Every port can append a `"pad":"xxx..."` field of N bytes to the JSON frame,
growing the CoAP payload so 6LoWPAN fragments it into several 802.15.4
frames. Campaign points: **0 / 64 / 256 / 512** bytes. Measure the TX burst
on TP3 per size; the burst count/length grows with fragmentation.

- Zephyr: `-DCONFIG_NODE_PAYLOAD_PAD_BYTES=N` + overlay `overlays/pad_sweep.conf`
- RIOT / Contiki-NG: `make PAYLOAD_PAD=N` (CI builds these variants, 5 s interval)

## Per-sensor current

All three ports read the SHTC3 (temperature + humidity) and the ICM-20648
accelerometer inside the green marker window. To isolate one sensor:
- Zephyr: device-tree overlays (`overlays/no_imu.overlay` disables the ICM;
  an SHTC3-off overlay is the same pattern).
- RIOT / Contiki-NG: the reads are independent; a missing/failed sensor is
  skipped automatically, and each read can be commented per build.
- The board has **no LDR** — the measurable sensor set is temperature,
  humidity, accelerometer (and gyro on Zephyr).

## Receive current

Radio-on reception is the default state of RIOT (GNRC) and Contiki-NG (CSMA)
and of the Zephyr AUTO profile. Procedure: put the node in its radio-on
state, have a second node transmit frames at a known rate, and read the node
current with the series meter with traffic present vs absent.

## Delivery / network for the measurements

- Zephyr: Thread mesh (gateway = Leader; USB-CDC console on the gateway).
- Contiki-NG: RPL — the node joins the gateway (RPL root, `fd00::1`) and
  POSTs each cycle. Verified on hardware.
- RIOT: reports go to the link-local all-nodes group `ff02::1`; the send now
  sets the egress interface explicitly (`remote.netif`), which the first
  hardware run was missing — that is why it produced no transmit. One-hop
  bench topology: RIOT gateway + RIOT node on channel 15.

## Build matrix (CI artifacts, `dist/`)

Per OS (RIOT, Contiki-NG), sensor node for the Cookie board:
- `*_mark_pad{0,64,256,512}_int5.hex` — markers on, pad sweep, 5 s interval
- `*_clean_int30.hex` — markers off, no pad, 30 s interval (pure current)

Zephyr equivalents build locally with `scripts/build_all.py`: profile overlays
+ `measure.conf` + `meas_led.conf` + `pad_sweep.conf` +
`-DCONFIG_NODE_PAYLOAD_PAD_BYTES=N`.
