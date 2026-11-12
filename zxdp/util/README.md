# Standalone packet utility

`zxdppacket` runs a raw Ethernet/IPv4/UDP client or server using the public packet
API. The main thread runs the library receive loop; a separate utility Tx owner
drives completions and the application deadline. Packets are built directly in
Tx backing; the independent peer
checks headers, checksums, sequence and payload. Every accepted transmission must
complete. The utility retains at most one unsent packet during local exhaustion.
There is no network loss recovery; a dropped packet expires the deadline.

Build with top-level `make -j8`, then run the scoped fixture:

```
sudo -n bash zxdp/util/zxdpfixture.sh --count 32 --payload 1472 --mode wait
sudo -n bash zxdp/util/zxdpfixture.sh --count 32 --payload 129 --mode spin
sudo -n bash zxdp/util/zxdpfixture.sh --count 32 --payload 128 --mode ll
```

The fixture creates an anonymous network namespace and its own veth pair. It
retains configuration and logs in the printed directory and removes its owned
network resources on namespace exit. Existing host network state is preserved.
The modes configure the library's receive spin count before its internal
epoll wait. `--spin` is passed through to `Params::spin`; `--timeout` controls
only the utility Tx service backoff, not receive blocking. Veth uses software-copy operation.

For caller-owned physical interfaces use `--physical --output NEW_DIR --` followed
by utility arguments; the fixture then runs only the utility and records logs.
Run the utility without arguments for its CLI. `--frames` must cover every Rx
queue. Payload must fit MTU minus 28 bytes. The default absent mask takes over
all traffic on the chosen interface. Provision peer addressing yourself.
