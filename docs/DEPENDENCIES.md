# Z dependencies

This inventory separates production/build dependencies from optional test and
interoperability tools. Version constraints come from [configure.ac](configure.ac);
link requirements also appear in module `src/Makefile.am` files. Install headers,
libraries and pkg-config metadata, not just runtime packages. Z does not require
Boost, Folly or BDE for its production libraries.

## Toolchain

- Current [GCC](https://gcc.gnu.org/) or [Clang/LLVM](https://clang.llvm.org/),
  with GNU C++23 (`-std=gnu++2b`) support. The source uses modern features such
  as explicit object parameters; an older compiler accepting the flag may still
  be insufficient.
- [GNU Make](https://www.gnu.org/software/make/),
  [Autoconf](https://www.gnu.org/software/autoconf/),
  [Automake](https://www.gnu.org/software/automake/),
  [Libtool](https://www.gnu.org/software/libtool/),
  [GNU M4](https://www.gnu.org/software/m4/), and
  [Perl](https://www.perl.org/). `z.config -c` runs `autoreconf -i`.
- A pkg-config-compatible tool for dependency discovery: either the original
  [pkg-config](https://www.freedesktop.org/wiki/Software/pkg-config/) or
  [pkgconf](https://github.com/pkgconf/pkgconf). Only one is required. The build
  searches for `pkg-config`; if pkgconf is not installed under that name, set
  `PKG_CONFIG=pkgconf` when configuring.
- [GNU Binutils](https://www.gnu.org/software/binutils/) or suitable LLVM
  tools. Linux link flags request Zstandard-compressed debug sections.
- On Windows: [MSYS2](https://www.msys2.org/) and
  [MinGW-w64](https://www.mingw-w64.org/); the wrapper selects LLVM's `lld`
  linker for MinGW builds.

## Required libraries

| Dependency | Requirement / discovery | Used for |
| --- | --- | --- |
| [Concurrency Kit](https://concurrencykit.org/) | `ck >= 0.6` | Atomic/concurrency primitives |
| [hwloc](https://www.open-mpi.org/projects/hwloc/) | `hwloc >= 2` | Hardware topology, affinity and NUMA placement |
| [PCRE](https://www.pcre.org/) | Original `libpcre`, discovered by `AX_PATH_LIB_PCRE` | `ZtRegex`; PCRE2 is not a drop-in replacement for this build |
| [c-ares](https://c-ares.org/) | `libcares` | Asynchronous DNS resolution |
| [zpicotls](https://github.com/kaldron-labs/zpicotls) | `zpicotls-core`, `zpicotls-openssl`, `zpicotls-fusion`, each `>= 1.3` | TLS 1.3 and QUIC cryptographic integration |
| [OpenSSL](https://openssl-library.org/) | `openssl >= 3.5`, with working ML-KEM-768 provider/API | zpicotls backend and Z cryptographic operations |
| [BFD and libiberty](https://www.gnu.org/software/binutils/) | `-lbfd -liberty` in `zm/src/Makefile.am` | Backtraces and symbol handling; current Zm build links both unconditionally |
| [ncurses/terminfo](https://invisible-island.net/ncurses/) | Linux `-lncurses`; other Unix branch uses `-lterminfo` | Terminal capabilities for Zrl |
| [GNU libiconv](https://www.gnu.org/software/libiconv/) | MinGW `-liconv`; libc supplies iconv on Linux | Character encoding conversion |

Use **zpicotls**, the Z fork of [picotls](https://github.com/h2o/picotls),
with all three pkg-config components above. Installing upstream picotls alone
does not satisfy configure. Follow the fork's build instructions, including
its submodules and backend prerequisites. OpenSSL is checked independently:
configure creates an ML-KEM-768 key and initializes encapsulation, rather than
accepting the version string alone. Cross-compilation defers that runtime check.

Platform runtime linkage also includes the C/C++ runtimes, math and atomic
support; Linux uses pthreads, realtime and dynamic-loading facilities. Windows
uses Winsock, DNS and Advapi32 system libraries. These are supplied by the
platform/toolchain rather than separate Z packages.

## Required platform services

| Dependency | Requirement / purpose |
| --- | --- |
| [D-Bus](https://www.freedesktop.org/wiki/Software/dbus/) bus service | Required on Linux by the Ztls vault. Native Zdbus implements the wire protocol directly and does not link libdbus; the libdbus development package is only an interoperability testing dependency. |

## Optional modules and services

| Dependency | Selection | Used for |
| --- | --- | --- |
| [SQLite](https://sqlite.org/) | `sqlite3 >= 3.37.0`; enabled by default; disable with `z.config -S` or `configure --without-sqlite` | Zdb SQLite backing store |
| [PostgreSQL](https://www.postgresql.org/) | `libpq >= 16`; wrapper enables it unless `-P`; direct configure requires `--with-pq=yes` | Zdb PostgreSQL backing store and PostgreSQL integration; extension builds also need server development headers/tools |
| [GTK 3](https://www.gtk.org/) | `gtk+-3.0 >= 3.24.0`; wrapper enables it unless `-G`; direct configure uses `--with-gtk=yes` | ZGtk and GUI tooling |
| [GNOME Keyring](https://gitlab.gnome.org/GNOME/gnome-keyring) / [Secret Service](https://specifications.freedesktop.org/secret-service/latest/) | Linux Vault keyring backend/service | Desktop credential storage and isolated Vault interoperability tests |

GTK brings its normal transitive dependencies, including
[GLib/GObject/GIO](https://gitlab.gnome.org/GNOME/glib),
[Pango](https://pango.gnome.org/), [Cairo](https://www.cairographics.org/),
[GdkPixbuf](https://gitlab.gnome.org/GNOME/gdk-pixbuf),
[ATK](https://gitlab.gnome.org/Archive/atk), and platform display/font libraries.
Use the distribution's development packages so their dependency closure and
pkg-config flags agree. Similarly, OpenSSL, hwloc and database package closures
are managed by their respective builds; they are not independently vendored by Z.

## Test, interoperability and benchmark dependencies

These are not production codec/network dependencies. Install the tools for the
suites you intend to run; [TESTING.md](TESTING.md) covers environment and ports.

| Dependency | Consumers / purpose |
| --- | --- |
| [libdbus](https://www.freedesktop.org/wiki/Software/dbus/) | `dbus-1` development package detected by configure; builds the independent `zdbuspeer` reference peer for Zdbus interoperability tests, which also require `dbus-daemon` |
| [Perl Test::Harness / prove](https://metacpan.org/pod/prove) | TAP test execution |
| [Python 3](https://www.python.org/), [cryptography](https://cryptography.io/), [Tomli](https://github.com/hukkin/tomli) | Integration drivers; Zum cryptographic fixtures; independent TOML parser (must support the TOML 1.1 cases in the fixture) |
| [Go](https://go.dev/) | REST and MCP reference peers; versions are declared in each `go.mod` |
| [FlatBuffers](https://github.com/google/flatbuffers) and `flatc` | `flatbuffers >= 24`; explicit Zfb/Zdb/Ztc interoperability and comparison targets only |
| [Protocol Buffers](https://github.com/protocolbuffers/protobuf) and `protoc` | `protobuf >= 25`; explicit Zpb interoperability target only |
| [curl](https://curl.se/), [Caddy](https://caddyserver.com/), [nghttp2](https://nghttp2.org/) (`nghttp`, `nghttpd`), [h2spec](https://github.com/summerwind/h2spec), [darkhttpd](https://github.com/emikulic/darkhttpd) | HTTP reference clients, servers and conformance/compatibility fixtures; curl needs the protocol support being exercised |
| [Docker](https://www.docker.com/), [QUIC Interop Runner](https://github.com/quic-interop/quic-interop-runner) | Containerized QUIC/HTTP interoperability |
| [ngtcp2](https://github.com/ngtcp2/ngtcp2), [quiche](https://github.com/cloudflare/quiche), [quic-go](https://github.com/quic-go/quic-go), [MsQuic](https://github.com/microsoft/msquic) | External QIR peers, selected by runner configuration |
| [Autobahn Testsuite](https://github.com/crossbario/autobahn-testsuite) | Opt-in WebSocket interoperability via Docker |
| [age](https://github.com/FiloSottile/age) | Independent encrypted-file interoperability; local age vectors also originate from [C2SP CCTV](https://github.com/C2SP/CCTV) |
| [OpenSSH](https://www.openssh.com/) | SSH-key fixtures/tools for age/key-loading tests |
| [X.Org Xvfb](https://www.x.org/), `xvfb-run` | Headless GTK/dashboard tests |
| [GDB](https://www.gnu.org/software/gdb/), [Valgrind](https://valgrind.org/), [LLVM sanitizers](https://clang.llvm.org/docs/AddressSanitizer.html) | Debugging, allocation tracing, memory/leak checks |

The QIR image additionally installs [ethtool](https://www.kernel.org/pub/software/network/ethtool/),
[iproute2](https://wiki.linuxfoundation.org/networking/iproute2),
[net-tools](https://net-tools.sourceforge.io/) and
[OpenBSD netcat](https://www.openbsd.org/) for network setup and diagnostics;
see [Dockerfile.interop](zhttp/interop/Dockerfile.interop).

### Pinned Go fixture packages

[zrest/interop/go/go.mod](zrest/interop/go/go.mod) declares Go 1.22 and
[golang-jwt/jwt v5.2.1](https://github.com/golang-jwt/jwt).
[zmcp/interop/go.mod](zmcp/interop/go.mod) declares Go 1.26.0 and
[mcp-go v1.0.0-beta.1](https://github.com/mark3labs/mcp-go), plus these recorded
indirect dependencies:

- [google/jsonschema-go v0.4.2](https://github.com/google/jsonschema-go)
- [google/uuid v1.6.0](https://github.com/google/uuid)
- [santhosh-tekuri/jsonschema/v6 v6.0.2](https://github.com/santhosh-tekuri/jsonschema)
- [spf13/cast v1.7.1](https://github.com/spf13/cast)
- [yosida95/uritemplate/v3 v3.0.2](https://github.com/yosida95/uritemplate)
- [golang.org/x/text v0.14.0](https://pkg.go.dev/golang.org/x/text)

The manifests and accompanying `go.sum` files are authoritative for fixture
versions. Z's local Go residue utility is repository code, not a third-party
package.

## BFD shared libraries

The existing Linux recipe builds a shared BFD from the
[project's Binutils fork](https://github.com/djnz00/binutils-gdb):

```sh
git clone https://github.com/djnz00/binutils-gdb.git
cd binutils-gdb
git checkout binutils-2_40
./configure --enable-shared --enable-install-libbfd --with-pic --prefix=/usr/local
make -j8 all-bfd
sudo make install-bfd
```

Provide libiberty as well, and ensure the compiler and runtime loader can find
the installed headers/libraries. Package builds may supply these without the
custom build above.

On Windows, run from an MSYS2 **MINGW64** shell:

```sh
./mingw/mingw_bfd_dll.sh
```

The script builds BFD and libsframe DLLs/import libraries from MinGW archives.
Its link dependencies include libiberty, Binutils libsframe,
[GNU gettext/libintl](https://www.gnu.org/software/gettext/),
[zlib](https://zlib.net/) and [Zstandard](https://facebook.github.io/zstd/).
See [mingw/README.md](mingw/README.md) for installation and rebuild behavior.

For a nonstandard prefix, configure `PKG_CONFIG_PATH`, compiler include/library
search paths, and the platform runtime loader consistently. See
[z.config](z.config) for the wrapper's feature defaults and
[configure.ac](configure.ac) for exact checks.

## Zfix independent FIX peer

QuickFIX/C++ is an optional fixture dependency, separate from the Zfix libraries.
The pinned version, source/dictionary checksums and SSL build helper are in
[zfix/interop](zfix/interop/README.md). Build prerequisites are CMake, a C++17
compiler, OpenSSL development files, curl, tar and SHA-256 tools. The helper
uses a caller-selected installation root and verifies the canonical FIX 4.4
dictionary. Configure `--with-quickfix=PREFIX` to enable the independent codec
fixture (or `--with-quickfix=yes` with pkg-config); omit it for the production
build. `QUICKFIX_DICT` selects the canonical FIX44.xml path. The runner verifies
the pinned dictionary checksum. Dependency setup and codec checks alone do not
satisfy the live interoperability gate.
