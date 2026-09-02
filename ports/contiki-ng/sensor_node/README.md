# Cookie sensor node — Contiki-NG port

The third cross-OS counterpart of `apps/sensor_node` (Zephyr) and the RIOT
port. Same workload, same wire format, Contiki-NG's uIP 6LoWPAN stack.

## What it does

Periodic cycle (`REPORT_INTERVAL_SEC`): read SHTC3 (temperature, humidity) →
build the **same JSON frame** the other ports emit (plus `"os":"contiki-ng"`) →
CoAP NON `POST sensors/data` over uIP 6LoWPAN → wait for the next cycle.

Minimal representative node, not a full mesh — it carries the energy-relevant
workload for the Chapter 6 comparison and does not join the Zephyr Thread mesh.

## Design choices

- **Network:** Contiki-NG native uIP 6LoWPAN/RPL + CoAP, not Thread.
- **Sensor:** SHTC3 read with a raw I2C transfer (nrfx TWIM), commands
  documented inline (wake 0x3517, measure 0x7866, sleep 0xB098).
- **Low power:** the default nrf52840 MAC (CSMA) keeps the radio on. The
  duty-cycled build (`RADIO_SLEEP=1`, the default) joins the RPL DAG radio-on
  first and then turns the MAC off between reporting cycles, with a re-off
  watchdog against RPL control traffic re-latching the radio. See
  `../../MEASUREMENT.md`.

## Building (needs a Linux env — WSL/Ubuntu or cloud CI)

```sh
# one-time
git clone --recurse-submodules https://github.com/contiki-ng/contiki-ng.git ~/contiki-ng
sudo apt install -y gcc-arm-none-eabi make git python3 srecord

# build for the Cookie board (BOARD=dongle also works, same SoC)
cd ports/contiki-ng/sensor_node
CONTIKI=~/contiki-ng TARGET=nrf52840 BOARD=cookie make
```

`BOARD=cookie` needs `../board-cookie` copied into the Contiki tree first
(`arch/platform/nrf52840/cookie`); the Docker image in `../../docker/` does
this automatically and builds without WSL.

## Flashing (from Windows, board on the nRF52-DK)

Build the `.hex` in Linux, flash from Windows with the J-Link tooling in
`C:\ncs`:

```powershell
nrfjprog --program cookie-sensor-node.nrf52840 --chiperase --verify -r
```

## Notes

- The node also reads the IMU (ICM-20648) inside the sensor window and puts it
  back to sleep after each read, matching the other ports.
- Reports go to the gateway at `fd00::1` (RPL root), verified on hardware.
- Measurement build knobs (`MEAS_MARKERS`, `RADIO_SLEEP`, `PAYLOAD_PAD`) are
  described in `../../MEASUREMENT.md`.
