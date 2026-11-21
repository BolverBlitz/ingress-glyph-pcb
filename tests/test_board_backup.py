"""Tests for full board flash backup and restore validation."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile


sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
import board_backup


class BoardBackupTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.image = bytes((index * 17) % 256 for index in range(board_backup.FLASH_SIZE))

    def tearDown(self):
        self.directory.cleanup()

    def make_archive(self, *, data=None, manifest_overrides=None, members=None):
        data = self.image if data is None else data
        manifest = {
            "format": board_backup.FORMAT,
            "version": 1,
            "chip": "esp32c3",
            "flash_size": board_backup.FLASH_SIZE,
            "sha256": hashlib.sha256(data).hexdigest(),
        }
        manifest.update(manifest_overrides or {})
        archive = self.root / "backup.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            if members is None:
                bundle.writestr("manifest.json", json.dumps(manifest))
                bundle.writestr("flash.bin", data)
            else:
                for name, value in members.items():
                    bundle.writestr(name, value)
        return archive

    def test_backup_and_read_backup_roundtrip(self):
        output = self.root / "nested" / "board.zip"

        def read_flash(port, *args):
            Path(args[-1]).write_bytes(self.image)

        with mock.patch.object(board_backup, "run_tool", side_effect=read_flash) as tool:
            board_backup.backup_board("COM11", output)
            restored = board_backup.read_backup(output)

        self.assertEqual(restored, self.image)
        tool.assert_called_once()
        self.assertEqual(tool.call_args.args[1:4], ("read_flash", "0", hex(board_backup.FLASH_SIZE)))

    def test_restore_uses_erase_verify_and_zero_offset(self):
        archive = self.make_archive()
        restored = []

        def capture_image(port, *args):
            restored.append(Path(args[-1]).read_bytes())

        with mock.patch.object(board_backup, "run_tool", side_effect=capture_image) as tool:
            board_backup.restore_board("COM12", archive)
        args = tool.call_args.args
        self.assertEqual(args[:6], (
            "COM12", "write_flash", "--erase-all", "--flash_size", "keep", "--verify"
        ))
        self.assertEqual(args[6], "0")
        self.assertEqual(restored, [self.image])

    def test_invalid_manifest_values_reject_before_tool(self):
        cases = {
            "checksum": {"sha256": "0" * 64},
            "chip": {"chip": "esp32"},
            "size": {"flash_size": board_backup.FLASH_SIZE - 1},
        }
        for name, overrides in cases.items():
            with self.subTest(name=name):
                archive = self.make_archive(manifest_overrides=overrides)
                with mock.patch.object(board_backup, "run_tool") as tool:
                    with self.assertRaises(ValueError):
                        board_backup.restore_board("COM12", archive)
                tool.assert_not_called()

    def test_bad_zip_rejects_before_tool(self):
        archive = self.root / "bad.zip"
        archive.write_bytes(b"not a zip")
        with mock.patch.object(board_backup, "run_tool") as tool:
            with self.assertRaises(zipfile.BadZipFile):
                board_backup.restore_board("COM12", archive)
        tool.assert_not_called()

    def test_incomplete_read_leaves_no_output(self):
        output = self.root / "board.zip"

        def incomplete_read(port, *args):
            Path(args[-1]).write_bytes(b"short")

        with mock.patch.object(board_backup, "run_tool", side_effect=incomplete_read):
            with self.assertRaises(ValueError):
                board_backup.backup_board("COM11", output)
        self.assertFalse(output.exists())

    def test_failed_read_leaves_no_output(self):
        output = self.root / "board.zip"
        with mock.patch.object(board_backup, "run_tool", side_effect=RuntimeError("read failed")):
            with self.assertRaises(RuntimeError):
                board_backup.backup_board("COM11", output)
        self.assertFalse(output.exists())

    def test_existing_output_is_not_overwritten_or_read(self):
        output = self.root / "board.zip"
        original = b"existing"
        output.write_bytes(original)
        with mock.patch.object(board_backup, "run_tool") as tool:
            with self.assertRaises(FileExistsError):
                board_backup.backup_board("COM11", output)
        tool.assert_not_called()
        self.assertEqual(output.read_bytes(), original)

    def test_failed_restore_propagates(self):
        archive = self.make_archive()
        with mock.patch.object(board_backup, "run_tool", side_effect=RuntimeError("write failed")):
            with self.assertRaises(RuntimeError):
                board_backup.restore_board("COM12", archive)


if __name__ == "__main__":
    unittest.main()
