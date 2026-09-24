# Async TCP Echo Server

A single-threaded, non-blocking TCP echo server for **Linux**. It uses `epoll` with edge-triggered, one-shot connection events. Each connection reads data into a bounded buffer, writes all of that data back while correctly handling partial writes, then resumes reading.

## Requirements

- Linux with `epoll` support
- A C11 compiler and `make`

> The project does not build natively on macOS because macOS does not provide `epoll` or `<sys/epoll.h>`.

## Build

```sh
make
```

The default build enables C11 and useful compiler warnings, producing `./server`.

Other targets:

```sh
make debug      # unoptimized symbols for debugging
make sanitize   # AddressSanitizer + UndefinedBehaviorSanitizer
make test       # build and run automated Linux integration tests
make clean
```

## Docker (recommended on macOS and Windows)

The included multi-stage `Dockerfile` uses **Debian 12 (Bookworm) slim**. Debian 12 is a stable, supported Linux release with glibc and native `epoll` support. The build stage installs only the required build and test dependencies (`build-essential`, `make`, and `python3`) and runs the integration suite. The final runtime image contains only the server binary and runs it as an unprivileged user.

Build the image. This also compiles and runs the Linux integration tests during the build:

```sh
docker build --tag async-echo-server .
```

Run the server and publish its TCP port:

```sh
docker run --rm --init --publish 8080:8080 async-echo-server
```

Then, from another terminal:

```sh
nc 127.0.0.1 8080
```

To run only the build-and-test stage interactively, use the named `build` stage:

```sh
docker build --target build --tag async-echo-server-test .
docker run --rm async-echo-server-test make test
```

## Run

```sh
./server [bind-address] [port]
```

Defaults:

- bind address: `127.0.0.1`
- port: `8080`
- listen backlog: `256`
- per-connection echo buffer: `16 KiB`

For example, to listen on all IPv4 interfaces at port 9000:

```sh
./server 0.0.0.0 9000
```

Connect with netcat:

```sh
nc 127.0.0.1 8080
```

Anything sent by the client is echoed back. Stop the server cleanly with `Ctrl-C`.

## Design notes

- The listener and accepted sockets are non-blocking and close-on-exec.
- `accept4()` accepts connections until `EAGAIN` to satisfy edge-triggered epoll semantics.
- Connections use `EPOLLONESHOT`; their interest is explicitly rearmed after each I/O pass.
- Reads, writes, `EINTR`, `EAGAIN`, peer EOF, partial writes, and epoll errors are handled explicitly.
- When the 16 KiB connection buffer is full, the server switches to writing before reading further. This provides bounded memory use and TCP backpressure.

## Testing and benchmarking

The automated integration suite requires Linux, Python 3, and a built server:

```sh
make test
```

The suite starts the server on an ephemeral loopback port and verifies:

- small text and binary payloads;
- payloads larger than the 16 KiB connection buffer;
- fragmented client writes;
- 64 concurrent clients;
- client half-closes after sending, while still receiving the full echo, including
  when the server is blocked writing the echo back;
- clients closing while their echo is still pending do not kill the server with `SIGPIPE`; and
- running out of file descriptors rejects excess connections instead of stopping the server.

Run it against a separately built executable if needed:

```sh
python3 tests/test_echo_server.py --server /path/to/server
```

For a quick manual check:

```sh
printf 'hello\n' | nc 127.0.0.1 8080
```

For load testing, an echo benchmark such as [rust_echo_bench](https://github.com/haraldh/rust_echo_bench) can be used:

```sh
cargo run --release -- --address 127.0.0.1:8080 --number 1000 --duration 60 --length 512
```

For memory and undefined-behavior checks, rebuild with `make sanitize` before running the same workload.
