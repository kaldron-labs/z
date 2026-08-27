# Zero Overhead library

Note: Work in Progress

The Z library is a vertically-integrated collection of general purpose
system programming C++ frameworks organized into layers, intended for
latency-sensitive applications and servers. Hallmarks of the library are:

1. Minimal dependency on, and use of, the STL while remaining interoperable
2. No use of, or dependency on, other C++ frameworks (Boost, BDE, Folly, etc.)
3. Not header-only - built and deployed as a traditional combination of headers and C++ ABI shared libraries / DLLs
4. Cross-platform - Linux and Windows
5. Intrusive containers and reference counting
6. No uncontrolled memory allocation, permitting fine-tuned NUMA-aware memory allocation
    - No use of shared_ptr control blocks
7. No uncontrolled thread creation, permitting fine-tuned binding of threads to CPU cores
    - No use of STL/Boost coroutines, threads, promise/futures, asio, etc.
8. Prefer message-passing and shared-nothing sharding to lock-free algorithms or locking
9. Explicitly interoperable with C
10. Lean C dependencies: SSL (picotls fork), lock-free (ck), hardware locality (hwloc), regular expressions (pcre), serialization (flatbuffers), backtracing (bfd)
11. Extensive advanced use of the C macro pre-processor
12. Use of prefixes alongside namespaces, aligning with C and pre-processor naming
13. Brevity - short names, less typing
14. Transparent - intentionally weakly encapsulated - no engine covers
15. Modern C++ functional style and template metaprogramming
16. No dogma
    - No conformance to purely theoretical undefined behavior or standards
17. Physical naming
    - A hash table is a hash table, not a map

## Test port allocations

Network tests use fixed, non-overlapping 100-port blocks so that module test
suites can run concurrently.  An integration-test (`itest`) directory has its
own block.  Ports not assigned below remain reserved for future tests in that
directory; tests which do not open a listening socket do not consume a port.

| Directory | Allocated ports |
| --- | --- |
| `zi/test` | 20000-20099 |
| `ztcp/test` | 20100-20199 |
| `ztls/test` | 20200-20299 |
| `zquic/test` | 20300-20399 |
| `zhttp/test` | 20400-20499 |
| `zhttp/itest` | 20500-20599 |
| `zrest/test` | 20600-20699 |
| `zrest/itest` | 20700-20799 |
| `zws/test` | 20800-20899 |
| `zhttp/interop` | 20900-20999 |
| `zmcp/itest` | 21000-21099 |

Subsidiary allocations within those blocks are:

| Directory | Test | Ports |
| --- | --- | --- |
| `zi/test` | `ZiIPTest` | 20000 |
| `zi/test` | `ZiResolverTest` | 20010 |
| `zi/test` | `ZiEventLoopTest` | 20020 |
| `zi/test` | `ZiMxLoopTest` | 20030-20033 |
| `ztcp/test` | `ZtcpLoopTest` | 20100-20101 |
| `ztls/test` | `ZtlsBufHookTest` | 20200-20299 |
| `zquic/test` | `ZquicSockTest` | 20300-20301 |
| `zquic/test` | `ZquicRuntimeTest` | 20310 |
| `zquic/test` | interoperability fixtures | 20320-20329 |
| `zhttp/test` | `ZhttpH1HubTest` | 20400-20411 |
| `zhttp/test` | `ZhttpH3HubTest` | 20412-20416 |
| `zhttp/test` | `ZhttpH2HubTest` | 20417-20422 |
| `zhttp/test` | `ZhttpMessageH1TCPTest` | 20423 |
| `zhttp/test` | `ZhttpMessageH1TLSTest` | 20424 |
| `zhttp/test` | `ZhttpMessageH2Test` | 20425 |
| `zhttp/test` | `ZhttpMessageH3Test` | 20426 |
| `zhttp/test` | `ZhttpServerIdleTest` | 20427-20436 |
| `zhttp/test` | `ZhttpClientCancelTest` | 20437-20445 |
| `zhttp/test` | `ZhttpClientPoolTest` | 20446-20479 |
| `zhttp/itest` | `zhttpmatrix` | 20500 |
| `zhttp/itest` | `zhttpapptest` | 20510-20516 |
| `zhttp/itest` | `zhttpmultirequesttest` | 20520 |
| `zhttp/itest` | `zhttplifecycletest` | 20530-20532 |
| `zhttp/itest` | `zhttpclientfallbacktest` | 20540 |
| `zhttp/itest` | `zhttpserverstreamtest` | 20550-20569 |
| `zrest/itest` | `zrestmatrix` | 20700-20709 |
| `zws/test` | `ZwsH1HubTest` | 20800-20809 |
| `zws/test` | `ZwsH2HubTest` | 20810-20819 |
| `zws/test` | `ZwsH3HubTest` | 20820-20829 |
| `zws/bench` | `zwsbench` | 20830 |
| `zhttp/interop` | QIR smoke tests | 20900-20903 |
| `zhttp/interop` | HTTP/3 interoperability | 20910-20914 |
| `zhttp/interop` | darkhttpd compatibility | 20920-20921 |
| `zmcp/itest` | plain and legacy HTTP | 21000-21002 |
| `zmcp/itest` | secure H1/H2/H3 | 21010-21012 |

## FAQs

1. why no Z namespace?
    - consistency with the pre-processor (macros are name-scoped with
      prefixes not namespaces)
    - unneeded - Z uses prefixes with a very low probability of collision
      (`Zu`, `Zt`, `Zf`, ...)
    - a short prefix is more succinct
    - no uncontrolled large-scale naming imports (no `using namespace std`)
    - mitigation of C++ name-mangling bloat with heavily templated code
    - intentional design preference for small focused namespaces
    - `Zxx` namespaces exist where useful in context
      (`ZuFmt`, `Ztel`, `Ztls`, ...)
    - `Zxx_` namespaces are used for internals
      (`ZtWindow_`, `Zdb_`, ...)

2. Isn't this pattern in class definitions redundant?
    ```
    using Base::Base;
    template <typename ...Args>
    Derived(Args &&...args) : Base{ZuFwd<Args>(args)...} { }
    ```
    - this is needed in 3 cases:
        - the Base has no constructor (it's a pure data structure)
        - the Base is a template typename (i.e. case 1 could apply)
        - Derived needs to be constructible/convertible from an instance of Base

3. Why so much use of run() / invoke()?
    - intentionally and tightly controlled thread creation within a small pool,
      with key long-running threads performing isolated workloads and bound
      to specific isolated CPU cores for performance
    - sharding (binding data to single threads and passing messages between
      threads via lambdas) is preferred to lock contention or lock-free
      memory contention
        - latency - less jitter due to avoidance of contention
        - throughput - higher concurrency due to pipelining
    - cheap context-switching and message-passing with capturing lambdas is
      enabled by ZmRing (MWSR variant) and ZmScheduler; this is exploited
      for I/O multiplexing by ZiMultiplex
    - threads can be named, bound and isolated by the app and the kernel
        - Linux kernel parameter isolcpus
    - some interfaces require the caller to context-switch prior to making
      the call, others internalize the context-switch within the called function
        - if the caller is potentially sharing the same thread as the callee,
          the callee should not redundantly context-switch - in these cases
          it should be the caller's responsibility (the callee can validate
          the current thread using invoked())
        - if the thread is exclusive to the callee, the callee can internalize
          the context switch
        - callbacks mirror calls in this regard
    - thread/workload association is re-configurable for performance tuning
        - run() passes via the ring buffer and defers calls regardless
        - invoke() is used to elide message-passing make an immediate call when
          the destination is the same thread
        - invoke() should not be used when stack-depth or long-running functions
          are a concern

## DLLs / shared objects

- Zu	- "Universal" - foundation (meta-programming, traits, etc.)
- Zm	- Multithreading - threads, locks, scheduler, concurrent containers
- Zt	- Vocabulary Types - dates/times, arrays, strings, formatting, etc.
- Ze	- Errors & Logging - errors & logging to file / syslog / event log
- Zf	- Data Formats - reflection, JSON, URI, CLI, CSV, ASN.1
- Zi	- I/O - file I/O and socket I/O multiplexing (epoll 
- Zv	- Service Frameworks - I/O framework, option parsing, config files
- Zdb	- Database - in-memory DB, using Zi for HA clustering/replication

## building libbfd shared object (Linux)

```
git clone https://github.com/djnz00/binutils-gdb.git
cd binutils-gdb
git checkout binutils-2_40
./configure --enable-shared --enable-install-libbfd --with-pic --prefix=/usr/local
make -j8 all-bfd
sudo make install-bfd
```

## building libbfd DLL (Windows)

```
./mingw/mingw_bfd_dll.sh
```

See `mingw/README.md` for details.
