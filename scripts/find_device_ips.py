#!/usr/bin/env python3
"""Find USB-connected Ingress Glyph controllers and print their IP addresses."""

import argparse
import concurrent.futures
import re
import sys
import time

import serial
from serial.tools import list_ports


IP_PATTERN = re.compile(
    r"^Device IP: (?P<address>[0-9.]+)(?: \((?P<connection>[^)]+)\))?$"
)
BOOT_WIFI_PATTERN = re.compile(r"Wi-Fi: connected, address (?P<address>[0-9.]+)")
BOOT_HOTSPOT_PATTERN = re.compile(
    r"Wi-Fi: hotspot fallback at http://(?P<address>[0-9.]+)"
)


def normalize_port(value: str) -> str:
    if sys.platform == "win32" and value.isdigit():
        return f"COM{value}"
    return value


def query_port(port_name: str, baud: int, timeout: float):
    port = serial.Serial()
    port.port = port_name
    port.baudrate = baud
    port.timeout = 0.2
    port.write_timeout = 2
    port.dtr = False
    port.rts = False
    try:
        port.open()
        # A USB UART may reset the ESP32 when opened. Repeating the harmless
        # command also covers a controller that is still completing startup.
        deadline = time.monotonic() + timeout
        next_request = time.monotonic() + 1.5
        controller_seen = False
        reset_sent = False
        fallback_result = None
        fallback_seen_at = None
        while time.monotonic() < deadline:
            if fallback_seen_at and time.monotonic() - fallback_seen_at >= 25:
                return fallback_result
            if time.monotonic() >= next_request:
                # `show` identifies firmware versions from before the `ip`
                # command was added. It never changes stored configuration.
                port.write(b"ip\nshow\n")
                port.flush()
                next_request = time.monotonic() + 1.5
            raw = port.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            match = IP_PATTERN.match(line)
            if match:
                result = port_name, match.group("address"), match.group("connection")
                if match.group("connection") == "fallback hotspot":
                    fallback_result = result
                    fallback_seen_at = fallback_seen_at or time.monotonic()
                else:
                    return result
            match = BOOT_WIFI_PATTERN.search(line)
            if match:
                return port_name, match.group("address"), "Wi-Fi"
            match = BOOT_HOTSPOT_PATTERN.search(line)
            if match:
                fallback_result = (
                    port_name,
                    match.group("address"),
                    "fallback hotspot",
                )
                fallback_seen_at = fallback_seen_at or time.monotonic()
            if line == "Wi-Fi: hotspot fallback stopped":
                fallback_result = None
                fallback_seen_at = None
            if line == "Device IP: unavailable":
                controller_seen = True
            elif "Serial configuration ready" in line or line.startswith(
                "Config: firmware "
            ):
                controller_seen = True
            if controller_seen and not reset_sent:
                # Old firmware can only report its address in the startup log.
                # Pulse EN through the CH340/USB-UART RTS line and boot normally.
                port.setRTS(True)
                time.sleep(0.1)
                port.setRTS(False)
                reset_sent = True
                next_request = deadline + 1
        if fallback_result:
            return fallback_result
        if controller_seen:
            return port_name, None, None
    except (OSError, serial.SerialException):
        return None
    finally:
        if port.is_open:
            port.close()
    return None


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Display IP addresses of USB-connected Ingress Glyph controllers."
    )
    parser.add_argument(
        "ports", nargs="*", help="optional serial ports; otherwise scan all ports"
    )
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=50.0)
    args = parser.parse_args()

    if args.timeout <= 0:
        parser.error("--timeout must be greater than zero")

    if args.ports:
        ports = [normalize_port(value) for value in args.ports]
    else:
        ports = [item.device for item in list_ports.comports()]
    ports = list(dict.fromkeys(ports))
    if not ports:
        print("No serial devices found.")
        return 1

    print(f"Checking {len(ports)} serial port(s)...", flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(ports)) as executor:
        results = list(
            executor.map(lambda name: query_port(name, args.baud, args.timeout), ports)
        )

    devices = sorted((result for result in results if result), key=lambda row: row[0])
    if not devices:
        print("No compatible Ingress Glyph controllers responded.")
        return 1
    for port_name, address, connection in devices:
        if address:
            suffix = f" [{connection}]" if connection else ""
            print(f"{port_name}: http://{address}/{suffix}")
        else:
            print(f"{port_name}: IP unavailable (still connecting)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
