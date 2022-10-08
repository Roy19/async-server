## Async HTTP Server

The purpose of this repository is to write a simple single threaded, non-blocking asynchronous TCP server in C for Linux based operating systems.

### Building & Running

To build the project, run the following in terminal:

```
make
```

Upon successful build, run the server using:

```
./server
```

Make an TCP call to the server using any netcat.

```
netcat localhost 8080
```

### Benchmarking

To run benchmarks on the server, use an echo server benchmarking tool like this [Rust Echo Server Benchmark](https://github.com/haraldh/rust_echo_bench)

Install cargo:

```
sudo apt update
sudo apt install -y cargo
```

Run the benchmarking tool:

```
cd <path to rust_echo_bench>
cargo run --release -- --address "127.0.0.1:8080" --number 1000 --duration 60 --length 512
```