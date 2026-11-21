"""Tests for the active serial startup handshake."""

from pathlib import Path
import sys
import unittest
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
import configure_serial


class FakePort:
    def __init__(self):
        self.requests = []
        self.flushed = 0

    def write(self, data):
        self.requests.append(data)

    def flush(self):
        self.flushed += 1

    def reset_input_buffer(self):
        pass


class BootingPort(FakePort):
    """Serial device that prints its banner long before it handles commands."""

    def __init__(self, clock, startup_delay=25.0):
        super().__init__()
        self.clock = clock
        self.startup_delay = startup_delay
        self.ready_at = startup_delay
        self.pending = [(0.2, b"Serial configuration ready\n")]
        self.mutations = []
        self.is_open = False

    def open(self):
        self.is_open = True

    def close(self):
        self.is_open = False

    def write(self, data):
        super().write(data)
        now = self.clock[0]
        if data == b"show\n" and now >= self.ready_at:
            for line in (b"Config: firmware 0.9.8\n",
                         b"Config: Wi-Fi 1: <empty>\n", b"Config: Wi-Fi 2: <empty>\n"):
                self.pending.append((now, line))
        elif data.startswith((b"rev ", b"faction ")):
            if now < self.ready_at:
                raise AssertionError("Configuration sent while setup is still blocking")
            self.mutations.append((now, data))
            self.pending.extend([
                (now + 0.2, b"Config saved. Restarting controller...\n"),
                (now + 0.6, b"Serial configuration ready\n"),
            ])
            self.ready_at = now + self.startup_delay

    def readline(self):
        self.clock[0] += 0.2
        if self.pending and self.pending[0][0] <= self.clock[0]:
            return self.pending.pop(0)[1]
        return b""


class SerialHandshakeTests(unittest.TestCase):
    def test_real_handshake_handles_slow_boot_and_every_restart(self):
        clock = [0.0]
        port = BootingPort(clock)
        with mock.patch.object(configure_serial.serial, "Serial", return_value=port), \
             mock.patch.object(configure_serial.time, "monotonic", side_effect=lambda: clock[0]), \
             mock.patch.dict(configure_serial.os.environ, {}, clear=True), \
             mock.patch.object(sys, "argv", ["configure_serial.py", "--port", "COM11",
                                            "--revision", "2", "--faction", "enl"]):
            self.assertEqual(configure_serial.main(), 0)
        self.assertEqual([data for _, data in port.mutations],
                         [b"rev 2\n", b"faction enlightened\n"])
        self.assertGreaterEqual(port.mutations[0][0], 25.0)
        self.assertGreaterEqual(port.mutations[1][0], port.mutations[0][0] + 25.0)
        self.assertGreaterEqual(clock[0], port.mutations[1][0] + 25.0)
        self.assertFalse(port.is_open)

    def test_real_handshake_timeout_never_sends_configuration(self):
        clock = [0.0]
        port = BootingPort(clock, startup_delay=120.0)
        with mock.patch.object(configure_serial.serial, "Serial", return_value=port), \
             mock.patch.object(configure_serial.time, "monotonic", side_effect=lambda: clock[0]), \
             mock.patch.dict(configure_serial.os.environ, {}, clear=True), \
             mock.patch.object(sys, "argv", ["configure_serial.py", "--port", "COM11",
                                            "--revision", "2"]):
            with self.assertRaises(TimeoutError):
                configure_serial.main()
        self.assertEqual(port.mutations, [])
        self.assertTrue(all(request == b"show\n" for request in port.requests))
        self.assertFalse(port.is_open)

    def test_main_waits_for_active_loop_before_mutating_command(self):
        port = FakePort()
        port.is_open = False
        events = []

        def open_port():
            port.is_open = True

        def close_port():
            port.is_open = False

        port.open = open_port
        port.close = close_port
        with mock.patch.object(configure_serial.serial, "Serial", return_value=port), \
             mock.patch.object(configure_serial, "wait_until_ready",
                                side_effect=lambda p: events.append("ready")), \
             mock.patch.object(configure_serial, "send_saved_command",
                                side_effect=lambda p, command: events.append(command)), \
             mock.patch.object(configure_serial, "show_status"), \
             mock.patch.object(configure_serial.time, "sleep"), \
             mock.patch.dict(configure_serial.os.environ, {}, clear=True), \
             mock.patch.object(sys, "argv", ["configure_serial.py", "--port", "COM11", "--revision", "2"]):
            self.assertEqual(configure_serial.main(), 0)
        self.assertEqual(events, ["ready", "rev 2"])
        self.assertFalse(port.is_open)

    def test_wait_until_ready_requires_three_config_lines_and_ignores_banner(self):
        port = FakePort()
        lines = iter([
            "Serial configuration ready",
            "Config: firmware v1",
            "Config: Wi-Fi 1",
            "Config: Wi-Fi 2",
        ])
        with mock.patch.object(configure_serial, "request_lines",
                               return_value=lines) as request:
            configure_serial.wait_until_ready(port, timeout=60)
        request.assert_called_once_with(port, b"show\n", mock.ANY)

    def test_wait_until_ready_error_and_silent_timeout_fail(self):
        port = FakePort()
        with mock.patch.object(configure_serial, "request_lines",
                               return_value=iter(["Error: unavailable"])):
            with self.assertRaises(RuntimeError):
                configure_serial.wait_until_ready(port)
        with mock.patch.object(configure_serial, "request_lines", return_value=iter(())):
            with self.assertRaises(TimeoutError):
                configure_serial.wait_until_ready(port)

    def test_send_saved_command_sends_once_then_waits_and_handshakes(self):
        port = FakePort()
        calls = []
        with mock.patch.object(configure_serial, "wait_for", side_effect=lambda p, expected, timeout: calls.append((expected, timeout))), \
             mock.patch.object(configure_serial, "wait_until_ready", side_effect=lambda p: calls.append(("ready",))):
            configure_serial.send_saved_command(port, "rev 2")
        self.assertEqual(port.requests, [b"rev 2\n"])
        self.assertEqual(calls, [(configure_serial.SAVE_TEXT, 15.0),
                                 (configure_serial.READY_TEXT, 60.0), ("ready",)])

    def test_wait_for_ip_accepts_late_station_ip_and_ignores_hotspot(self):
        port = FakePort()
        lines = iter([
            "Device IP: 192.0.2.1 (fallback hotspot)",
            "Device IP: 192.0.2.42 (Wi-Fi)",
        ])
        with mock.patch.object(configure_serial, "request_lines",
                               return_value=lines) as request:
            self.assertEqual(configure_serial.wait_for_ip(port), "192.0.2.42")
        request.assert_called_once_with(port, b"ip\n", mock.ANY)

    def test_wait_for_ip_error_and_timeout_fail(self):
        port = FakePort()
        with mock.patch.object(configure_serial, "request_lines",
                               return_value=iter(["Error: unavailable"])):
            with self.assertRaises(RuntimeError):
                configure_serial.wait_for_ip(port)
        with mock.patch.object(configure_serial, "request_lines", return_value=iter(())):
            with self.assertRaises(TimeoutError):
                configure_serial.wait_for_ip(port)


if __name__ == "__main__":
    unittest.main()
