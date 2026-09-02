<#
    flash_bootloader.ps1 — one-time SWD programming of the Nordic Open Bootloader
    onto a Cookie nRF V2.00 board.

    Run from Windows with an nRF52-DK (or any J-Link) wired to the Cookie's SWD
    header. After this runs once, every later application update goes over the
    board's own USB-C with nrfutil, no debugger needed.

    Usage:  powershell -ExecutionPolicy Bypass -File .\flash_bootloader.ps1
#>

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$Hex       = Join-Path $ScriptDir 'out\cookie_open_bootloader_mbr.hex'

# ---------------------------------------------------------------- locate tool --
$nrfjprog = $null
$onPath = Get-Command nrfjprog -ErrorAction SilentlyContinue
if ($onPath) {
    $nrfjprog = $onPath.Source
} else {
    $candidates = @(
        'C:\Program Files\Nordic Semiconductor\nrf-command-line-tools\bin\nrfjprog.exe',
        'C:\Program Files (x86)\Nordic Semiconductor\nrf-command-line-tools\bin\nrfjprog.exe'
    )
    foreach ($c in $candidates) { if (Test-Path $c) { $nrfjprog = $c; break } }
}

# Fallback: SEGGER J-Link Commander (installed with the J-Link software pack).
# Does the same job as nrfjprog here: ERASEALL through NVMC (flash + UICR),
# program the hex, write the UICR words, reset.
$jlink = $null
if (-not $nrfjprog) {
    $jlinkCandidates = @(Get-ChildItem 'E:\cookie-flash-kit\jlink\JLink.exe' -ErrorAction SilentlyContinue) +
                       @(Get-ChildItem 'C:\Program Files\SEGGER\JLink*\JLink.exe' -ErrorAction SilentlyContinue) +
                       @(Get-ChildItem 'C:\Program Files (x86)\SEGGER\JLink*\JLink.exe' -ErrorAction SilentlyContinue)
    if ($jlinkCandidates.Count -gt 0) { $jlink = $jlinkCandidates[0].FullName }
}

if (-not $nrfjprog -and -not $jlink) {
    Write-Host ''
    Write-Host 'ERROR: neither nrfjprog nor JLink.exe was found.' -ForegroundColor Red
    Write-Host '  Install "nRF Command Line Tools" from Nordic Semiconductor (installer'
    Write-Host '  sits in E:\thesis), or the SEGGER J-Link software pack. Expected:'
    Write-Host '    C:\Program Files\Nordic Semiconductor\nrf-command-line-tools\bin\nrfjprog.exe'
    Write-Host '    C:\Program Files\SEGGER\JLink*\JLink.exe'
    exit 1
}

if (-not (Test-Path $Hex)) {
    Write-Host ''
    Write-Host "ERROR: bootloader hex not found at $Hex" -ForegroundColor Red
    Write-Host '  Build it first (see README.md):'
    Write-Host '    docker run --rm -v "E:\projects\cookie-thread-mesh:/work" \'
    Write-Host '      -v "E:\projects\nrf5_sdk\nRF5_SDK_17.1.0_ddde560:/sdk" \'
    Write-Host '      cookie-ports bash /work/ports/bootloader/build.sh'
    exit 1
}

if ($nrfjprog) { Write-Host "nrfjprog : $nrfjprog" } else { Write-Host "JLink    : $jlink  (nrfjprog not installed, using J-Link Commander)" }
Write-Host "hex      : $Hex"
Write-Host ''

function Show-Done {
    Write-Host ''
    Write-Host '=== DONE ===' -ForegroundColor Green
    Write-Host ''
    Write-Host 'What you should see now:'
    Write-Host '  * With no application programmed, the board stays in DFU mode.'
    Write-Host '  * Windows enumerates a new USB CDC ACM device named'
    Write-Host '    "Open DFU Bootloader" and assigns it a COM port'
    Write-Host '    (check Device Manager -> Ports, or: [System.IO.Ports.SerialPort]::GetPortNames()).'
    Write-Host '  * The red LED (D2, P1.10) shows the slow breathing-style DFU indication.'
    Write-Host ''
    Write-Host 'Flash an application over USB (no debugger):'
    Write-Host '  nrfutil pkg generate --hw-version 52 --sd-req 0x00 --application app.hex app.zip'
    Write-Host '  nrfutil dfu usb-serial -pkg app.zip -p COMx'
    Write-Host ''
    Write-Host 'Caveat: the full erase wiped everything. Any application previously on the'
    Write-Host 'board is gone and must be re-flashed (over USB DFU or SWD).'
}

# --------------------------------------------------- J-Link Commander branch --
# Same sequence as the nrfjprog path below: NVMC ERASEALL (flash + UICR, the
# recover step), program the hex, write the three UICR words through NVMC WEN,
# reset. Values documented at the nrfjprog steps below.
if (-not $nrfjprog) {
    $cmdFile = Join-Path $env:TEMP 'cookie_bl_flash.jlink'
    @"
si SWD
speed 4000
device NRF52840_XXAA
connect
h
w4 0x4001E504 0x00000002
w4 0x4001E50C 0x00000001
sleep 1000
w4 0x4001E504 0x00000000
loadfile "$Hex"
w4 0x4001E504 0x00000001
w4 0x10001200 0x00000012
sleep 20
w4 0x10001204 0x00000012
sleep 20
w4 0x10001304 0xFFFFFFF5
sleep 20
w4 0x4001E504 0x00000000
r
g
qc
"@ | Set-Content -LiteralPath $cmdFile -Encoding ascii
    & $jlink -CommandFile $cmdFile
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: JLink Commander (exit $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
    Show-Done
    exit 0
}

function Invoke-Step {
    param([string]$Label, [string[]]$ToolArgs)
    Write-Host ">>> $Label" -ForegroundColor Cyan
    & $nrfjprog @ToolArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: $Label (exit $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# ------------------------------------------------------------------- program --
# --recover clears any APPROTECT / readback protection and mass-erases, which is
# also the only way to rewrite UICR (UICR is flash: bits only go 1 -> 0).
Invoke-Step 'Recovering device (mass erase, clears UICR and APPROTECT)' `
    @('--recover')

Invoke-Step 'Programming MBR + Open Bootloader' `
    @('--program', $Hex, '--verify')

# ----------------------------------------------------------------- UICR bits --
# PSELRESET[0] / PSELRESET[1] = 0x12 -> P0.18 is the hardware reset pin.
# Both words must hold the same value or the pin mapping is ignored. This is what
# makes SW2 a real reset button, and pin-reset is one of the three DFU entry
# methods the bootloader watches for.
Invoke-Step 'UICR PSELRESET[0] = P0.18' `
    @('--memwr', '0x10001200', '--val', '0x00000012')
Invoke-Step 'UICR PSELRESET[1] = P0.18' `
    @('--memwr', '0x10001204', '--val', '0x00000012')

# REGOUT0 = 0b101 -> 3.0 V. The Cookie is always fed from USB-C through VDDH, so
# the internal regulator starts at its 1.8 V default and the GPIOs cannot drive
# the LEDs or meet the sensor rails until this is set.
Invoke-Step 'UICR REGOUT0 = 3.0 V (high-voltage mode)' `
    @('--memwr', '0x10001304', '--val', '0xFFFFFFF5')

Invoke-Step 'Resetting' @('--reset')

# ---------------------------------------------------------------------- done --
Show-Done
