# Open Bootloader for Cookie nRF V2.00

USB serial DFU bootloader, so the Cookie can be reprogrammed over its own USB-C
port with no debugger. This is Nordic's **open_bootloader** from nRF5 SDK 17.1.0,
the same one the nRF52840 Dongle ships with, retargeted to the Cookie's pins.

"Open" means unsigned: the bootloader checks the SHA-256 of an incoming image
against the hash in its init packet, but it does not require an ECDSA signature.
Anyone with physical access to the USB port can reprogram the board. That is the
intended trade-off here, and it matches the Dongle.

The SWD flash below is a **one-time** operation. After it, application updates
go over USB.

## Memory map

| Region | Address | Size |
|---|---|---|
| MBR | `0x00000000` – `0x00000AFF` | 4 kB reserved (`0x0` – `0xFFF`) |
| Application | `0x00001000` – `0x000DCFFF` | 876 kB |
| Application data | `0x000DD000` – `0x000DFFFF` | 12 kB, preserved across updates |
| Bootloader | `0x000E0000` – `0x000FDFFF` | 120 kB reserved, `0xE0000` – `0xE9FD7` used |
| MBR parameters page | `0x000FE000` | 4 kB |
| Bootloader settings page | `0x000FF000` | 4 kB |
| UICR bootloader start address | `0x10001014` | holds `0x000E0000` |
| UICR MBR parameters page | `0x10001018` | holds `0x000FE000` |

Linker script `FLASH` region is `ORIGIN = 0xe0000, LENGTH = 0x1e000`. The
application data area is the stock `NRF_DFU_APP_DATA_AREA_SIZE` of 12288 bytes,
carved off the top of the application region.

The two UICR words are emitted by the linker script and are inside the flashable
hex, so they do not need to be written by hand.

## What differs from the stock example

Source is `<SDK>/examples/dfu/open_bootloader` and is used unmodified. Only the
build configuration in this directory changes.

- `custom_board.h` — Cookie pin map, selected with `-DBOARD_CUSTOM`. LEDs are
  active **high** here and active **low** on the Dongle. Red D2 (P1.10) is
  placed at LED index 1 because `main.c` hard-codes `BSP_LED_1` for its DFU
  indication. Green D3 is P1.15, SW1 is P1.13 with an internal pull-up.
- `sdk_config.h` — copied from the example, three values changed (below).
- `led_softblink.h` — four-line `#include_next` shim that flips
  `LED_SB_INIT_PARAMS_ACTIVE_HIGH` to `true`. The SDK hard-codes it `false`
  outside `sdk_config.h`, which would invert the breathing envelope on
  high-side LEDs.
- `dfu_public_key.c` — the SDK's file is a bare `#error` unless
  `NRF_DFU_DEBUG_VERSION` is defined. The key is unused while signature
  checking is off, but the symbol must exist and must parse as a valid
  secp256r1 point. Local copy carries Nordic's published debug key.
- `Makefile` — `SDK_ROOT` defaults to `/sdk`, this directory goes first on the
  include path, `-DBOARD_PCA10059` becomes `-DBOARD_CUSTOM`, `-fcommon` is added
  for gcc 10, and `-Werror` is dropped since SDK 17.1.0 predates that compiler.
- `open_bootloader_gcc_nrf52.ld` — byte-identical copy of the example's script,
  kept here so the SDK tree stays read-only.

### sdk_config.h changes

| Setting | Stock | Here | Why |
|---|---|---|---|
| `NRF_BL_DFU_ENTER_METHOD_BUTTON` | 0 | 1 | enables the hold-SW1 entry |
| `NRF_BL_DFU_ENTER_METHOD_BUTTON_PIN` | 34 | 45 | 45 = P1.13 = SW1. Absolute pin number, not a BSP index |
| `NRF_BL_DFU_ENTER_METHOD_GPREGRET` | 0 | 1 | lets the application request its own update |

`NRF_BL_DFU_ENTER_METHOD_PINRESET` was already 1 and is untouched. Everything
else is stock.

## Rebuild

Needs Docker with the `cookie-ports` image and the SDK unzipped at
`E:\projects\nrf5_sdk\nRF5_SDK_17.1.0_ddde560`.

```
docker run --rm -v "E:\projects\cookie-thread-mesh:/work" ^
                -v "E:\projects\nrf5_sdk\nRF5_SDK_17.1.0_ddde560:/sdk" ^
                cookie-ports bash /work/ports/bootloader/build.sh
```

Produces `out/cookie_open_bootloader_mbr.hex`, the MBR and the bootloader merged
into one image. `_build/` holds the ELF, the map file, and intermediates.

## Flash once over SWD

Wire an nRF52-DK (or any J-Link) to the Cookie's SWD header, then from Windows:

```
powershell -ExecutionPolicy Bypass -File .\flash_bootloader.ps1
```

The script finds `nrfjprog`, mass-erases with `--recover`, programs the merged
hex with `--verify`, and writes three UICR registers that the hex does not carry.
If `nrfjprog` is not installed it falls back to SEGGER's **J-Link Commander**
(`C:\Program Files\SEGGER\JLink*\JLink.exe`) and performs the same sequence
through NVMC (`ERASEALL`, `loadfile`, UICR word writes, reset). The registers:

- `0x10001200` and `0x10001204` = `0x00000012` — `PSELRESET[0..1]` = P0.18. This
  is what makes SW2 a hardware reset button. SDK 17.1.0 does **not** configure
  this from firmware on the nRF52840, so without it SW2 does nothing and
  pin-reset DFU entry is unavailable.
- `0x10001304` = `0xFFFFFFF5` — `REGOUT0` = 3.0 V. The Cookie is always fed from
  USB-C through VDDH, where the internal regulator defaults to 1.8 V. The
  bootloader does not set this itself: `boards.c` only does so under
  `#if defined(BOARD_PCA10059)`.

`--recover` erases the whole device. Any application already on the board is
gone and has to be re-flashed.

Afterwards the board enumerates as a USB CDC ACM device named
**Open DFU Bootloader** and takes a COM port, with the red LED breathing.

## Entering DFU

Three ways, all enabled:

1. **Press SW2** (reset). `NRF_BL_DFU_ENTER_METHOD_PINRESET` treats a pin reset
   as a DFU request.
2. **Hold SW1 while plugging in USB.** The bootloader samples P1.13 at start-up
   and stays in DFU while it reads low.
3. **From the application**: write `0xB1` to `NRF_POWER->GPREGRET` and reset.
   ```c
   nrf_power_gpregret_set(BOOTLOADER_DFU_START);  /* 0xB0 | 0x01 */
   NVIC_SystemReset();
   ```

The bootloader also enters DFU on its own whenever no valid application is
present, which is the state right after flashing it.

`NRF_BL_DFU_INACTIVITY_TIMEOUT_MS` is 0, so it waits indefinitely rather than
booting the application after a delay.

## Flashing applications over USB

The application must be linked to start at `0x1000` (above the MBR) and must not
run past `0x000DCFFF`.

```
nrfutil pkg generate --hw-version 52 --sd-req 0x00 --application app.hex app.zip
nrfutil dfu usb-serial -pkg app.zip -p COM7
```

`--sd-req 0x00` says no SoftDevice is required, which is right for a
bare-metal, RIOT, Contiki-NG, or OpenThread-without-SoftDevice image.

Two different tools share the `nrfutil` name and the commands above are the
**classic Python nrfutil 6.1.7** syntax. That is the version already vendored in
this repo at `tools/nrfutil/nrfutil.exe`, so no install is needed:

```
tools\nrfutil\nrfutil.exe pkg generate --hw-version 52 --sd-req 0x00 --application app.hex app.zip
tools\nrfutil\nrfutil.exe dfu usb-serial -pkg app.zip -p COM7
```

The current Rust `nrfutil` is a different program and moved these under a
subcommand:

```
nrfutil install nrf5sdk-tools
nrfutil nrf5sdk-tools pkg generate --hw-version 52 --sd-req 0x00 --application app.hex app.zip
nrfutil nrf5sdk-tools dfu usb-serial -pkg app.zip -p COM7
```

Find the port with `[System.IO.Ports.SerialPort]::GetPortNames()` or in Device
Manager under Ports (COM & LPT).
