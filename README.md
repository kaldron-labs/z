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
