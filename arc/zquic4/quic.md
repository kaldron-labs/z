- Connection lifecycle
    - Version negotiation
    - Handshake and TLS integration
    - Connection ID management
    - Address validation and migration
    - Shutdown and draining

- Packet processing
    - Packet number spaces
    - Packet encoding and decoding
    - Header protection
    - Payload encryption and decryption
    - Frame parsing and dispatch

- Streams
    - Stream creation
    - Stream state machines
    - Stream send buffering
    - Stream receive reassembly
    - Reset and stop-sending handling

- Flow control
    - Connection-level limits
    - Stream-level limits
    - Credit advertisement
    - Blocked-state detection
    - Limit update scheduling

- Congestion control
    - RTT estimation
    - Congestion window management
    - Pacing
    - Slow start
    - Loss recovery integration

- Retransmission
    - PTO
        - Probe timeout calculation
        - Probe packet generation
        - Exponential backoff
        - Anti-deadlock behavior

    - ACK
        - ACK frame generation
        - ACK delay handling
        - ACK range tracking
        - Loss detection from ACK gaps
        - RTT sampling from ACKs

- Loss recovery
    - Packet tracking
    - Time-threshold loss detection
    - Packet-threshold loss detection
    - In-flight byte accounting
    - Crypto-data recovery

- Frame scheduling
    - Control frame prioritization
    - Stream data scheduling
    - Retransmittable frame tracking
    - Path challenge/response scheduling
    - ACK-eliciting packet selection

- Path management
    - Path validation
    - PMTU discovery
    - NAT rebinding handling
    - Connection migration
    - Multi-path readiness, if supported

- Security
    - TLS key updates
    - Key phase handling
    - Stateless reset
    - Token generation and validation
    - Anti-amplification enforcement

- Datagram support
    - Unreliable DATAGRAM frame send
    - DATAGRAM receive dispatch
    - MTU-aware payload sizing
    - Application-level loss tolerance

- Application interface
    - Stream API
    - Datagram API
    - Event callbacks
    - Send/receive readiness
    - Error reporting

- Timers
    - ACK timer
    - PTO timer
    - Loss detection timer
    - Idle timeout
    - Key discard timer

- Instrumentation
    - qlog/event tracing
    - Packet counters
    - RTT/loss metrics
    - Congestion metrics
    - Error diagnostics
