"""Host tests for the serial configuration command sequence."""

import sys
import unittest
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
import configure_serial


class FakeSerial:
    def __init__(self):
        self.is_open = False
        self.port = None

    def open(self):
        self.is_open = True

    def close(self):
        self.is_open = False

    def reset_input_buffer(self):
        pass


class SerialConfigTests(unittest.TestCase):
    def run_main(self, *arguments, command_side_effect=None, serial_holder=None,
                 wait_ip=None):
        serial_port = FakeSerial()
        if serial_holder is not None:
            serial_holder.append(serial_port)
        calls = []

        def send_command(port, command):
            calls.append(command)
            if command_side_effect:
                command_side_effect(port, command)

        with mock.patch.object(configure_serial.serial, "Serial", return_value=serial_port), \
             mock.patch.object(configure_serial, "send_saved_command", side_effect=send_command), \
             mock.patch.object(configure_serial, "wait_until_ready"), \
             mock.patch.object(configure_serial, "show_status"), \
             mock.patch.object(configure_serial, "wait_for_ip", return_value=wait_ip), \
             mock.patch.object(configure_serial.time, "sleep"), \
             mock.patch.dict(configure_serial.os.environ, {}, clear=True), \
             mock.patch.object(sys, "argv", ["configure_serial.py", *arguments]):
            result = configure_serial.main()
        return result, calls, serial_port

    def test_each_faction_and_alias_is_saved_canonically(self):
        for faction, canonical in (("enlightened", "enlightened"),
                                   ("resistance", "resistance"),
                                   ("ENL", "enlightened"),
                                   ("ReS", "resistance")):
            with self.subTest(faction=faction):
                result, calls, serial_port = self.run_main(
                    "--port", "COM11", "--revision", "2", "--faction", faction
                )
                self.assertEqual(result, 0)
                self.assertEqual(calls, ["rev 2", f"faction {canonical}"])
                self.assertFalse(serial_port.is_open)

    def test_omitted_faction_sends_no_faction_command(self):
        result, calls, serial_port = self.run_main(
            "--port", "COM11", "--revision", "1"
        )
        self.assertEqual(result, 0)
        self.assertEqual(calls, ["rev 1"])
        self.assertFalse(serial_port.is_open)

    def test_invalid_faction_is_rejected_before_port_opens(self):
        with mock.patch.object(configure_serial.serial, "Serial") as serial_factory, \
             mock.patch.object(sys, "argv", [
                 "configure_serial.py", "--port", "COM11", "--revision", "1",
                 "--faction", "neutral",
             ]), \
             self.assertRaises(SystemExit) as raised:
            configure_serial.main()
        self.assertEqual(raised.exception.code, 2)
        serial_factory.assert_not_called()

    def test_faction_precedes_optional_wifi_and_failure_closes_port(self):
        calls = []
        def fail_on_wifi(port, command):
            calls.append(command)
            if command.startswith("wifi "):
                raise RuntimeError("wifi failed")

        ports = []
        with self.assertRaises(RuntimeError):
            self.run_main(
                "--port", "COM11", "--revision", "2", "--faction", "resistance",
                "--ssid", "Network",
                command_side_effect=fail_on_wifi,
                serial_holder=ports,
            )
        self.assertEqual(calls, ["rev 2", "faction resistance", 'wifi 1 "Network" ""'])
        self.assertEqual(len(ports), 1)
        self.assertFalse(ports[0].is_open)

    def test_faction_failure_propagates_closes_port_and_skips_wifi(self):
        calls = []
        ports = []

        def fail_on_faction(port, command):
            calls.append(command)
            if command == "faction enlightened":
                raise RuntimeError("faction failed")

        with self.assertRaises(RuntimeError):
            self.run_main(
                "--port", "COM11", "--revision", "2", "--faction", "enlightened",
                "--ssid", "Network", command_side_effect=fail_on_faction,
                serial_holder=ports,
            )
        self.assertEqual(calls, ["rev 2", "faction enlightened"])
        self.assertEqual(len(ports), 1)
        self.assertFalse(ports[0].is_open)

    def test_faction_and_wifi_succeed_in_order(self):
        result, calls, serial_port = self.run_main(
            "--port", "COM11", "--revision", "2", "--faction", "resistance",
            "--ssid", "Network", wait_ip="192.0.2.10",
        )
        self.assertEqual(result, 0)
        self.assertEqual(
            calls,
            ["rev 2", "faction resistance", 'wifi 1 "Network" ""'],
        )
        self.assertFalse(serial_port.is_open)


if __name__ == "__main__":
    unittest.main()
