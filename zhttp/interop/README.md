# zhttp QUIC Interop Runner Endpoint

This directory contains the `zhttpqir` endpoint for the upstream QUIC Interop
Runner.

## Current Status

Implemented:

- `zhttpqir server|client|--help`
- role parsing and usage exit status `2`
- testcase parsing and unsupported exit status `127`
- environment loading for Docker paths and local override paths
- `REQUESTS` parsing
- HTTPS URL-to-download path mapping
- output parent-directory creation for valid client request paths
- executable-level bad path and mixed-authority rejection before transfer startup
- `hq-interop` request-line parsing
- `hq-interop` request-line generation
- safe `hq-interop` server file mapping under the www root
- direct `hq-interop` `zquic` server/client data transfer
- HTTP/3 server mode using the reusable `zhttpd` static-file implementation
- direct HTTP/3 client data transfer using the `Zhttp::H3` parser/builder path
- non-Docker loopback single-file and multi-file smoke tests for HTTP/3 and
  `hq-interop`
- non-Docker loopback smoke coverage that routes `handshake`, `rebind-port`,
  and `rebind-addr` through the shared `hq-interop` implementation
- Docker image fixed-IP multi-file smoke tests for HTTP/3 and `hq-interop`
- named `ZuTime` endpoint timeout constants
- script lifecycle validation, result collection, report generation, and
  `scripts/qir-run-local --dry-run`
- upstream QUIC Interop Runner validation against `ngtcp2`, `quiche`,
  `quic-go`, and `msquic` for `handshake`, `transfer`, and `http3`
- upstream QUIC Interop Runner validation for `rebind-port` and `rebind-addr`
  passing in both directions against `ngtcp2`; see
  `zhttp/interop/qir-results/local-ngtcp2-rebind/report.md`
- upstream QUIC Interop Runner validation for `rebind-port` and `rebind-addr`
  passing with zquic as server against `quiche` and `quic-go`
- two-connection `resumption` and `zerortt` HQ transfers: the first connection
  obtains a session ticket and remembered transport parameters; the second
  resumes the session and, for `zerortt`, sends requests before the handshake
  completes
- `chacha20`, `retry`, `ecn`, and `keyupdate` test configurations, plus
  `multiconnect` for the runner's handshake loss and corruption scenarios

Failures in the 2026-11-22 full runner matrix:

- With `zquic` as client, quiche's server fails `rebind-port` and
  `rebind-addr`. The server-side captures show matched path validation on
  the new tuple in both cases. The server transmits through the initial
  stream credit of 1 MiB, while the client-side captures have gaps in the
  first 257 KiB. MsQuic's server fails `rebind-addr` and logs `Send failed,
  0x1` after the new client address appears. Both directions against ngtcp2
  and quic-go pass both rebinding cases in that matrix.
- With `zquic` as server, quiche, quic-go, and MsQuic clients complete the
  transfer but send no probe to the advertised preferred port 8443 in
  `connectionmigration`. The runner records three failures. The same
  server passes with ngtcp2, whose client probes and uses the preferred path.

The focused upstream `resumption` and `zerortt` matrix passes all 16 cells
against `ngtcp2`, `quiche`, `quic-go`, and `msquic` with zquic in both roles.
Zquic offers X25519 and P-256; `libZtls` also resumes a ticket after a
group-mismatch HelloRetryRequest and rejects 0-RTT on that path. The raw TLS
tests cover stateful and stateless retry, including a rejected CH2 binder.
The P-256-only interoperability image forces HelloRetryRequest and verifies
PSK selection on the resumed handshake.
The normal image `zhttp-qir:hrr-normal-20261123` passed all 16
`resumption`/`zerortt` cells after the TLS change.
In that forced-retry run, five of six `resumption` pairings pass the upstream
runner. The `zquic` server / `quic-go` client pairing times out after quic-go
offers 0-RTT, receives HelloRetryRequest, and sends its second ClientHello.
This matches the [Go TLS HRR binder issue](https://github.com/golang/go/issues/82210).
The testcase report retains the failed pairing.

The 2026-11-23 focused QIR runs exercise the newly exposed cases. ChaCha20,
Retry, ECN, key update, HTTP/3, and preferred-address migration pass in both
directions against ngtcp2 where applicable. After accepting legal five-byte
Retry source CIDs, Retry also passes in both directions against quic-go.
Against quiche, quic-go, and MsQuic, the core run has 19 raw successes,
13 peer-reported unsupported cells, and four raw failures; the quic-go Retry
fix turns one of those failures into a success. The remaining three are
preferred-address checks where the peer clients never probe port 8443.
The saved core and Retry reports are in
`/home/ubuntu/zquic-qir-gaps-20261123/qir-peers-core/report.md` and
`/home/ubuntu/zquic-qir-gaps-20261123/qir-quicgo-retry-e/report.md`.

The fault run uses a separate fresh handshake per request. All four ngtcp2
loss/corruption cells pass. With the other peers, five of twelve raw cells
pass and seven fail under the runner's 30% loss or corruption scenario. The
raw report is retained at
`/home/ubuntu/zquic-qir-gaps-20261123/qir-peers-loss-e/report.md`.
These failures need further attribution before calling the fault matrix closed:

| server | client | raw failing cases | capture evidence and status |
|---|---|---|---|
| quiche | zquic | corruption | Client Finished packets are visible on the client side only. Quiche's log records an idle timeout on the corresponding connection. |
| quic-go | zquic | loss | A retransmitted retirement of CID sequence 0 precedes missing STREAM data in a packet that `zquic` does not acknowledge. `zquic` now accepts the repeated retirement after the slot has been reused. |
| quic-go | zquic | corruption | Quic-go removes the connection before the first valid client Finished reaches the server. Its log records the later Handshake packet drops. |
| MsQuic | zquic | loss, corruption | Successive client Finished retransmissions are lost or damaged between captures. When the first valid copy reaches MsQuic, its response ends in the advertised stateless reset token. |
| zquic | quic-go | loss, corruption | One loss trace shows a dropped coalesced server flight followed by Initial CRYPTO and a Handshake PING. The companion probe now tries retained Handshake CRYPTO before PING. |

The quiche client and MsQuic client pass both fault cases with `zquic` as
server; the quiche server passes loss with `zquic` as client. The packet
captures, TLS key logs, and peer logs are retained beside each runner result.

## Local Tests

Run the integration tests that exercise the interoperability tooling:

```sh
make -C zhttp/itest test
```

Run the wider zhttp module path:

```sh
make -C zhttp test
```

Docker and upstream runner checks are interop lifecycle steps and are not
prerequisites of `make test`.

The automated interop smoke tests create local www/download/cert directories,
start `zhttpqir server`, run `zhttpqir client`, compare downloaded files, and
terminate the server for these cases:

- `http3` single file
- `http3` multiple files on one connection
- `transfer`/`hq-interop` single file
- `transfer`/`hq-interop` multiple files on one connection
- `resumption` and `zerortt` transfers across two connections

## Executable Contract

```sh
libtool exec ./zhttp/interop/zhttpqir --help
libtool exec ./zhttp/interop/zhttpqir server
libtool exec ./zhttp/interop/zhttpqir client
```

Exit statuses:

- `0`: success
- `1`: endpoint runtime error or unimplemented supported transfer path
- `2`: command-line or environment usage error
- `127`: unsupported `TESTCASE`

Supported and unsupported testcase behavior:

| testcase | status | protocol path |
|---|---:|---|
| `handshake` | `0` on successful transfer | `hq-interop` |
| `transfer` | `0` on successful transfer | `hq-interop` |
| `http3` | `0` on successful transfer | HTTP/3 |
| `rebind-port` | `0` on successful transfer | `hq-interop` |
| `rebind-addr` | `0` on successful transfer | `hq-interop` |
| `connectionmigration` | `0` server, `2` client | server advertises configured preferred bindings; the client follows advertised preferred addresses during `transfer` |
| `versionnegotiation` | `127` | unsupported |
| `chacha20` | `0` on successful transfer | ChaCha20-only TLS suite selection |
| `keyupdate` | `0` on successful transfer | client initiates a 1-RTT key update after handshake confirmation |
| `retry` | `0` on successful transfer | server requires address validation with Retry |
| `ecn` | `0` on successful transfer | ECN-enabled QUIC datagrams and ACK_ECN |
| `multiconnect` | `0` on successful transfers | one fresh handshake per request for loss/corruption scenarios; no ticket reuse |
| `resumption` | `0` on successful transfer | two `hq-interop` connections using a session ticket |
| `zerortt` | `0` on successful transfer | two `hq-interop` connections, with early requests on the second |
| `v2` | `127` | unsupported |

Unknown testcase names return `127`.
The upstream runner selects `multiconnect` at the endpoint for
`handshakeloss` and `handshakecorruption`; these are separate runner scenarios
with packet faults configured by the runner. The table describes the endpoint
contract. Peer endpoints can still return `127` for cases they do not support.

## Preferred server addresses

The `connectionmigration` server configuration uses concrete original and
preferred UDP bindings on one QUIC server. The default ports are 443 and 8443;
`Dockerfile.interop` exposes both. Both bindings share TLS, streams, recovery,
and CID routing. The runner selects `transfer` for the client in this case;
invoking `zhttpqir client` with `TESTCASE=connectionmigration` returns usage
status 2. Ordinary cases keep their existing wildcard binding behavior.

| Variable | Meaning for a `connectionmigration` server |
|---|---|
| `ZHTTP_QIR_PORT` | Original UDP port, decimal 1–65535; default 443 |
| `ZHTTP_QIR_PREF_PORT` | Preferred UDP port, decimal 1–65535; default 8443 |
| `ZHTTP_QIR_ORIG4`, `ZHTTP_QIR_ORIG6` | Numeric original IP by family; `off` disables the family |
| `ZHTTP_QIR_PREF4`, `ZHTTP_QIR_PREF6` | Numeric preferred IP; unset uses that family's original; `off` omits its preferred endpoint |
| `ZHTTP_QIR_IFACE` | Entrypoint discovery interface; default `eth0` |

Addresses must be concrete unicast values of their declared family. Hostnames,
CIDR suffixes, bracketed addresses, embedded ports, wildcard, multicast,
IPv4-mapped IPv6, and scoped/link-local IPv6 values are rejected. An enabled
preferred family requires an original of that family. Equal original/preferred
IP and port pairs are normalized away; at least one alternative must remain.
Different-IP alternatives may use the original port. Invalid explicit values
return usage status 2; bind or allocator failure returns runtime status 1.

The entrypoint discovers each unset original independently with
`ip -o -4|-6 addr show dev "$ZHTTP_QIR_IFACE" scope global`. It excludes tentative
and deprecated addresses and deduplicates values. No address disables that
family, one supplies its original, and multiple addresses fail with runtime
status 1 and require an explicit original. Explicit originals, including `off`,
are preserved. Discovery command/interface failure and zero enabled families
also return runtime status 1 before exec. Interface names must be nonempty
single arguments with no leading `-`.

Direct execution of `zhttpqir server` does no interface discovery: missing
originals are absent, so supply at least one original explicitly. For example,
use `TESTCASE=connectionmigration ZHTTP_QIR_ORIG4=127.0.0.1
ZHTTP_QIR_ORIG6=off ZHTTP_QIR_PORT=9443 ZHTTP_QIR_PREF_PORT=10443` with the
certificate and www variables from the smoke example below. The client uses
`TESTCASE=transfer` and requests the original address. Simulator routing always
uses addresses discovered on `eth0`, independently of any configured preferred
address; local bridge fixtures skip simulator setup.

Configuration and script fixtures cover typed addresses, family/port errors,
missing/ambiguous discovery, overrides, and IPv6 absence. The local HQ smoke
fixture configures both a changed port and a different loopback IP. These checks
alone do not establish reference-peer interoperability; focused packet captures
and the final 176-cell report remain the interoperability evidence.

## Local Non-Docker Smoke

The normal smoke path is:

```sh
make -C zhttp/itest test
```

For manual debugging, create temporary directories and run the endpoint through
libtool:

```sh
tmp=$(mktemp -d)
mkdir -p "$tmp/www" "$tmp/downloads" "$tmp/certs"
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout "$tmp/certs/priv.key" \
  -out "$tmp/certs/cert.pem" \
  -subj /CN=localhost \
  -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1' \
  -days 1
cp "$tmp/certs/cert.pem" "$tmp/certs/ca.pem"
printf 'hello\n' >"$tmp/www/file.txt"

TESTCASE=http3 \
ZHTTP_QIR_WWW="$tmp/www" \
ZHTTP_QIR_CERT="$tmp/certs/cert.pem" \
ZHTTP_QIR_KEY="$tmp/certs/priv.key" \
ZHTTP_QIR_PORT=9443 \
libtool exec ./zhttp/interop/zhttpqir server
```

In another shell:

```sh
TESTCASE=http3 \
REQUESTS=https://127.0.0.1:9443/file.txt \
ZHTTP_QIR_DOWNLOADS="$tmp/downloads" \
ZHTTP_QIR_CA="$tmp/certs/ca.pem" \
libtool exec ./zhttp/interop/zhttpqir client
```

Stop the server with SIGTERM after the client exits.

The certificate must include a SAN matching the request host. A certificate
with only `/CN=localhost` is rejected by the QUIC/TLS verifier and looks like a
transfer timeout.
Interop-runner clients trust `/certs/ca.pem`; servers present
`/certs/cert.pem` and `/certs/priv.key`.

## Docker Prefix

Interop Docker images must be built with:

```sh
./z.config -c -Q -L /usr/local
make clean
make -j8 zhttp-qir-image
```

Use `/usr/local` because the image entrypoint invokes
`/usr/local/bin/qir_endpoint.sh`, which executes `/usr/local/bin/zhttpqir`, and
the staged shared libraries are expected under `/usr/local/lib`.

`/opt/z` remains fine for host-side developer builds that do not create the
Docker image.

## Docker Image Lifecycle

Build the image through the top-level automake target:

```sh
./z.config -c -Q -L /usr/local
make clean
make -j8 zhttp-qir-image
```

Use a revisioned tag by setting `ZHTTP_QIR_IMAGE`:

```sh
ZHTTP_QIR_IMAGE=zhttp-qir:$(git rev-parse --short HEAD) \
  make -j8 zhttp-qir-image
```

Build from an existing staged root for debugging:

```sh
scripts/qir-build-image --root zhttp/interop/qir-root --tag zhttp-qir:local
```

Upload only an image that has already been validated:

```sh
scripts/qir-upload-image \
  --source zhttp-qir:$(git rev-parse --short HEAD) \
  --target ghcr.io/<org>/zhttp-qir:$(git rev-parse --short HEAD)
```

Authenticate manually first with `docker login`. Do not publish `latest` until
the exact digest has passed the required upstream matrix.

## Docker Smoke

After building `zhttp-qir:local`, run a local image smoke with a SAN that
matches the fixed server container IP:

```sh
tmp=$(mktemp -d)
net=qir-smoke-$$
srv=qir-smoke-server
ip=172.28.44.10
mkdir -p "$tmp/www/nested" "$tmp/downloads" "$tmp/certs"
printf 'alpha\n' >"$tmp/www/a.txt"
printf 'bravo\n' >"$tmp/www/nested/b.txt"
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout "$tmp/certs/priv.key" \
  -out "$tmp/certs/cert.pem" \
  -subj /CN=localhost \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1,IP:$ip" \
  -days 1
cp "$tmp/certs/cert.pem" "$tmp/certs/ca.pem"
docker network create --subnet 172.28.44.0/24 "$net"
docker run -d --name "$srv" --network "$net" --ip "$ip" \
  -e TESTCASE=http3 -e ZHTTP_QIR_PORT=9443 \
  -e ZHTTP_QIR_WWW=/qir/www \
  -e ZHTTP_QIR_CERT=/qir/certs/cert.pem \
  -e ZHTTP_QIR_KEY=/qir/certs/priv.key \
  -v "$tmp/www:/qir/www:ro" \
  -v "$tmp/certs:/qir/certs:ro" \
  zhttp-qir:local server
docker run --rm --network "$net" --user "$(id -u):$(id -g)" \
  -e TESTCASE=http3 \
  -e REQUESTS="https://$ip:9443/a.txt https://$ip:9443/nested/b.txt" \
  -e ZHTTP_QIR_DOWNLOADS=/qir/downloads \
  -e ZHTTP_QIR_CA=/qir/certs/ca.pem \
  -v "$tmp/downloads:/qir/downloads" \
  -v "$tmp/certs:/qir/certs:ro" \
  zhttp-qir:local client
cmp "$tmp/www/a.txt" "$tmp/downloads/a.txt"
cmp "$tmp/www/nested/b.txt" "$tmp/downloads/nested/b.txt"
docker rm -f "$srv"
docker network rm "$net"
rm -rf "$tmp"
```

Repeat the server and client commands with `TESTCASE=transfer` to smoke the
`hq-interop` path. Use `--user "$(id -u):$(id -g)"` on the client for local
manual runs so downloaded files are easy to remove from the host temp
directory; the upstream runner may still run containers as root.

Manual smoke commands may pass `server` or `client` as argv. The upstream
runner normally starts the image with no argv and sets `ROLE=server` or
`ROLE=client`; `qir_endpoint.sh` supports both forms.

When the entrypoint detects the upstream runner's `193.167.*` simulator
topology, it disables checksum offload and routes simulator traffic through
`sim`. It skips that setup on ordinary local Docker bridge networks.

## Upstream Runner

Clone the upstream `quic-interop-runner` outside this repository and run the
local lifecycle wrapper. The wrapper temporarily adds a `zquic` entry to the
runner's `implementations_quic.json`, runs the selected matrix, collects logs,
and restores the runner file on exit.

Run the local lifecycle wrapper:

```sh
scripts/qir-run-local --runner /path/to/quic-interop-runner \
  --image zhttp-qir:local \
  --url https://github.com/<final-upstream>/z \
  --peers ngtcp2,quiche,quic-go,msquic \
  --build-type release
```

The default matrix has 22 cases in both roles: handshake, transfer, longrtt,
chacha20, multiplexing, retry, resumption, zerortt, http3, blackhole, keyupdate,
ecn, amplificationlimit, handshakeloss, transferloss, handshakecorruption,
transfercorruption, ipv6, v2, rebind-port, rebind-addr and connectionmigration.
With four peers this runs 176 cells. Use `--tests` to select a focused subset;
a three-case smoke test does not cover the full interoperability matrix.

The 2026-11-22 repair-4 image
`sha256:78907feac7568edb6ed0421b42efbba96e70ec9f91e2e3d641d3a25b2582d653`
passed focused `connectionmigration` against ngtcp2 with zquic in both roles.
The server handshake advertised IPv6 port 8443 and its per-connection CID;
packet capture showed a challenge from port 8443, a matching client response,
the client's CID change, and a complete 2 MiB download. Focused quiche,
quic-go and MsQuic clients sent no packet to port 8443, so the preferred-path
proof check failed for those pairings; their server roles were reported
unsupported. Retain these as failed and unsupported results separately. The
full 176-cell run on this image completed with 95 succeeded, 74 unsupported,
and 7 failed cells. Both ngtcp2 directions had no failures. The failed cells
were zquic server/quiche client `multiplexing` and `connectionmigration`,
zquic server/quic-go client `connectionmigration`, zquic server/MsQuic client
`connectionmigration`, quiche server/zquic client `rebind-port` and
`rebind-addr`, and MsQuic server/zquic client `rebind-addr`. In the full-run
captures, each of the three failing preferred-address clients sent zero UDP
packets to port 8443; their file downloads completed. The rebinding and
multiplexing failures remain failures with their logs in the saved report.
The full report is at
`/home/ubuntu/zquic-repair4-evidence-20261122/qir-full/report.md`.

If `--url` is omitted, the wrapper derives a GitHub HTTPS URL from
`remote.origin.url` when possible and otherwise uses the repository's expected
upstream URL.

The wrapper writes `report.md` into the output directory. Regenerate it after
manual artifact edits with:

```sh
scripts/qir-report \
  --in zhttp/interop/qir-results/<run-dir> \
  --out zhttp/interop/qir-results/<run-dir>/report.md
```

For an upstream registration PR, add the implementation entry in the upstream
runner repository, include the tested image digest, attach the generated report
summary, and do not commit credentials or local paths.

QIR endpoints use dedicated UDP receive and transmit allocators registered with
the multiplexer. Stream and CRYPTO buffers retain application bytes independently
of packet I/O.

The local QIR launcher uses `qir-run.py` to correct the preferred-address
probe-order check: RFC 9000 section 9.6.2 allows a server PATH_RESPONSE before
its own PATH_CHALLENGE. The check still requires a challenge on every new
server path before non-probing output, matching client responses, successful
file transfer, and the upstream client CID-change checks. Ordinary rebinding
checks and the external runner checkout are unchanged.
