# Rename Client Request Submission from `enqueue()` to `send()`

## Purpose and Ordering

This is a precursor to `plan2.md`.  Complete this rename first so the client
and server APIs use the same application-facing verb before the server-side
Parser/Builder boundary and per-link response TxQ are introduced.

The resulting terminology is:

```text
client.send(poolSlot, request)    -> selected Pool::send(request) -> TxQ
server Link::send(response)       -> that link's response TxQ
```

The client still names a pool slot because it must select a destination pool.
The future server call is link-local and therefore needs no slot or separate
link parameter.

Backward compatibility is not required.  Delete the old names rather than
adding forwarding overloads.

## 1. Rename `Client::enqueue()`

In `ZhttpClient.hh`, change:

```cpp
bool enqueue(unsigned slot, ZmRef<Request> request);
```

to:

```cpp
bool send(unsigned slot, ZmRef<Request> request);
```

Preserve its behavior:

- reject calls before `Client::start()`;
- reject null requests;
- reject an invalid or unconfigured pool slot;
- transfer the request reference to the selected Pool;
- return `true` once submission to the Pool has been accepted for asynchronous
  Tx-thread ingress.

Change its call to the Pool from:

```cpp
pool->enqueue(ZuMv(request));
```

to:

```cpp
pool->send(ZuMv(request));
```

`Client` has no existing `send()` member, so this rename adds no overload or
name-hiding issue at that level.

## 2. Rename `Pool::enqueue()` and Its Internal Helper

Change the application-facing Pool entry point from:

```cpp
void enqueue(ZmRef<Request> request);
```

to:

```cpp
void send(ZmRef<Request> request);
```

Keep it as the thread-safe ingress wrapper.  It posts the owning request
reference to the configured Tx thread and does not invoke request Builder
callbacks on the caller's thread.

Rename the current private helper:

```cpp
bool enqueue_(ZmRef<Request> request);
```

to:

```cpp
bool submit_(ZmRef<Request> request);
```

Do not name this helper `send_()`: `Pool` already implements the
`ZmPQTx` callback `send_(Request *, bool)`.  Although those signatures could be
overloaded, keeping queue ingress distinct from the TxQ drain callback makes
the ownership and thread boundary explicit.

The implementation should have this shape:

```cpp
void send(ZmRef<Request> request) {
  txRun_([this, request = ZuMv(request)]() mutable {
    (void)submit_(ZuMv(request));
  });
}

bool submit_(ZmRef<Request> request) {
  assertTx_();
  if (m_txIngressStopped || m_sealed || !request) return false;
  Tx::send(ZuMv(request));
  return true;
}
```

This preserves the existing asynchronous validation semantics: `Client::send`
can validate client/slot state synchronously, while Pool sealing or concurrent
Tx-ingress shutdown is resolved later on the Tx thread.

## 3. Make `ZmPQTx::send` Name Hiding Deliberate

`Pool` publicly derives from its configured `TxQ`.  `TxQ::Msg` is `Request`, so
the new:

```cpp
Pool::send(ZmRef<Request>)
```

has effectively the same signature as the inherited:

```cpp
TxQ::send(ZmRef<TxQ::Msg>)
```

This is legal C++ name hiding, not an ambiguous overload or redeclaration.
Make the boundary unambiguous:

- do not add `using Tx::send`;
- call the queue implementation only as `Tx::send(...)`;
- retain qualified `Tx::send()` calls which drive the queue;
- audit `Pool` for every unqualified `send(...)` expression so none can recurse
  into the application-facing wrapper accidentally;
- leave `send_(Request *, bool)` and `resend_(Request *, bool)` as the `ZmPQTx`
  drain callbacks.

Add or update the nearby `TxQ` comment to state that `Pool::send(Request)` is
thread-safe ingress and intentionally hides the inherited queue insertion
method; internal queue operations must be qualified with `Tx::`.

## 4. Update Repository Call Sites

Propagate the breaking rename through all repository-owned dependents:

- `zhttp/util/zhttp.cc`: change the unqualified inherited client call in
  `produce_()` to `send(0, ZuMv(request))`;
- `zhttp/test/ZhttpClientCancelTest.cc`: change every `app.enqueue(...)` to
  `app.send(...)`;
- `zhttp/itest/zhttpclientfallbacktest.cc`: change every client enqueue to
  `send(...)`;
- `zhttp/README.md`: update the public client example and surrounding wording;
- any additional call sites found by a repository-wide exact-symbol search.

Update `plan2.md` after the code rename so its client/server comparison refers
to `Client::send(slot, request)` rather than `Client::enqueue(slot, request)`.
The successor plan should then assume the new API and must not mention the old
name except when describing removed code.

Do not change unrelated queue/container methods named `enqueue`, and do not
perform a blind repository-wide textual substitution.

## 5. Preserve Semantics

This precursor is a naming and boundary-clarification change only.  Do not
change:

- request ownership or intrusive reference-counting;
- pool selection;
- Rx/Tx thread routing;
- TxQ ordering or acknowledgement behavior;
- cancellation, sealing, replay, redirect, or retry behavior;
- request Builder or response Parser callback threads;
- method return types or synchronous/asynchronous failure reporting.

No compatibility `enqueue()` forwarding functions are added to either
`Client` or `Pool`.

## 6. Verification

Perform these checks in order:

1. Search `ZhttpClient.hh` for unqualified `send(...)` calls in `Pool` and
   verify every intended base queue operation is written `Tx::send(...)`.
2. Search the `zhttp`, `zws`, and `zrest` trees for client API uses of
   `enqueue`; only unrelated data-structure terminology may remain.
3. Run the top-level `make -j8` because `ZhttpClient.hh` is a public library
   header used by multiple modules.
4. Run `make -C zhttp/test -j8 && make -C zhttp/test test`, covering both
   `ZhttpClientCancelTest` and `ZhttpClientPoolTest` (which share the same
   source).
5. Run `make -C zhttp/util -j8` so the inherited call in `zhttp.cc` is
   compiled.
6. Run `make -C zhttp/itest -j8 && make -C zhttp/itest test`, including
   `zhttpclientfallbacktest`.

The existing cancellation and fallback suites provide the behavioral coverage
for submission, multiple pools, sealing, cancellation, retries, and transport
fallback.  No new behavioral test binary is needed solely for the rename, but
add a compile-time or focused unit assertion if name hiding makes overload
resolution unclear during implementation.

## Acceptance Criteria

- `Client::send(unsigned, ZmRef<Request>)` is the public coordinator API.
- `Pool::send(ZmRef<Request>)` is the public/thread-safe Pool ingress API.
- `Pool::submit_()` is the Tx-thread ingress helper.
- `Pool::send` intentionally hides `TxQ::send`; every base queue call is
  explicitly qualified as `Tx::send`.
- The old `Client::enqueue`, `Pool::enqueue`, and `Pool::enqueue_` names are
  absent.
- All repository-owned callers and documentation use `send`.
- No forwarding compatibility API remains.
- Build, unit-test, utility, and fallback-integration verification passes.
- `plan2.md` uses the new client naming before its implementation begins.
