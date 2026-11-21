#!/usr/bin/env python3
"""Back up and restore the ESP32-C3 board's entire 4 MB flash."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid
import zipfile


PROJECT = Path(__file__).resolve().parent.parent
FLASH_SIZE = 4 * 1024 * 1024
FORMAT = "ingress-glyph-pcb-full-flash"


def esptool_path():
    from platformio.platform.factory import PlatformFactory

    package = PlatformFactory.new("espressif32").get_package_dir("tool-esptoolpy")
    if package and (Path(package) / "esptool.py").is_file():
        return Path(package) / "esptool.py"
    raise RuntimeError("ESP32 tools are missing. Run python -m platformio run -e esp32-c3-devkitm-1-en first.")


def run_tool(port, *arguments):
    subprocess.run([
        sys.executable, str(esptool_path()), "--chip", "esp32c3",
        "--port", port, "--baud", "460800", *map(str, arguments),
    ], check=True)


def backup_board(port, output):
    output = Path(output).resolve()
    if output.exists():
        raise FileExistsError(f"Backup already exists: {output}; choose a new filename.")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="glyph-backup-") as temporary:
        image = Path(temporary) / "flash.bin"
        run_tool(port, "read_flash", "0", hex(FLASH_SIZE), image)
        if image.stat().st_size != FLASH_SIZE:
            raise ValueError("Incomplete flash read; no backup was saved.")
        data = image.read_bytes()
        manifest = {
            "format": FORMAT, "version": 1, "chip": "esp32c3",
            "flash_size": FLASH_SIZE, "sha256": hashlib.sha256(data).hexdigest(),
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "source_port": port,
        }
        archive = Path(temporary) / "backup.zip"
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
            bundle.writestr("manifest.json", json.dumps(manifest, indent=2))
            bundle.writestr("flash.bin", data)
        # Exclusive creation never overwrites an existing backup.
        destination = output.open("xb")
        try:
            with destination, archive.open("rb") as source:
                shutil.copyfileobj(source, destination)
        except BaseException:
            output.unlink(missing_ok=True)
            raise
    print(f"Backup saved: {output}")


def read_backup(backup):
    with zipfile.ZipFile(backup) as bundle:
        if sorted(bundle.namelist()) != ["flash.bin", "manifest.json"]:
            raise ValueError("Not a supported board backup: expected manifest.json and flash.bin.")
        if bundle.getinfo("manifest.json").file_size > 65536:
            raise ValueError("Invalid backup manifest size.")
        manifest = json.loads(bundle.read("manifest.json"))
        if not isinstance(manifest, dict) or any((
            manifest.get("format") != FORMAT,
            manifest.get("version") != 1,
            manifest.get("chip") != "esp32c3",
            manifest.get("flash_size") != FLASH_SIZE,
        )):
            raise ValueError("Backup is not a supported ESP32-C3 4 MB flash image.")
        if bundle.getinfo("flash.bin").file_size != FLASH_SIZE:
            raise ValueError("Backup flash image must be exactly 4 MB.")
        data = bundle.read("flash.bin")
        if hashlib.sha256(data).hexdigest() != manifest.get("sha256"):
            raise ValueError("Backup checksum mismatch; restore cancelled.")
        return data


def restore_board(port, backup):
    # Validate the whole archive before opening the board or erasing flash.
    data = read_backup(backup)
    with tempfile.TemporaryDirectory(prefix="glyph-restore-") as temporary:
        image = Path(temporary) / "flash.bin"
        image.write_bytes(data)
        run_tool(port, "write_flash", "--erase-all", "--flash_size", "keep",
                 "--verify", "0", image)
    print("Restore completed and verified. Firmware, configuration and history restored.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    backup = actions.add_parser("backup", help="save a full-flash ZIP backup")
    backup.add_argument("--port", required=True)
    backup.add_argument("--output", type=Path, help="new ZIP filename; defaults to backups/")
    restore = actions.add_parser("restore", help="replace all flash from a backup")
    restore.add_argument("--port", required=True)
    restore.add_argument("--backup", type=Path, required=True)
    args = parser.parse_args()
    port = args.port.strip()
    if os.name == "nt" and port.isdigit():
        port = f"COM{port}"
    if args.action == "backup":
        output = args.output
        if output is None:
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
            output = PROJECT / "backups" / f"board-{stamp}-{uuid.uuid4().hex[:8]}.zip"
        backup_board(port, output)
    else:
        restore_board(port, args.backup)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError,
            zipfile.BadZipFile) as error:
        print(f"Board backup/restore failed: {error}", file=sys.stderr)
        raise SystemExit(1)
