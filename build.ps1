# Kompilasi ulang firmware (dan custom chip bila diperlukan) untuk Wokwi for VS Code.
# Jalankan dari folder 04_Wokwi:  powershell -ExecutionPolicy Bypass -File build.ps1
# Tambahkan -Chips untuk ikut mengompilasi custom chip (perlu wokwi-cli di PATH).
param([switch]$Chips)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
  Write-Host 'arduino-cli belum ada. Install dulu: winget install ArduinoSA.CLI' -ForegroundColor Yellow
  exit 1
}
arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir PLTS/build PLTS
Write-Host 'Firmware selesai: PLTS/build/PLTS.ino.merged.bin' -ForegroundColor Green

if ($Chips) {
  foreach ($c in 'lm35', 'bh1750', 'sct013', 'reservoir', 'pir') {
    wokwi-cli chip compile "$c.chip.c" -o "$c.chip.wasm"
  }
}
