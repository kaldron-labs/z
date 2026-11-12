# Vault integration test

`ztlsvaultdbustest` starts an isolated session bus using the in-tree
`zdbus/util/zdbusbus` fixture, then serves Secret Service calls with
`ZdbusServer`. Run `make -C zdbus/util -j8` once, followed by
`make -C ztls/itest -j8 && make -C ztls/itest test` in the current build.
The test does not access the user's desktop keyring.

`ztlstxpackettest` establishes a TLS client/server pair over kernel TCP. Separate
small TxTCP allocators split a full 16 KiB record across packet buffers while
TxTLS retains complete plaintext records. The pair checks payload integrity in
both directions, KeyUpdate, once-per-record protection, per-packet completion,
and zero kernel headroom. A syscall fixture applies seven-byte partial writes
and EAGAIN, then verifies that the retained ciphertext resumes without duplicate
TLS/application bytes. A two-buffer queue limit rejects output after a
ciphertext prefix and verifies one failed packet callback and terminal teardown.
Every reservation position for a full record is failed in turn after
establishment, checking unchanged retained plaintext, no protected/published
application record, origin disconnection and final retirement of all packet
references. A synchronous Tx-owner stream also fails its final flush, checks
failed(), and dispatches disconnect before any protection/publication. A finite custom pool also permits handshake output but fails the
maximum-record capacity probe, verifying that application connected() is never
published and the session terminates. Failing the first ClientHello record's
reservation separately verifies terminal handshake teardown without publishing
any failed-record bytes. Run it with `make -C ztls/itest test`.
`ztls/test/ZtlsTxPacketTest` separately exercises fixed fake-provider headroom
and tail sentinels, all reservation failure positions, payload caps and retirement
of unpublished reservations after publication fails.

The kernel pair also runs with duplicate TCP delivery in an isolated network
namespace. This keeps host interfaces unchanged:

```sh
sudo unshare --net -- sh -c 'ip link set lo up && tc qdisc add dev lo root netem duplicate 100% && ./libtool exec ./ztls/itest/ztlstxpackettest'
```

Capture that namespace's loopback traffic with tcpdump to verify identical
ciphertext payload ranges recur at their original TCP sequence positions. The
pair still requires exact plaintext, one protection per application record and
normal reference retirement. This exercises TCP duplicate suppression; it does
not claim AF_XDP or NIC zero-copy operation.

`ztlsrxpackettest` uses a kernel TCP pair with deliberately small, unequal
RxTCP capacities and small TxTCP packet spans. It sends application frames
spanning multiple full authenticated TLS records in both directions and exercises
KeyUpdate. Runs with mux Rx equal to and different from TLS Rx verify RxTLS
allocator identities, fixed input capacity, payload integrity, retained plaintext
lifetime and exactly-once input release after owner drain. An fd-backed signer
deterministically suspends the real certificate-signature callback, checks
resume on both owner arrangements, then closes a suspended connection and
restarts the hubs for a successful reconnect. Input and async-job counts must
balance after final drain. Run it with
`make -C ztls/itest test`. Authentication gating and the absence of ciphertext
gathering are established separately by the isolated sink/parser tests.
