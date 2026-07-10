# zhttp QUIC Interop Runner Endpoint

This directory contains the `zhttpqir` endpoint for the upstream QUIC Interop
Runner. The implementation follows `../../quic_interop.md`.

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

Known gaps:

- `rebind-port` and `rebind-addr` with zquic as client pass against `ngtcp2`
  but currently fail against `quiche`, `quic-go`, and `msquic`
- `rebind-port` and `rebind-addr` with zquic as server currently fail against
  `msquic`; the latest full-reference rebinding report is
  `zhttp/interop/qir-results/local-rebind-refs/report.md`

## Local Tests

Run the interop-local checks:

```sh
make -C zhttp/interop test
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
| `connectionmigration` | `127` | unsupported until preferred-address behavior is verified |
| `versionnegotiation` | `127` | unsupported |
| `chacha20` | `127` | unsupported |
| `keyupdate` | `127` | unsupported |
| `retry` | `127` | unsupported |
| `resumption` | `127` | unsupported |
| `zerortt` | `127` | unsupported |
| `v2` | `127` | unsupported |

Unknown testcase names return `127`.

## Local Non-Docker Smoke

The normal smoke path is:

```sh
make -C zhttp/interop test
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
  --peers ngtcp2 \
  --tests handshake,transfer,http3 \
  --build-type release
```

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
