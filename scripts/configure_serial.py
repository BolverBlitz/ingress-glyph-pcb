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
IP_REPLY_PATTERN = re.compile(r"^Device IP: ([0-9.]+) \(Wi-Fi\)$")
STARTUP_TIMEOUT = 60.0


def normalize_faction(value: str) -> str:
    normalized = value.lower()
    aliases = {"res": "resistance", "enl": "enlightened"}
    return aliases.get(normalized, normalized)


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


def request_lines(port: serial.Serial, request: bytes, timeout: float):
    """Repeat only read-only queries while the controller completes startup."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        port.write(request)
        port.flush()
        remaining = deadline - time.monotonic()
        if remaining > 0:
            yield from read_lines(port, min(2.0, remaining))


def wait_until_ready(port: serial.Serial, timeout: float = STARTUP_TIMEOUT):
    # The boot banner precedes blocking Wi-Fi/BLE initialization. Only a show
    # response proves setup() has completed and loopSerialConfig() is running.
    status = []
    for line in request_lines(port, b"show\n", timeout):
        if line.startswith("Error:"):
            raise RuntimeError(line)
        if line.startswith("Config: firmware "):
            status = [line]
        elif status and line.startswith("Config:"):
            status.append(line)
        if len(status) == 3:
            return status
    raise TimeoutError(
        f"controller did not respond to show within {timeout:g} seconds; "
        "check the selected port and the controller's startup log"
    )


def send_saved_command(port: serial.Serial, command: str) -> None:
    port.write((command + "\n").encode("utf-8"))
    port.flush()
    try:
        wait_for(port, SAVE_TEXT, 15.0)
    except TimeoutError as error:
        # Name the command type without exposing Wi-Fi passwords.
        raise TimeoutError(f"{command.split()[0]} command was not acknowledged: {error}") from error
    wait_for(port, READY_TEXT, STARTUP_TIMEOUT)
    wait_until_ready(port)


def wait_for_ip(port: serial.Serial) -> str:
    # Startup handshakes may already have consumed the boot-time IP message.
    for line in request_lines(port, b"ip\n", 35.0):
        if line.startswith("Error:"):
            raise RuntimeError(line)
        match = CONNECTED_PATTERN.search(line) or IP_REPLY_PATTERN.match(line)
        if match:
            return match.group(1)
    raise TimeoutError("Wi-Fi did not connect within 35 seconds")


def show_status(port: serial.Serial) -> None:
    for line in wait_until_ready(port, timeout=10.0):
        print(line)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Set hardware revision, optional faction and Wi-Fi after flashing."
    )
    parser.add_argument("--port", required=True, help="serial port, e.g. COM11")
    parser.add_argument("--revision", required=True, choices=("1", "2"))
    parser.add_argument("--faction", type=normalize_faction,
                        choices=("resistance", "enlightened"),
                        help="optional faction (res/enl aliases; omitted leaves current unchanged)")
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
        port.reset_input_buffer()
        print("Waiting for controller startup...", flush=True)
        wait_until_ready(port)
        send_saved_command(port, f"rev {args.revision}")
        print(f"Hardware revision {args.revision} saved.")

        if args.faction is not None:
            send_saved_command(port, f"faction {args.faction}")
            print(f"Faction {args.faction} saved.")

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
