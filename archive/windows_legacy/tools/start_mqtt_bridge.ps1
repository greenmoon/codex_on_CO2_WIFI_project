# Start the shared MQTT-to-HTTP bridge on Windows.
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Bridge = Join-Path $ProjectRoot "tools\common\mqtt_bridge.py"

if ($env:CO2_WIFI_PYTHON) {
    & $env:CO2_WIFI_PYTHON $Bridge
} elseif (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 $Bridge
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    & python $Bridge
} else {
    Write-Error "Python 3 was not found. Install Python 3, then install paho-mqtt."
    exit 1
}
