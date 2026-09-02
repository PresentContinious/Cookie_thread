<#
Build the Cookie nRF V2.00 Zephyr images and print a binary footprint table in
the same format as ports/docker/build-ports.sh, so all three operating systems
can be read on identical terms from a console.

The two ports build inside the cookie-ports Docker image. Zephyr cannot: it
needs the nRF Connect SDK, which is a Windows-side install. This script is the
Zephyr half of the same measurement.

    powershell -ExecutionPolicy Bypass -File scripts\build_zephyr_cookie.ps1
    powershell -ExecutionPolicy Bypass -File scripts\build_zephyr_cookie.ps1 -Pristine
    powershell -ExecutionPolicy Bypass -File scripts\build_zephyr_cookie.ps1 -Dfu

-Dfu builds the same two images for the Nordic Open Bootloader instead: linked
at 0x1000 above the static MBR, console on USB-CDC, into build_z\*_dfu. Package
them with scripts\make_dfu_zips.ps1.

flash = text + data (what is programmed), RAM = data + bss (what is claimed).
#>
param([switch]$Pristine, [switch]$Dfu)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

# ------------------------------------------------------------- toolchain ----
# Resolved rather than hardcoded: the SDK version and the toolchain hash both
# move, and a stale path is a silent build failure.
$ncs = Get-ChildItem "C:\ncs" -Directory -Filter "v*" -ErrorAction SilentlyContinue |
       Sort-Object Name -Descending | Select-Object -First 1
$tc  = Get-ChildItem "C:\ncs\toolchains" -Directory -ErrorAction SilentlyContinue |
       Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $ncs) { throw "No nRF Connect SDK found under C:\ncs" }
if (-not $tc)  { throw "No NCS toolchain found under C:\ncs\toolchains" }

$env:PATH = (@(
  "$($tc.FullName)", "$($tc.FullName)\mingw64\bin", "$($tc.FullName)\bin",
  "$($tc.FullName)\opt\bin", "$($tc.FullName)\opt\bin\Scripts",
  "$($tc.FullName)\nrfutil\bin",
  "$($tc.FullName)\opt\zephyr-sdk\arm-zephyr-eabi\bin"
) -join ';') + ';' + $env:PATH
$env:PYTHONPATH = "$($tc.FullName)\opt\bin;$($tc.FullName)\opt\bin\Lib;$($tc.FullName)\opt\bin\Lib\site-packages"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR   = "$($tc.FullName)\opt\zephyr-sdk"
$env:ZEPHYR_BASE              = "$($ncs.FullName)\zephyr"

Write-Host "Zephyr version:"
Write-Host "$($ncs.Name)  (toolchain $($tc.Name))"

# --------------------------------------------------------------- targets ----
$targets = @(
  @{ label = "Zephyr     sensor node"
     app   = "apps\sensor_node"
     extra = @("overlays\profile_sed.conf")
     dfu   = @("overlays\usb_console.conf", "overlays\dfu.conf")
     dtc   = @("overlays\usb_console.overlay", "overlays\dfu.overlay") },
  @{ label = "Zephyr     gateway"
     app   = "apps\gateway"
     extra = @()
     dfu   = @("usb_console.conf", "dfu.conf")
     dtc   = @("usb_console.overlay", "dfu.overlay") }
)

# The DFU fragments layer on top of whatever the target already builds, and
# land in their own build directories, so the default footprint table keeps
# reading the plain SWD images.
if ($Dfu) {
    foreach ($t in $targets) {
        $t.extra = @($t.extra) + $t.dfu
        $t.label = "$($t.label) (DFU)"
    }
}

$outRoot = Join-Path (Split-Path -Parent $repo) "build_z"
$results = @{}

foreach ($t in $targets) {
    $name  = ($t.app -split '\\')[-1]
    if ($Dfu) { $name = "${name}_dfu" }
    $bdir  = Join-Path $outRoot $name
    $src   = Join-Path $repo $t.app
    if ($Pristine -and (Test-Path $bdir)) { Remove-Item $bdir -Recurse -Force }

    Write-Host ""
    Write-Host "::: BUILD zephyr/$name @ cookie_nrf_v200"

    $cfg = @("-B", $bdir, "-S", $src, "-GNinja",
             "-DBOARD=cookie_nrf_v200/nrf52840")
    if ($t.extra.Count) {
        $paths = $t.extra | ForEach-Object { Join-Path $src $_ }
        $cfg += "-DEXTRA_CONF_FILE=$($paths -join ';')"
    }
    if ($Dfu) {
        $paths = $t.dtc | ForEach-Object { Join-Path $src $_ }
        $cfg += "-DEXTRA_DTC_OVERLAY_FILE=$($paths -join ';')"
    }

    if (-not (Test-Path (Join-Path $bdir "build.ninja"))) { & cmake @cfg | Out-Host }
    & ninja -C $bdir | Out-Host
    $results[$t.label] = if ($LASTEXITCODE -eq 0) { "OK" } else { "FAIL" }
    $t.elf = Join-Path $bdir "zephyr\zephyr.elf"
}

# ------------------------------------------------------------- footprint ----
Write-Host ""
Write-Host ("  {0,-34} {1,12} {2,12}" -f "image", "flash (B)", "RAM (B)")
Write-Host ("  {0,-34} {1,12} {2,12}" -f ("-" * 34), ("-" * 12), ("-" * 12))
foreach ($t in $targets) {
    if (-not (Test-Path $t.elf)) {
        Write-Host ("  {0,-34} {1,12} {2,12}" -f $t.label, "--", "--"); continue
    }
    $n = (& arm-zephyr-eabi-size $t.elf | Select-Object -Last 1) -split '\s+' |
         Where-Object { $_ -ne "" }
    $text = [int]$n[0]; $data = [int]$n[1]; $bss = [int]$n[2]
    Write-Host ("  {0,-34} {1,12} {2,12}" -f $t.label, ($text + $data), ($data + $bss))
}

Write-Host ""
Write-Host "================ BUILD SUMMARY ================"
foreach ($t in $targets) {
    Write-Host ("  {0,-36} {1}" -f ($t.label -replace '\s+', ' '), $results[$t.label])
}
Write-Host "=============================================="
