# Zquic Buffer Contract

`Zquic` uses separate packet and stream `ZiIOBuf` heaps.

- `PacketBufAlloc` uses the `"Zquic.Packet"` heap and built-in size `1472`.
- `StreamBufAlloc` uses the `"Zquic.StreamBuf"` heap and built-in size `1472`.
- Packet buffers hold UDP datagrams and QUIC packet ciphertext/plaintext during
  receive and transmit packetization.
- Stream buffers hold application stream bytes and are retained while stream
  data may need ACK, loss, retransmission, reset, cancellation, or teardown
  handling.

Transmit packet protection is source-to-destination. Retained plaintext
stream/control byte ranges are gathered as read-only inputs and encrypted
directly into the final packet buffer. Tx packet protection must not use an
intermediate ciphertext staging buffer.

Receive packet protection decrypts in place in the packet buffer. After frame
parsing, each STREAM payload slice is copied exactly once from the decrypted
packet buffer into a stream buffer before application delivery through
`ZiRxStream`. That copy is required and counted separately.

Hidden fallback copies below the public API boundary are forbidden. Any new
copy path must be documented here, counted in diagnostics, and covered by a
test that explains why it is required.
