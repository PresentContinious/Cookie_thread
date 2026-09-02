<#
Package every DFU-linked image in the workspace into a Nordic DFU zip that
`nrfutil dfu usb-serial` can send to the Cookie board over its own USB-C.

    powershell -ExecutionPolicy Bypass -File scripts\make_dfu_zips.ps1

Inputs are whatever has already been built: the Zephyr *_dfu build directories
under the west workspace build root, and the <app>_dfu.hex files the Docker
port build (ports/docker/build-ports.sh) leaves in the RIOT and Contiki-NG
trees. Nothing is compiled here.

Outputs land in <workspace>\build\dfu as <os>_<app>.zip, for example
zephyr_cookie_node_sed.zip, riot_cookie_sensor_node.zip,
contiki_cookie-gateway.zip.

Two packaging front-ends exist and take different command lines: the unified
nrfutil hides the SDK tools behind an `nrf5sdk-tools` subcommand, the classic
6.1.7 executable does not. The unified one is preferred and the copy bundled in
tools\nrfutil is the fallback.
#>
param(
    [string]$BuildRoot,
    [string]$OutDir
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$workspace = Split-Path -Parent $repo
if (-not $BuildRoot) { $BuildRoot = Join-Path $workspace "build" }
if (-not $OutDir)    { $OutDir    = Join-Path $BuildRoot "dfu" }

# ------------------------------------------------------------- packager ----
# Candidates in the same order build_all.py resolves nrfutil, plus the classic
# executable kept in the repo so a machine without the unified CLI still works.
$candidates = @()
$onPath = Get-Command nrfutil -ErrorAction SilentlyContinue
if ($onPath) { $candidates += $onPath.Source }
$candidates += @(
    "C:\Program Files\nrfutil\bin\nrfutil.exe",
    "C:\Program Files\nordicsemi\nrfutil\nrfutil.exe",
    "$env:LOCALAPPDATA\nrfutil\bin\nrfutil.exe",
    "$env:LOCALAPPDATA\Programs\nrfutil\bin\nrfutil.exe"
)
$ncsRoot = if ($env:NRF_CONNECT_SDK_ROOT) { $env:NRF_CONNECT_SDK_ROOT } else { "C:\ncs" }
Get-ChildItem (Join-Path $ncsRoot "toolchains") -Directory -ErrorAction SilentlyContinue |
    ForEach-Object {
        $candidates += @("$($_.FullName)\opt\bin\nrfutil.exe",
                         "$($_.FullName)\bin\nrfutil.exe",
                         "$($_.FullName)\nrfutil.exe")
    }

$exe = $null
$prefix = @()
foreach ($c in $candidates) {
    if (-not (Test-Path $c)) { continue }
    & $c nrf5sdk-tools pkg generate --help *> $null
    if ($LASTEXITCODE -eq 0) { $exe = $c; $prefix = @("nrf5sdk-tools"); break }
}
if (-not $exe) {
    $bundled = Join-Path $repo "tools\nrfutil\nrfutil.exe"
    if (Test-Path $bundled) {
        & $bundled pkg generate --help *> $null
        if ($LASTEXITCODE -eq 0) { $exe = $bundled; $prefix = @() }
    }
}
if (-not $exe) { throw "No usable nrfutil found (unified CLI or tools\nrfutil\nrfutil.exe)" }
Write-Host "packager: $exe $($prefix -join ' ')"

# ---------------------------------------------------------------- inputs ----
$images = @()
Get-ChildItem $BuildRoot -Directory -Filter "*_dfu" -ErrorAction SilentlyContinue |
    ForEach-Object {
        $bd = $_.Name
        Get-ChildItem $_.FullName -Recurse -Filter "zephyr.hex" -ErrorAction SilentlyContinue |
            ForEach-Object { $images += @{ os = "zephyr"; base = $bd; hex = $_.FullName } }
    }
foreach ($os in @("riot", "contiki-ng")) {
    $root = Join-Path $repo "ports\$os"
    Get-ChildItem $root -Recurse -Filter "*_dfu.hex" -ErrorAction SilentlyContinue |
        ForEach-Object {
            $tag = if ($os -eq "riot") { "riot" } else { "contiki" }
            $images += @{ os = $tag
                          base = [IO.Path]::GetFileNameWithoutExtension($_.Name)
                          hex = $_.FullName }
        }
}
if (-not $images.Count) { throw "No DFU images found under $BuildRoot or $repo\ports" }

# --------------------------------------------------------------- package ----
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$made = @()
foreach ($img in $images) {
    # The OS already prefixes the zip name, so drop the tokens that would only
    # repeat it, and the _dfu tag that the output directory already carries.
    $base = $img.base -replace '_dfu$', '' -replace '_riot$', ''
    $zip  = Join-Path $OutDir "$($img.os)_$base.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    & $exe @prefix pkg generate --hw-version 52 --sd-req 0x00 `
        --application $img.hex --application-version 1 $zip | Out-Host
    if ($LASTEXITCODE -eq 0 -and (Test-Path $zip)) {
        $made += $zip
    } else {
        Write-Host "  FAIL  $zip"
    }
}

Write-Host ""
Write-Host "================ DFU PACKAGES ================"
foreach ($z in $made) { Write-Host "  $z" }
Write-Host "=============================================="
if ($made.Count -ne $images.Count) { exit 1 }
