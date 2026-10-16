# HTTP Clients with Multiple Pools of Links

the goal of this work is to update the `Client` application model so that pools (and links) are associated with specific servers (i.e. host + port).

## Immediate Requirements

- `Pool` will be disentangled from `Client`
- `Client` will own an array of `Pool`s, indexed by slot
- host+port will be determined by which pool requests are submitted to
- individual requests will continue to vary path etc.
- the app will no longer submit requests for arbitrary host+port,
  instead the app will:
  - initially specify that pools should be pre-created with:
    - a specific destination host+port (i.e. one pool per destination host)
    - a specific protocol policy (e.g. H3 forced, etc.)
    - a specific number of links
    - the app will specify the number of pools and the slot to occupy for each pool
      - i.e. the slot is controlled by the app, not allocated by the `Client`
  - the app submits requests to a specific pool slot, not to the `Client` as a whole
    - individual requests do not specify the host, port or protocol
- the `Client` will:
  - instantiate multiple pools, and route to them using the passed slot
  - explicitly control concurrency per pool (number of links per pool) (the `-j` option)
  - cache and re-use the hostname and protocol resolution for a pool
    - i.e. persistently associate a pool with a specific IP+port and protocol
      - H1/TCP, H1/TLS, H2 or QUIC
  - re-use established connections to servers
    - send successive requests on the same TCP/TLS/QUIC connection
  - Note: plain `http` continues to imply H1/TCP, while `https` continues to resolve dynamically to one of H1/TLS, H2 or QUIC
  - Note: `zhttp.cc` will continue to just create a single pool at slot 0, but other applications will be able to create multiple pools
- rate-limiting
  - while H2 and H3 have their protocol-level flow control etc., applications may need to apply rate limits and therefore need a `Client::limited(unsigned pool, unsigned link, bool value)` (`pool` and `link` being slots, `value` being whether or not the link is rate-limited by the application and should therefore not receive further requests for the time being); generally links can be "saturated" (protocol-level flow control), "limited" (application-level rate limiting), both or neither

## Future Requirements

The above work will provide the foundation for potential future requirements.

These capabilities are out of scope for now, but the work should enable them to be added subsequently:
- **keepalive** - use a keepalive mechanism to establish connections initially before any app request and keep them connected, to reduce the latency of the first request
- learn the prevailing per-link per-operation response time
  - this would be done by maintaining an exponentially-decaying running mean and standard deviation of RTT
  - slow outliers can be intentionally reconnected
    - this would reduce overall latency because production servers are typically clustered
    - clusters are often k8s or similar
    - connections are routed to pods via an intervening LB
    - some pods run faster than others due to varying node workloads and path-specific network congestion
    - such inter-pod variations make it worth reconnecting a slow connection to try and get connected to a faster pod
