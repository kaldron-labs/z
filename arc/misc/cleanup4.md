`zhttpmatrix` client teardown diagnosis
=======================================

`zhttpmatrix` launches `zhttp`; the likely lifetime bug is in
`zhttp/test/zhttp.cc`, where stack-owned client objects can be finalized and
destroyed while live links or posted callbacks still hold raw `App *`
back-pointers into that client.

The clearest bad path is H1 pooled timeout:

1. `runH1Pool()` starts timeout teardown.
2. It calls `disconnect()` on each link.
3. It waits at most 2 seconds and ignores whether all workers actually stopped.
4. It copies counts, calls `client.final()`, returns, and destroys the stack
   client.

Those links use raw owner back-pointers, which is consistent with the local
I/O style, but the owner must drain or cancel dependent work before teardown.
`Ztcp::Client`, `Ztls::Client`, and `Zquic::Client` all expose the common
`ZmEngine` start/stop contract: `stop()` blocks synchronously when called
without a continuation callback. `zhttp` should use that common client-level
drain instead of manually iterating over links in the app.

The H3 multi path is better but still fragile:

1. `runH3Multi()` creates `QUICClient client` on the stack.
2. It creates a live `QUICClient::Link` and stores it in `client.link`.
3. Teardown calls `abortDrained()` or `disconnectDrained()`, then
   `waitDisconnect(mx)`.
4. It clears `client.link`, calls `client.final()`, and lets the stack client
   die.

`Zquic::CliLink::disconnect()` is multi-stage: it posts to Rx, may send close
work on Tx, returns to Rx, disconnects the endpoint, and still schedules
follow-on callback work. A helper that waits for only the low-level close
callback plus a fixed barrier sequence is not a strong ownership proof. The
client should remain alive until the application-level `disconnected()` path
has run and both scheduler shards have drained after that point.

Suggested fix
-------------

1. Make `zhttp` use `Ztcp::Client`, `Ztls::Client`, and `Zquic::Client`
   consistently as engines:
   - add the missing `ZmEngine` lifecycle to `Ztcp::Engine` and
     `Ztls::Engine`, not just their `Client` specializations, so both
     `Client` and `Server` benefit from the same start/stop/final sequencing;
   - after `init()`, start the client engine before connecting/scheduling
     links;
   - before `final()` or stack destruction, call synchronous `client.stop()`;
   - call `client.final()` only after `stop()` has returned.

2. For H1 pooled clients, replace the ignored 2-second timeout wait with the
   common engine drain:
   - set `client.stopping = true` so app callbacks do not schedule more work;
   - request stop at the client level with synchronous `client.stop()`, with
     `H1PoolClient::stop_()` owning worker disconnect/drain behind that common
     engine API;
   - only then copy counts, call `client.final()`, and let `client` destruct.

3. For H3 multi, use the same client-level stop rule:
   - set `client.stopping = true`;
   - on timeout/stall, record the abort/error intent in the app state, then
     drive shutdown through `client.stop()` rather than bespoke link iteration;
   - on normal completion, call synchronous `client.stop()` before dropping the
     last link reference;
   - clear `client.link`, call `client.final()`, and then leave scope.

4. Delete or sharply reduce bespoke drain helpers such as `disconnectDrained()`,
   `abortDrained()`, and fixed barrier sequences where `client.stop()` provides
   the required lifecycle boundary. If a helper remains for protocol-specific
   close intent, it should be subordinate to `client.stop()`, not a substitute
   for it.

5. Keep raw back-pointers from links to clients. Do not fix this by refcounting
   the client from short-lived links; the repo style expects raw dependent
   back-pointers and explicit owner teardown.

Verification target
-------------------

Run focused matrix cases after the change:

```
libtool exec ./zhttp/test/zhttpmatrix --case=zhttp-zhttpd/h3/j5n10 --timeout=10 --stall-timeout=5 --quiet-timeout=0 --debug
libtool exec ./zhttp/test/zhttpmatrix --case=zhttp-zhttpd/h1-tcp/j5n10 --timeout=10 --debug
libtool exec ./zhttp/test/zhttpmatrix --case=zhttp-zhttpd/h1-tls/j5n10 --timeout=10 --debug
```

Also run the same cases with intentionally short timeouts to exercise the
teardown paths, because that is where the current lifetime bug is most exposed.
