# zrest interoperability tests

`make -C zrest/itest test` runs `zrestmatrix`, which supervises the C++ and Go
clients and servers over verified localhost TLS. The Go tool and the pinned Go
module dependencies are required for the full matrix; without Go the driver
reports a TAP skip.

Build the complete `zrest` module first so `zrest/example` and
`zrest/interop/go` contain their peer executables. The matrix generates a
temporary certificate, uses isolated loopback ports, imposes a 15-second case
deadline, and removes every child process and temporary file on exit.
