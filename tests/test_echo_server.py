#!/usr/bin/env python3
"""Linux integration tests for the async TCP echo server."""

import argparse
import concurrent.futures
import contextlib
import os
import resource
from pathlib import Path
import signal
import socket
import subprocess
import sys
import threading
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


class ServerProcessTestCase(unittest.TestCase):
    server_path = DEFAULT_SERVER
    open_file_limit = None
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

        preexec_fn = None
        if cls.open_file_limit is not None:
            limit = cls.open_file_limit

            def preexec_fn():
                resource.setrlimit(resource.RLIMIT_NOFILE, (limit, limit))

        cls.port = find_available_port()
        cls.process = subprocess.Popen(
            [str(cls.server_path), HOST, str(cls.port)],
            cwd=PROJECT_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            preexec_fn=preexec_fn,
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


class EchoServerTestCase(ServerProcessTestCase):

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

    def test_peer_shutdown_while_echo_is_blocked_still_receives_echo(self):
        # Large enough that the server must wait for the socket to become
        # writable while the client's FIN is already queued.
        payload = (b"blocked-half-close-" * (8 * 1024 * 1024 // 19)) + b"end"
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client:
            client.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
            client.settimeout(SOCKET_TIMEOUT_SECONDS)
            client.connect((HOST, self.port))
            send_errors = []

            def send_then_shutdown():
                try:
                    client.sendall(payload)
                    client.shutdown(socket.SHUT_WR)
                except OSError as error:
                    send_errors.append(error)

            sender = threading.Thread(target=send_then_shutdown)
            sender.start()
            response = receive_exactly(client, len(payload))
            sender.join(timeout=SOCKET_TIMEOUT_SECONDS)

            self.assertEqual(send_errors, [])
            self.assertEqual(response, payload)
            self.assertEqual(client.recv(1), b"")


class FileDescriptorExhaustionTestCase(ServerProcessTestCase):
    open_file_limit = 16

    def test_survives_running_out_of_file_descriptors(self):
        with contextlib.ExitStack() as stack:
            for _ in range(self.open_file_limit * 2):
                stack.enter_context(
                    socket.create_connection((HOST, self.port), timeout=SOCKET_TIMEOUT_SECONDS)
                )
            time.sleep(0.2)
            self.assertIsNone(self.process.poll(), "server exited after running out of descriptors")

        time.sleep(0.2)
        payload = b"after-descriptor-exhaustion"
        self.assertEqual(echo_once(self.port, payload), payload)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--server",
        type=Path,
        default=DEFAULT_SERVER,
        help="path to the built server executable (default: %(default)s)",
    )
    arguments, unittest_arguments = parser.parse_known_args()
    ServerProcessTestCase.server_path = arguments.server.resolve()
    unittest.main(argv=[sys.argv[0], *unittest_arguments], verbosity=2)
