# Overall

The real world is filled with malicious actors attempting to make servers unavailable through asymmetric resource exhaustion. This directory explores these potential vulnerabilities. I have included 2 `Java` scripts that allows you to safely simulate these attacks against my demo server running on your local machine. The scripts and their benchmark results are all documented in this directory.

# Analysis from the Architecture

In a naive implementation, HTTP servers assign a dedicated thread to each connection. Under high concurrency, this leads to severe performance degradation because the OS kernel spends more CPU cycles scheduling threads than actually processing user requests. As a solution, I implemented a Thread Pool in the server with `2N+2` threads, which is a standard industry heuristic. However, this architecture still introduces vulnerabilities:

*   **Thread Starvation (Slowloris):** Legitimate users are forced to wait if all threads are occupied by slow connections. A hacker can intentionally simulate this by sending exactly one byte every 10 seconds. This exhausts the thread pool, paralyzing the server and locking out legitimate users indefinitely.
*   **The Slow Read Attack:** Hackers can also manipulate the response phase by reading data excruciatingly slowly. This causes the OS kernel to fill its send buffer and respond with `TCP Zero Window` packets, which blocks the `write()` syscall. The worker thread is held hostage until the connection closes.

Furthermore, we know that random I/O is much slower than sequential I/O. A typical web server handles tons of small files like `HTML`, `CSS`, and `JavaScript`. Although `sendfile` saves context-switching overhead, the random disk I/O bottleneck remains. To solve this, I wrote an `LRU (Least Recently Used) cache`. To avoid OOM (Out of Memory) crashes, I limited the maximum file count and evict the least used files. However, this introduces another attack vector:
*   **Cache Thrashing:** If I set the maximum file count to `20`, a hacker can intentionally request `21` different files in a sequential loop. If timed correctly, this guarantees a 100% cache miss rate, forcing the server to constantly read from the disk and defeating the purpose of the cache entirely.

For higher performance, I implemented `epoll` (which operates identically to the `Java NIO` Event Loop I am familiar with) as an extra lab component. It mitigates the thread starvation attacks because legitimate users can still be processed while slow connections wait in the kernel. However, it cannot solve the problem completely:
*   **Syscall Flooding (CPU Exhaustion):** A hacker can enable `TCP_NODELAY` and intentionally force their TCP Segments to carry only 1 byte of payload. This causes `epoll_wait` to wake up constantly. The Event Loop will burn CPU cycles repeatedly transitioning between kernel and user space just to process 1 byte at a time.
*   **Slow Read via Window Updates:** The `Slow Read Attack` also affects `epoll`. By reading exactly 1 byte and immediately sending a `TCP Window Update`, `epoll_wait` is triggered constantly for `EPOLLOUT`, causing the same syscall flooding overhead.

Ultimately, there is **NO** silver bullet for these attacks in application code alone. To properly secure a server, we must use a combination of strict application-layer read/write timeouts and OS-level firewall packet-filtering rules.

# Analysis from the Test Results

During my `siege` benchmarking, I noticed a strange, recurring **1-second latency outlier**. Further investigation revealed this was caused by a low TCP `listen()` backlog queue. If the backlog queue is full, the kernel silently drops new `SYN` packets, forcing the client to wait for the standard 1-second `InitRTO` (Initial Retransmission Timeout) before trying again.

This raises a security question: What if hackers intentionally flood the server with an infinite stream of `SYN` packets but never send the final `ACK`? 
This creates thousands of half-open sockets (a **SYN Flood**). Hackers can achieve this by spoofing fake IP addresses or writing an eBPF program to drop outgoing ACKs. The Linux kernel will not aggressively clean these up, because it deliberately ignores `ICMP Destination Unreachable` packets from routers to prevent **ICMP Blind Reset Attacks** (since anyone can forge an ICMP packet).

Fortunately, the modern Linux kernel solves this via **SYN Cookies**. Instead of saving the half-open connection into a queue and wasting RAM, Linux cryptographically hashes the connection details into the `SYN-ACK` sequence number and instantly *forgets* it. The backlog queue remains safe, and the SYN Flood is neutralized.

# Test with them (with 50MB large files)

In this test, I created 200 clients to simulate malicious actors. 100 for Slowloris and 100 for Slow Read Attack. Then, I tested them with `siege -c 50 -r 100 -f url.txt`

The `epoll` server showed this:

```bash
Transactions:                5000    hits
Availability:                 100.00 %
Elapsed time:                  10.90 secs
Data transferred:           62579.81 MB
Response time:                104.79 ms
Transaction rate:             458.72 trans/sec
Throughput:                  5741.27 MB/sec
Concurrency:                   48.07
Successful transactions:     5000
Failed transactions:            0
Longest transaction:          720.00 ms
Shortest transaction:           0.00 ms
```

The thread pool one's result:

```bash
[alert] socket: select and discovered it's not ready sock.c:384: Connection timed out
[alert] socket: read check timed out(30) sock.c:273: Connection timed out
Transactions:                   0    hits
Availability:                   0.00 %
Elapsed time:                  57.51 secs
Data transferred:               0.00 MB
Response time:                  0.00 ms
Transaction rate:               0.00 trans/sec
Throughput:                     0.00 MB/sec
Concurrency:                    0.00
Successful transactions:        0
Failed transactions:         1073
Longest transaction:            0.00 ms
Shortest transaction:           0.00 ms
```

The thread-pool implementation became **COMPLETELY** unavailable under this attack scenario. My analysis is in the second section of this additional hack report.

The experiment demonstrates that the event-driven non-blocking design is significantly more robust against slow connection attacks than the thread-per-task design.

In the thread-pool implementation, worker threads may remain occupied by malicious slow connections, causing starvation of normal requests. In contrast, the epoll-based implementation maintains connection progress explicitly through a state machine and therefore avoids blocking execution resources on slow clients.
