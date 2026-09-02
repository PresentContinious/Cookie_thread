#!/usr/bin/env bash
# Build all four cross-OS binaries (RIOT sensor+gateway, Contiki-NG
# sensor+gateway) inside the cookie-ports image and emit flashable .hex, plus a
# DFU-linked copy of each Cookie image (application at 0x1000, for the Nordic
# Open Bootloader) named <app>_dfu.hex beside the SWD one.
#
# Mirrors .github/workflows/ci-os-ports.yml. Runs every target even if an
# earlier one fails, so one run surfaces every compile error at once (the ports
# are first cuts, expected to need compile-and-fix). Prints a summary and exits
# non-zero if any target failed.
#
# Expects the repo mounted at /work and RIOTBASE / CONTIKI set by the image.
set -u

REPO=/work
RIOTBASE=${RIOTBASE:-/opt/RIOT}
CONTIKI=${CONTIKI:-/opt/contiki-ng}

declare -A RESULT

# COOKIE_ONLY=1 builds only the real hardware and drops the nrf52840dk and
# dongle development targets. Those exist to show the ports are not tied to one
# board, which is a CI concern, not something the measurement campaign reads.
COOKIE_ONLY=${COOKIE_ONLY:-0}

run() {  # run <label> <cmd...>
    local label=$1; shift
    if [ "$COOKIE_ONLY" = "1" ] && [[ "$label" != *cookie* ]]; then
        RESULT[$label]=SKIP
        return
    fi
    echo "::: BUILD $label"
    if "$@"; then RESULT[$label]=OK; else RESULT[$label]=FAIL; fi
}

# ---------------------------------------------------------------- RIOT --------
# nrf52840dk is the dev board (same SoC); cookie_nrf_v200 is the real hardware.
run "riot/sensor_node @ nrf52840dk" \
    make -C "$REPO/ports/riot/sensor_node" BOARD=nrf52840dk RIOTBASE="$RIOTBASE"
run "riot/sensor_node @ cookie_nrf_v200" \
    make -C "$REPO/ports/riot/sensor_node" BOARD=cookie_nrf_v200 RIOTBASE="$RIOTBASE"
run "riot/gateway @ nrf52840dk" \
    make -C "$REPO/ports/riot/gateway" BOARD=nrf52840dk RIOTBASE="$RIOTBASE"
run "riot/gateway @ cookie_nrf_v200" \
    make -C "$REPO/ports/riot/gateway" BOARD=cookie_nrf_v200 RIOTBASE="$RIOTBASE"

# ------------------------------------------------------------ Contiki-NG ------
# Install the out-of-tree Cookie board into the Contiki tree (CI does the same).
# Idempotent: only patch the BOARDS line if it has not been patched yet.
cp -r "$REPO/ports/contiki-ng/board-cookie" \
      "$CONTIKI/arch/platform/nrf52840/cookie" 2>/dev/null || true
if grep -q '^BOARDS = dk dongle$' "$CONTIKI/arch/platform/nrf52840/Makefile.nrf52840"; then
    sed -i 's/^BOARDS = dk dongle$/BOARDS = dk dongle cookie/' \
        "$CONTIKI/arch/platform/nrf52840/Makefile.nrf52840"
fi

run "contiki/sensor_node @ dongle" \
    make -C "$REPO/ports/contiki-ng/sensor_node" CONTIKI="$CONTIKI" TARGET=nrf52840 BOARD=dongle
run "contiki/gateway @ dongle" \
    make -C "$REPO/ports/contiki-ng/gateway" CONTIKI="$CONTIKI" TARGET=nrf52840 BOARD=dongle
# Clean the per-board output name between board builds (Contiki names the ELF
# after the project, not the board).
rm -f "$REPO"/ports/contiki-ng/sensor_node/cookie-sensor-node \
      "$REPO"/ports/contiki-ng/sensor_node/cookie-sensor-node.nrf52840
run "contiki/sensor_node @ cookie" \
    make -C "$REPO/ports/contiki-ng/sensor_node" CONTIKI="$CONTIKI" TARGET=nrf52840 BOARD=cookie
rm -f "$REPO"/ports/contiki-ng/gateway/cookie-gateway \
      "$REPO"/ports/contiki-ng/gateway/cookie-gateway.nrf52840
run "contiki/gateway @ cookie" \
    make -C "$REPO/ports/contiki-ng/gateway" CONTIKI="$CONTIKI" TARGET=nrf52840 BOARD=cookie

# ------------------------------------------------------- DFU variants ---------
# Same four Cookie applications linked at 0x1000 instead of 0x0, for the Nordic
# Open Bootloader (static MBR in the first 4 kB, USB bootloader at the top of
# flash). These are the images that go over the board's own USB-C with nrfutil,
# no SWD and no nRF52-DK in the path. RIOT takes ROM_OFFSET from the board
# Makefile.include, Contiki-NG picks the bootloader linker script, both gated on
# COOKIE_DFU=1. Separate output trees (bin_dfu, build_dfu) so the bare images
# built above survive and the footprint table below still reads them.
rm -f "$REPO"/ports/contiki-ng/sensor_node/cookie-sensor-node \
      "$REPO"/ports/contiki-ng/sensor_node/cookie-sensor-node.nrf52840 \
      "$REPO"/ports/contiki-ng/gateway/cookie-gateway \
      "$REPO"/ports/contiki-ng/gateway/cookie-gateway.nrf52840

run "riot/sensor_node @ cookie_nrf_v200 (dfu)" \
    make -C "$REPO/ports/riot/sensor_node" BOARD=cookie_nrf_v200 RIOTBASE="$RIOTBASE" \
         COOKIE_DFU=1 BINDIRBASE="$REPO/ports/riot/sensor_node/bin_dfu"
run "riot/gateway @ cookie_nrf_v200 (dfu)" \
    make -C "$REPO/ports/riot/gateway" BOARD=cookie_nrf_v200 RIOTBASE="$RIOTBASE" \
         COOKIE_DFU=1 BINDIRBASE="$REPO/ports/riot/gateway/bin_dfu"
run "contiki/sensor_node @ cookie (dfu)" \
    make -C "$REPO/ports/contiki-ng/sensor_node" CONTIKI="$CONTIKI" TARGET=nrf52840 \
         BOARD=cookie COOKIE_DFU=1 BUILD_DIR=build_dfu
rm -f "$REPO"/ports/contiki-ng/gateway/cookie-gateway \
      "$REPO"/ports/contiki-ng/gateway/cookie-gateway.nrf52840
run "contiki/gateway @ cookie (dfu)" \
    make -C "$REPO/ports/contiki-ng/gateway" CONTIKI="$CONTIKI" TARGET=nrf52840 \
         BOARD=cookie COOKIE_DFU=1 BUILD_DIR=build_dfu
# The app-directory copies Contiki leaves behind now hold DFU images, which
# would be mistaken for the SWD ones. Drop them; the build trees are the truth.
rm -f "$REPO"/ports/contiki-ng/sensor_node/cookie-sensor-node \
      "$REPO"/ports/contiki-ng/sensor_node/cookie-sensor-node.nrf52840 \
      "$REPO"/ports/contiki-ng/gateway/cookie-gateway \
      "$REPO"/ports/contiki-ng/gateway/cookie-gateway.nrf52840

# -------------------------------------------------- ELF -> flashable HEX ------
echo "::: objcopy ELF -> HEX"
while IFS= read -r f; do
    arm-none-eabi-objcopy -O ihex "$f" "${f%.elf}.hex" && echo "  hex: ${f%.elf}.hex"
done < <(find "$REPO/ports/riot" "$REPO/ports/contiki-ng" -name '*.elf' 2>/dev/null)

# Park each DFU image next to its SWD counterpart under a name that says which
# one it is, so the packaging step (scripts/make_dfu_zips.ps1) can pick the DFU
# set out of the tree by name alone.
dfu_copy() {  # dfu_copy <built-hex> <destination-hex>
    [ -f "$1" ] || return 0
    cp "$1" "$2" && echo "  dfu: $2"
}
dfu_copy "$REPO/ports/riot/sensor_node/bin_dfu/cookie_nrf_v200/cookie_sensor_node_riot.hex" \
         "$REPO/ports/riot/sensor_node/bin/cookie_nrf_v200/cookie_sensor_node_riot_dfu.hex"
dfu_copy "$REPO/ports/riot/gateway/bin_dfu/cookie_nrf_v200/cookie_gateway_riot.hex" \
         "$REPO/ports/riot/gateway/bin/cookie_nrf_v200/cookie_gateway_riot_dfu.hex"
dfu_copy "$REPO/ports/contiki-ng/sensor_node/build_dfu/nrf52840/cookie/cookie-sensor-node.hex" \
         "$REPO/ports/contiki-ng/sensor_node/build/nrf52840/cookie/cookie-sensor-node_dfu.hex"
dfu_copy "$REPO/ports/contiki-ng/gateway/build_dfu/nrf52840/cookie/cookie-gateway.hex" \
         "$REPO/ports/contiki-ng/gateway/build/nrf52840/cookie/cookie-gateway_dfu.hex"

# ------------------------------------------------------------- footprint ------
# RIOT's own make prints arm-none-eabi-size at the end of every target, but
# Contiki-NG's does not, so the two ports could not be read on the same terms
# from one run. Emit both here on the real board only: the dev-board builds
# exist to prove the ports are not board-locked, not to be compared.
# flash = text + data (what is programmed), RAM = data + bss (what is claimed).
echo
echo "=================== BINARY FOOTPRINT, Cookie nRF V2.00 ==================="
printf '  %-34s %12s %12s\n' "image" "flash (B)" "RAM (B)"
printf '  %-34s %12s %12s\n' "----------------------------------" \
       "------------" "------------"
footprint() {  # footprint <label> <elf>
    [ -f "$2" ] || { printf '  %-34s %12s %12s\n' "$1" "--" "--"; return; }
    read -r text data bss _ < <(arm-none-eabi-size "$2" | tail -1)
    printf '  %-34s %12d %12d\n' "$1" "$((text + data))" "$((data + bss))"
}
footprint "RIOT       sensor node" \
  "$REPO/ports/riot/sensor_node/bin/cookie_nrf_v200/cookie_sensor_node_riot.elf"
footprint "RIOT       gateway" \
  "$REPO/ports/riot/gateway/bin/cookie_nrf_v200/cookie_gateway_riot.elf"
footprint "Contiki-NG sensor node" \
  "$REPO/ports/contiki-ng/sensor_node/build/nrf52840/cookie/cookie-sensor-node.elf"
footprint "Contiki-NG gateway" \
  "$REPO/ports/contiki-ng/gateway/build/nrf52840/cookie/cookie-gateway.elf"
echo "========================================================================="

# --------------------------------------------------------------- summary ------
echo
echo "================ BUILD SUMMARY ================"
fail=0
for label in "${!RESULT[@]}"; do :; done
# stable order
order=(
  "riot/sensor_node @ nrf52840dk" "riot/sensor_node @ cookie_nrf_v200"
  "riot/gateway @ nrf52840dk" "riot/gateway @ cookie_nrf_v200"
  "contiki/sensor_node @ dongle" "contiki/gateway @ dongle"
  "contiki/sensor_node @ cookie" "contiki/gateway @ cookie"
  "riot/sensor_node @ cookie_nrf_v200 (dfu)" "riot/gateway @ cookie_nrf_v200 (dfu)"
  "contiki/sensor_node @ cookie (dfu)" "contiki/gateway @ cookie (dfu)"
)
for label in "${order[@]}"; do
    state=${RESULT[$label]:-SKIP}
    [ "$COOKIE_ONLY" = "1" ] && [ "$state" = "SKIP" ] && continue
    printf '  %-36s %s\n' "$label" "$state"
    [ "$state" = "FAIL" ] && fail=1
done
echo "=============================================="
exit $fail
