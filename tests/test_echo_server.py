#!/usr/bin/env python3
"""Linux integration tests for the async TCP echo server."""

import argparse
import concurrent.futures
import contextlib
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time
import unittest


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SERVER = PROJECT_ROOT / "server"
HOST = "127.0.0.1"
STARTUP_TIMEOUT_SECONDS = 5.0
SOCKET_TIMEOUT_SECONDS = 5.0


def find_available_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind((HOST, 0))
        return probe.getsockname()[1]


def receive_exactly(client, expected_size):
    data = bytearray()
    while len(data) < expected_size:
        chunk = client.recv(expected_size - len(data))
        if not chunk:
            raise AssertionError(
                f"connection closed after {len(data)} of {expected_size} expected bytes"
            )
        data.extend(chunk)
    return bytes(data)


def echo_once(port, payload, send_chunk_size=None):
    with socket.create_connection((HOST, port), timeout=SOCKET_TIMEOUT_SECONDS) as client:
        client.settimeout(SOCKET_TIMEOUT_SECONDS)
        if send_chunk_size is None:
            client.sendall(payload)
        else:
            for offset in range(0, len(payload), send_chunk_size):
                client.sendall(payload[offset : offset + send_chunk_size])
        return receive_exactly(client, len(payload))


class EchoServerTestCase(unittest.TestCase):
    server_path = DEFAULT_SERVER
    process = None
    port = None

    @classmethod
    def setUpClass(cls):
        if not sys.platform.startswith("linux"):
            raise unittest.SkipTest("the server uses Linux epoll and requires Linux to run")
        if not cls.server_path.is_file() or not os.access(cls.server_path, os.X_OK):
            raise RuntimeError(
                f"server executable not found or not executable: {cls.server_path}; run `make` first"
            )

        cls.port = find_available_port()
        cls.process = subprocess.Popen(
            [str(cls.server_path), HOST, str(cls.port)],
            cwd=PROJECT_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        cls.wait_until_ready()

    @classmethod
    def tearDownClass(cls):
        if cls.process is None:
            return

        if cls.process.poll() is None:
            cls.process.send_signal(signal.SIGTERM)
            try:
                cls.process.wait(timeout=STARTUP_TIMEOUT_SECONDS)
            except subprocess.TimeoutExpired:
                cls.process.kill()
                cls.process.wait(timeout=STARTUP_TIMEOUT_SECONDS)

        if cls.process.returncode not in (0, -signal.SIGTERM):
            stdout, stderr = cls.process.communicate()
            raise AssertionError(
                f"server exited unexpectedly with {cls.process.returncode}\n"
                f"stdout:\n{stdout}\nstderr:\n{stderr}"
            )

    @classmethod
    def wait_until_ready(cls):
        deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if cls.process.poll() is not None:
                stdout, stderr = cls.process.communicate()
                raise RuntimeError(
                    f"server exited during startup with {cls.process.returncode}\n"
                    f"stdout:\n{stdout}\nstderr:\n{stderr}"
                )
            try:
                with socket.create_connection((HOST, cls.port), timeout=0.1):
                    return
            except OSError:
                time.sleep(0.02)

        cls.process.kill()
        stdout, stderr = cls.process.communicate()
        raise RuntimeError(f"server did not become ready\nstdout:\n{stdout}\nstderr:\n{stderr}")

    def test_echoes_small_payload(self):
        payload = b"hello, async echo server\n"
        self.assertEqual(echo_once(self.port, payload), payload)

    def test_echoes_binary_payload(self):
        payload = bytes(range(256)) * 4
        self.assertEqual(echo_once(self.port, payload), payload)

    def test_echoes_payload_larger_than_connection_buffer(self):
        payload = (b"large-payload-" * 4096) + b"end"
        self.assertGreater(len(payload), 16 * 1024)
        self.assertEqual(echo_once(self.port, payload), payload)

    def test_echoes_fragmented_payload(self):
        payload = (b"fragmented-message-" * 2048) + b"end"
        self.assertEqual(echo_once(self.port, payload, send_chunk_size=37), payload)

    def test_serves_concurrent_clients(self):
        client_count = 64

        def run_client(index):
            payload = (f"client={index}:".encode("ascii") + bytes([index % 256])) * 256
            return echo_once(self.port, payload, send_chunk_size=113)

        with concurrent.futures.ThreadPoolExecutor(max_workers=client_count) as executor:
            responses = list(executor.map(run_client, range(client_count)))

        for index, response in enumerate(responses):
            expected = (f"client={index}:".encode("ascii") + bytes([index % 256])) * 256
            self.assertEqual(response, expected)

    def test_peer_shutdown_after_send_still_receives_echo(self):
        payload = b"request-before-half-close" * 256
        with socket.create_connection((HOST, self.port), timeout=SOCKET_TIMEOUT_SECONDS) as client:
            client.settimeout(SOCKET_TIMEOUT_SECONDS)
            client.sendall(payload)
            client.shutdown(socket.SHUT_WR)
            self.assertEqual(receive_exactly(client, len(payload)), payload)
            self.assertEqual(client.recv(1), b"")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--server",
        type=Path,
        default=DEFAULT_SERVER,
        help="path to the built server executable (default: %(default)s)",
    )
    arguments, unittest_arguments = parser.parse_known_args()
    EchoServerTestCase.server_path = arguments.server.resolve()
    unittest.main(argv=[sys.argv[0], *unittest_arguments], verbosity=2)
