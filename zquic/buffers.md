# Zquic Buffer Contract

`Zquic` uses role-specific `ZiIOBuf` heaps.

- `PacketRxBufAlloc` uses the `"Zquic.Packet.Rx"` heap for received UDP
  datagrams and in-place QUIC packet decryption.
- `PacketTxBufAlloc` uses the `"Zquic.Packet.Tx"` heap for transmit
  packetization and packet protection output.
- `StreamTxBufAlloc` uses the `"Zquic.Stream.TxBuf"` heap for application
  stream bytes retained while stream
  data may need ACK, loss, retransmission, reset, cancellation, or teardown
  handling.
- STREAM receive data is represented by retained slices of the owning Rx packet
  buffer. Frame-processing APIs that accept STREAM payload require that packet
  reference; there is no frame-only STREAM copy fallback.
- `CryptoRxBufAlloc` and `CryptoTxBufAlloc` are named CRYPTO/TLS buffers.
  Fragmented CRYPTO reassembly can still copy into retained crypto Rx buffers;
  in-order CRYPTO delivery and TLS output use direct spans/origin-backed
  buffers.

Transmit packet protection is source-to-destination. Retained plaintext
stream/control byte ranges are gathered as read-only inputs and encrypted
directly into the final packet buffer. Tx packet protection must not use an
intermediate ciphertext staging buffer.

Receive packet protection decrypts in place in the packet buffer. After frame
parsing, each STREAM payload slice is queued with a `ZmRef` to the decrypted
packet buffer before application delivery through `ZiRxStream`.

Hidden fallback copies below the public API boundary are forbidden. Any new copy
path must be documented here and covered by a test that explains why it is
required.
