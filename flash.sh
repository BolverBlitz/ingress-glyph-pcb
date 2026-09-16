#!/usr/bin/env bash
set -euo pipefail

project_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$project_dir"

if command -v python3 >/dev/null 2>&1; then
  python_cmd=python3
elif command -v python >/dev/null 2>&1; then
  python_cmd=python
else
  echo "Python was not found. Install Python and PlatformIO first." >&2
  exit 1
fi

default_port=""
for candidate in /dev/ttyUSB* /dev/ttyACM* /dev/cu.usbserial* /dev/cu.SLAB_USBtoUART*; do
  if [[ -e "$candidate" ]]; then
    default_port="$candidate"
    break
  fi
done
default_port="${default_port:-/dev/ttyUSB0}"

port="${1:-}"
if [[ -z "$port" ]]; then
  read -r -p "Serial port [$default_port]: " port
  port="${port:-$default_port}"
fi

revision=""
while [[ "$revision" != "1" && "$revision" != "2" ]]; do
  read -r -p "Hardware revision (1 or 2): " revision
  if [[ "$revision" != "1" && "$revision" != "2" ]]; then
    echo "Please enter 1 or 2."
  fi
done

read -r -p "Wi-Fi SSID (leave blank to skip Wi-Fi setup): " wifi_ssid
wifi_password=""
if [[ -n "$wifi_ssid" ]]; then
  read -r -s -p "Wi-Fi password (leave blank for an open network): " wifi_password
  echo
fi
trap 'unset wifi_password INGRESS_WIFI_PASSWORD' EXIT

echo "Building firmware..."
"$python_cmd" -m platformio run

echo "Flashing $port..."
"$python_cmd" -m platformio run --target upload --upload-port "$port"

configure_args=(
  scripts/configure_serial.py
  --port "$port"
  --revision "$revision"
)
if [[ -n "$wifi_ssid" ]]; then
  configure_args+=(--ssid "$wifi_ssid")
fi

echo "Saving controller configuration..."
INGRESS_WIFI_PASSWORD="$wifi_password" "$python_cmd" "${configure_args[@]}"
echo "Build, flash, and configuration completed."
