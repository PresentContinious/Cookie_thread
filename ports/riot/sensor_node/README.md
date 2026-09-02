# Cookie sensor node — RIOT port

The cross-OS comparison counterpart of `apps/sensor_node` (Zephyr). Same
workload, same wire format, different operating system and network stack.

## What it does

One reporting cycle: wake → read SHTC3 (temperature, humidity) → build the
**same JSON frame** the Zephyr node emits → CoAP `POST sensors/data` over
GNRC 6LoWPAN → sleep `REPORT_INTERVAL_SEC` seconds. The frame carries an
additive `"os":"riot"` tag; everything else matches the Zephyr schema, so the
OS-agnostic PC tool ingests RIOT frames unchanged.

This is the **minimal representative node** for the energy comparison, not a
full mesh. It carries the energy-relevant workload (sensor read + radio TX +
sleep), which is what Chapter 6 measures across operating systems. It does not
join the Zephyr Thread mesh — the comparison is the same workload per OS, not
the same network.

## Design choices

- **Network:** RIOT's native GNRC 6LoWPAN + gcoap, not Thread. Thread is the
  Zephyr/OpenThread branch; the differing network stack is itself part of the
  comparison.
- **Sensor:** RIOT's `shtcx` driver (covers SHTC3) over I2C.
- **Timing / low power:** `ztimer` for the cycle, `pm_layered` for sleep — the
  RIOT counterpart of Zephyr's PM deep sleep.
- **Reporting target:** link-local all-nodes `ff02::1`. The transmit always
  happens (real radio energy for the measurement) whether or not a collector
  listens. Point it at a RIOT border router's address for end-to-end delivery.

## Building (needs a Linux env — WSL/Ubuntu or cloud CI)

This does **not** build on native Windows. RIOT is Make + POSIX. Build inside
WSL/Ubuntu (or a Linux CI runner):

```sh
# one-time
git clone https://github.com/RIOT-OS/RIOT.git ~/RIOT
sudo apt install -y gcc-arm-none-eabi make git

# build for the Cookie board (nrf52840dk also works, same SoC)
cd ports/riot/sensor_node
RIOTBASE=~/RIOT BOARD=cookie_nrf_v200 make
```

The build produces `bin/cookie_nrf_v200/cookie_sensor_node_riot.{elf,hex}`.
The Docker image in `../../docker/` builds the same targets without WSL.

## Flashing (from Windows, board on the nRF52-DK)

Build elsewhere, flash here. The `.hex` from the Linux build is flashed with
the J-Link tooling already installed under `C:\ncs`:

```powershell
nrfjprog --program cookie_sensor_node_riot.hex --chiperase --verify -r
```

## Notes

- The Cookie board definition lives in `../boards/cookie_nrf_v200` (pinout,
  LEDs, UICR REGOUT0 restore at boot).
- The node also reads the IMU (ICM-20648) inside the sensor window and puts it
  back to sleep after each read, matching the Zephyr node.
- Measurement build knobs (`MEAS_MARKERS`, `RADIO_SLEEP`, `PAYLOAD_PAD`) are
  described in `../../MEASUREMENT.md`.
