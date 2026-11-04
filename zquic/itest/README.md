# QUIC endpoint integration

Build with `make -C zquic/itest -j8`, then run
`make -C zquic/itest test`.

`zquictxqueuetest` checks datagram order when a transmit-drain callback
submits another packet. Native completion must install the selected queued
packet before invoking that callback. The epoll path checks the same packet
sequence through synchronous sends. Both endpoints bind ephemeral loopback
ports, and teardown drains their existing completion callbacks.
