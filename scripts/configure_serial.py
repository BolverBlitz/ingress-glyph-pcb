#!/usr/bin/env python3
"""Configure a flashed controller through its USB serial console."""

import argparse
import os
import re
import sys
import time

import serial


READY_TEXT = "Serial configuration ready"
SAVE_TEXT = "Config saved. Restarting controller"
CONNECTED_PATTERN = re.compile(r"Wi-Fi: connected, address ([0-9.]+)")


def quote_argument(value: str) -> str:
    if "\n" in value or "\r" in value or "\0" in value:
        raise ValueError("serial configuration values cannot contain newlines or NUL")
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def read_lines(port: serial.Serial, duration: float):
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if line:
            yield line


def wait_for(port: serial.Serial, expected: str, timeout: float) -> None:
    for line in read_lines(port, timeout):
        if line.startswith("Error:"):
            raise RuntimeError(line)
        if expected in line:
            return
    raise TimeoutError(f"controller did not report {expected!r}")


def send_saved_command(port: serial.Serial, command: str) -> None:
    port.write((command + "\n").encode("utf-8"))
    port.flush()
    wait_for(port, SAVE_TEXT, 8.0)
    wait_for(port, READY_TEXT, 10.0)


def wait_for_ip(port: serial.Serial) -> str:
    for line in read_lines(port, 35.0):
        if line.startswith("Error:"):
            raise RuntimeError(line)
        match = CONNECTED_PATTERN.search(line)
        if match:
            return match.group(1)
    raise TimeoutError("Wi-Fi did not connect within 35 seconds")


def show_status(port: serial.Serial) -> None:
    # The ready banner is printed near the start of setup; BLE, the web server,
    # and storage initialization may still be running before loop() reads show.
    time.sleep(1.0)
    port.write(b"show\n")
    port.flush()
    found = 0
    for line in read_lines(port, 10.0):
        if line.startswith("Error:"):
            raise RuntimeError(line)
        if line.startswith("Config:"):
            print(line)
            found += 1
            if found == 3:
                return
    raise TimeoutError("controller did not return serial configuration status")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Set hardware revision and optional Wi-Fi after flashing."
    )
    parser.add_argument("--port", required=True, help="serial port, e.g. COM11")
    parser.add_argument("--revision", required=True, choices=("1", "2"))
    parser.add_argument("--ssid", help="optional Wi-Fi SSID for profile 1")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    if os.name == "nt" and args.port.isdigit():
        args.port = f"COM{args.port}"

    password = os.environ.pop("INGRESS_WIFI_PASSWORD", "")
    if args.ssid is not None:
        if not 1 <= len(args.ssid.encode("utf-8")) <= 32:
            parser.error("SSID must contain 1 to 32 UTF-8 bytes")
        password_bytes = len(password.encode("utf-8"))
        if password_bytes and not 8 <= password_bytes <= 64:
            parser.error("password must be empty or contain 8 to 64 UTF-8 bytes")

    port = serial.Serial()
    port.port = args.port
    port.baudrate = args.baud
    port.timeout = 0.2
    port.write_timeout = 3
    port.dtr = False
    port.rts = False

    try:
        port.open()
        # Opening a USB UART can reset the ESP32. Let startup complete whether
        # or not this process attached early enough to see the ready message.
        time.sleep(2.0)
        port.reset_input_buffer()
        send_saved_command(port, f"rev {args.revision}")
        print(f"Hardware revision {args.revision} saved.")

        ip_address = None
        if args.ssid is not None:
            command = (
                f"wifi 1 {quote_argument(args.ssid)} "
                f"{quote_argument(password)}"
            )
            send_saved_command(port, command)
            print("Wi-Fi profile 1 saved; waiting for connection...")
            ip_address = wait_for_ip(port)

        show_status(port)
        if ip_address:
            print(f"Controller ready at http://{ip_address}/")
    finally:
        if port.is_open:
            port.close()
        password = ""
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, serial.SerialException, RuntimeError,
            TimeoutError) as error:
        print(f"Serial configuration failed: {error}", file=sys.stderr)
        raise SystemExit(1)
