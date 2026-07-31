# Release Build zhttpmatrix Benchmark

These are single-run `j3n10000` results from the release build.

Command shape:

```sh
libtool exec ./zhttp/test/zhttpmatrix --quiet --discard-response \
  --timeout=60 --stall-timeout=60 --quiet-timeout=20 --case=<case>
```

All cases passed.

| case | duration | req/s |
|---|---:|---:|
| `curl-caddy/h1-tcp/j3n10000` | 691 ms | 14,472 |
| `curl-caddy/h1-tls/j3n10000` | 780 ms | 12,821 |
| `curl-caddy/h3/j3n10000` | 1060 ms | 9,434 |
| `zhttp-caddy/h1-tcp/j3n10000` | 482 ms | 20,747 |
| `zhttp-caddy/h1-tls/j3n10000` | 523 ms | 19,120 |
| `zhttp-caddy/h3/j3n10000` | 764 ms | 13,089 |
| `curl-zhttpd/h1-tcp/j3n10000` | 632 ms | 15,823 |
| `curl-zhttpd/h1-tls/j3n10000` | 698 ms | 14,327 |
| `curl-zhttpd/h3/j3n10000` | 821 ms | 12,180 |
| `zhttp-zhttpd/h1-tcp/j3n10000` | 311 ms | 32,154 |
| `zhttp-zhttpd/h1-tls/j3n10000` | 421 ms | 23,753 |
| `zhttp-zhttpd/h3/j3n10000` | 409 ms | 24,450 |

Raw output was captured in `/tmp/zhttpmatrix-j3n10000-release.out`.

## Post-consolidation success-path spot check

After the executable teardown and span repairs, the GCC 16 release programs
were checked again with 10,000 requests at concurrency three.  These runs
precede the final library-owned resolver shutdown repair, which affects
process teardown rather than the request data path.

| case | duration | req/s |
|---|---:|---:|
| `zhttp-zhttpd/h1-tcp/j3n10000` | 462 ms | 21,645 |
| `zhttp-zhttpd/h1-tls/j3n10000` | 606 ms | 16,502 |
| `zhttp-zhttpd/h3/j3n10000` | 649 ms | 15,408 |

## Final policy-specific checks

The final Clang debug build adds a reusable, self-verifying prefer-mode row:

```sh
libtool exec ./zhttp/test/zhttpmatrix \
  --case=zhttp-zhttpd/h3-prefer/j1n10000 --timeout=90
```

The row requires at least two requests, starts `zhttpd` with TLS and QUIC,
runs `zhttp` with its normal prefer policy, and fails unless verbose connection
reporting proves both the initial TLS connection and the subsequent QUIC
connection.  The 10,000-request serial run passed in 6,597 ms (1,516 req/s).
This is debug/instrumented throughput and is recorded as a reproducible policy
baseline, not compared directly with the GCC release table above.

`ZhttpAgentFallbackTest` is the complementary deterministic policy fixture. It
proves an untrusted H3 attempt falls back to TLS/H1, then uses a separately
trusted Agent to prove the first TLS/H1 response populates the library-owned
Alt-Svc cache and the second request selects QUIC/H3. Five complete debug
fixture runs, including certificate generation, service start/stop, both
Agents, and orderly teardown, took 268, 234, 279, 271, and 247 ms (268 ms
median). The fixture also passes strict Valgrind memcheck and ASAN/LSAN.
