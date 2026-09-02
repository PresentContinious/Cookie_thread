#!/usr/bin/env bash
# Build the Cookie nRF V2.00 Open Bootloader inside the cookie-ports container
# and emit a single flashable image containing the MBR, the bootloader, and the
# UICR words that tell the MBR where the bootloader lives.
#
# Run from the host:
#
#   docker run --rm -v "E:\projects\cookie-thread-mesh:/work" \
#              -v "E:\projects\nrf5_sdk\nRF5_SDK_17.1.0_ddde560:/sdk" \
#              cookie-ports bash /work/ports/bootloader/build.sh
#
# Output: ports/bootloader/out/cookie_open_bootloader_mbr.hex
set -euo pipefail

BL_DIR=${BL_DIR:-/work/ports/bootloader}
SDK_ROOT=${SDK_ROOT:-/sdk}
OUT_DIR="$BL_DIR/out"
BUILD_DIR="$BL_DIR/_build"

MBR_HEX="$SDK_ROOT/components/softdevice/mbr/hex/mbr_nrf52_2.4.1_mbr.hex"
BL_HEX="$BUILD_DIR/nrf52840_xxaa.hex"
MERGED="$OUT_DIR/cookie_open_bootloader_mbr.hex"

# ------------------------------------------------------------ sanity checks --
[ -d "$SDK_ROOT/components/boards" ] || {
    echo "ERROR: nRF5 SDK not found at $SDK_ROOT."
    echo "       Mount it, e.g. -v \"E:\\projects\\nrf5_sdk\\nRF5_SDK_17.1.0_ddde560:/sdk\""
    exit 1
}
[ -f "$MBR_HEX" ] || { echo "ERROR: MBR hex missing: $MBR_HEX"; exit 1; }

# The SDK's components/toolchain/gcc/Makefile.posix points at a hand-installed
# gcc-arm 9.3.1 under /usr/local. This image uses Ubuntu's packaged toolchain,
# so the three variables are overridden on the make command line, where they
# beat the ?= assignments in that file without editing the SDK.
GCC_VERSION=$(arm-none-eabi-gcc -dumpversion)

echo "=== toolchain: arm-none-eabi-gcc $GCC_VERSION"
echo "=== sdk      : $SDK_ROOT"
echo "=== output   : $MERGED"
echo

mkdir -p "$OUT_DIR"

# ------------------------------------------------------------------- build ---
make -C "$BL_DIR" \
     SDK_ROOT="$SDK_ROOT" \
     GNU_INSTALL_ROOT=/usr/bin/ \
     GNU_PREFIX=arm-none-eabi \
     GNU_VERSION="$GCC_VERSION" \
     "$@"

[ -f "$BL_HEX" ] || { echo "ERROR: build produced no $BL_HEX"; exit 1; }

# --------------------------------------------------------------- merge MBR ---
# The bootloader hex already carries its UICR words (0x10001014 bootloader start
# address, 0x10001018 MBR params page) from the linker script. The MBR is the
# first 4 kB of flash and is what actually forwards the reset vector, so both
# have to be programmed. Merging them here means one --program on the host.
#
# srec_cat is given -intel on both inputs and on the output. Both inputs carry
# an execution start address record and srec_cat keeps the first, so the MBR is
# listed first: at reset the core runs the MBR, which then forwards to the
# bootloader address it reads out of UICR 0x10001014.
echo
echo "=== merging MBR + bootloader"
srec_cat "$MBR_HEX" -intel \
         "$BL_HEX"  -intel \
         -o "$MERGED" -intel

# ------------------------------------------------------------------ report ---
echo
echo "=== srec_info: MBR"
srec_info "$MBR_HEX" -intel
echo
echo "=== srec_info: bootloader"
srec_info "$BL_HEX" -intel
echo
echo "=== srec_info: merged image"
srec_info "$MERGED" -intel
echo
echo "=== size"
arm-none-eabi-size "$BUILD_DIR/nrf52840_xxaa.out"
echo
echo "=== done: $MERGED"
echo "Flash it from Windows with ports/bootloader/flash_bootloader.ps1"
