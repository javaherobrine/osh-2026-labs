# Lab 3

## Already Done

- HTTP Server with thread pool (As it's not declared explictly in the requirement, I overrode the "one thread per connection" with it. It's in `src`)
- HTTP Server with `epoll` (in `epoll_src`)
- LRU Cache (Built-in in both implementation)

## Build

I use `CMake` in both implementations. For HTTP Server implementation with thread pool, use these:
```bash
cmake . -DCPU_COUNT=[CPU_COUNT]
make
```

If without `-DCPU_COUNT=[CPU_COUNT]`, the processor count will be determined with `ProcessorCount` in `CMake`. The executable file is `server.out`, execute it without parameters.

For `epoll` implementation, use these:
```bash
cmake . -DMAX_FD_COUNT=[MAX_FD]
make
```

If without `-DMAX_FD_COUNT=[MAX_FD]`, the maximum file descriptors will be determined with `ulimit -n`. The executable file is `server_epoll.out`, execute it without parameters.

## Architecture

In the thread pool implementation, I implemented a thread pool with `2N+2` threads (N is processor count and is determined automatically in compilation). Also, I converted the socket file descriptor into `FILE*` with `fdopen()` and parsed the header with C Standard Library.
In the output, I switched to LRU cache and `sendfile` syscall for higher performance. When a connection is established, it will be regarded as an event and be put into the event queue of thread pool. To implement the thread pool, a producer-consumer model synchronized with
semaphores was used.

In the `epoll` implementation, I implemented a state machine for an explicit control flow. In the state machine, I successfully coped with the partial read problem and partial write problem with precise state control and transition. In this case, I use `read` syscall directly in the
parser as C Standard Library will interpret `EAGAIN` or `EWOULDBLOCK` as an error and is not capable for event-driven parser as data buffer inside is hidden. Also, I use LRU cache and `sendfile` syscall for higher performance. In fact, it works extremely like Java NIO, JavaScript
Async I/O and Project Loom in Java.

As for LRU (or Least Recently Used) cache, it aims to reduce the overhead of syscalls. If I use `sendfile`, each request will introduce 3 syscalls (`stat`, `open` and `sendfile`). But if I use LRU cache, some requests will only introduce 2 syscalls (`stat` and `write`). In real
web servers, there are lots of small `HTML`, `CSS` and `JavaScript` files, and the syscalls will introduce latency in this case. For large file, the bottleneck is `File I/O` instead of syscall. In this case, I selected to trust Linux page caches. Also, if I put these files
into cache, the memory usage will increase rapidly. So I set a threshold `1MB`, files with size less than this threshold will be cached. Also, I set a limit `10`. If the file count in the cache exceeds this count the LRU one will be evicted. Additionally, there are some details that must be noticed:
- Accessing an entry updates its recency state, so cache access must be treated as a write operation internally.
- If the file is modified, it must be reloaded from disk.
- If the file is deleted, it must be evicted from cache.

## Tests

For large files, execute `./large_file.sh` script.

### With 50MB large files

The `epoll` server showed this with command `siege -c 50 -r 100 -f url.txt -b`, result:

```bash
Transactions:                5000    hits
Availability:                 100.00 %
Elapsed time:                   9.10 secs
Data transferred:           62579.81 MB
Response time:                 87.47 ms
Transaction rate:             549.45 trans/sec
Throughput:                  6876.90 MB/sec
Concurrency:                   48.06
Successful transactions:     5000
Failed transactions:            0
Longest transaction:          630.00 ms
Shortest transaction:           0.00 ms
```

The thread pool one's result: (same command)

```bash
Transactions:                5000    hits
Availability:                 100.00 %
Elapsed time:                   9.66 secs
Data transferred:           62579.81 MB
Response time:                 91.45 ms
Transaction rate:             517.60 trans/sec
Throughput:                  6478.24 MB/sec
Concurrency:                   47.33
Successful transactions:     5000
Failed transactions:            0
Longest transaction:         1010.00 ms
Shortest transaction:           0.00 ms
```

### Without large files

The `epoll` server showed this with command `siege -c 50 -r 100 -f url.txt -b `, result:

```bash
Transactions:                5000    hits
Availability:                 100.00 %
Elapsed time:                   0.13 secs
Data transferred:             105.58 MB
Response time:                  1.14 ms
Transaction rate:           38461.54 trans/sec
Throughput:                   812.14 MB/sec
Concurrency:                   44.00
Successful transactions:     5000
Failed transactions:            0
Longest transaction:           10.00 ms
Shortest transaction:           0.00 ms
```

The thread pool one's result: (same command)

```bash
Transactions:                5000    hits
Availability:                 100.00 %
Elapsed time:                   2.08 secs
Data transferred:             105.58 MB
Response time:                  7.19 ms
Transaction rate:            2403.85 trans/sec
Throughput:                    50.76 MB/sec
Concurrency:                   17.27
Successful transactions:     5000
Failed transactions:            0
Longest transaction:         2060.00 ms
Shortest transaction:           0.00 ms
```

For large files, both implementations achieved high throughput, but the epoll-based server was slightly faster.

For small files, the epoll-based server showed a much larger advantage: it achieved significantly lower response time and much higher transaction rate. This is because the `epoll` implementation avoids the overhead of dispatching every connection through the thread-pool event queue and
reduces thread scheduling/synchronization cost. The tradeoff is that the `epoll` server may consume more CPU under high concurrency as there are more syscalls with `epoll_wait`.

### Robustness Test: Slowloris and Slow Read Attacks

showed in [Possible Hacks](./possible_hacks/hack.md)

Fun fact: `siege` is the bottleneck of this test actually :-)
