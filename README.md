## Async HTTP Server

The purpose of this repository is to write a simple single threaded, non-blocking asynchronous server in C for Linux based operating systems.

### Building & Running

To build the project, run the following in terminal:

```
make
```

Upon successful build, run the server using:

```
./server
```

Make an HTTP call to the server using any HTTP client.

```
curl localhost:8080
```

### Benchmarking

To run benchmarks on the server, run use any HTTP server benchmarking tool (example Apache Benchmark):

```
ab -n <no of requests> -c <no of concurrent users> http://localhost:8080/
```