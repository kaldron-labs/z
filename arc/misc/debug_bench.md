# Debug Build zhttpmatrix Benchmark

These are single-run `j3n10000` results from the debug build. They should be
used for relative debugging signal only, not release-performance claims.

Command shape:

```sh
libtool exec ./zhttp/test/zhttpmatrix --quiet --discard-response \
  --timeout=60 --stall-timeout=60 --quiet-timeout=20 --case=<case>
```

All cases passed.

| case | duration | req/s |
|---|---:|---:|
| `curl-caddy/h1-tcp/j3n10000` | 704 ms | 14,205 |
| `curl-caddy/h1-tls/j3n10000` | 800 ms | 12,500 |
| `curl-caddy/h3/j3n10000` | 1080 ms | 9,259 |
| `zhttp-caddy/h1-tcp/j3n10000` | 549 ms | 18,215 |
| `zhttp-caddy/h1-tls/j3n10000` | 632 ms | 15,823 |
| `zhttp-caddy/h3/j3n10000` | 1887 ms | 5,299 |
| `curl-zhttpd/h1-tcp/j3n10000` | 752 ms | 13,298 |
| `curl-zhttpd/h1-tls/j3n10000` | 1184 ms | 8,446 |
| `curl-zhttpd/h3/j3n10000` | 2301 ms | 4,346 |
| `zhttp-zhttpd/h1-tcp/j3n10000` | 585 ms | 17,094 |
| `zhttp-zhttpd/h1-tls/j3n10000` | 1001 ms | 9,990 |
| `zhttp-zhttpd/h3/j3n10000` | 2985 ms | 3,350 |

Raw output was captured in `/tmp/zhttpmatrix-j3n10000-bench.out`.
