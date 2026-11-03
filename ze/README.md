# Ze: errors, events and exception vocabulary

Ze represents native operating-system errors and structured diagnostic events.
It supplies severity, timestamp, source location, component identity and message
formatting for exceptions and logs. Include `<zlib/ZePlatform.hh>` and link
`libZe`. Actual asynchronous logging and sinks belong to
[ZiLog](../zi/README.md#logging), not a separate Ze logging runtime.

## Native errors

`ZeError` stores a native error code and formats it through the platform's
error-message facility. Default construction means success; `errNo()` preserves
the numeric code and `message()` supplies readable text.

```cpp
#include <errno.h>
#include <zlib/ZePlatform.hh>

ZeError invalid{EINVAL};
if (invalid) {
  ZtString<> message;
  message << "operation failed: " << invalid;
}
```

Capture `ZeLastError` immediately after a failed OS call, or `ZeLastSockError`
for sockets. On Windows these distinguish `GetLastError()` from
`WSAGetLastError()`; on Unix both use `errno`. Resolver `EAI_*` errors are
handled by the platform conversion path. Do not fetch the last error after
formatting or cleanup calls that may overwrite it. Copy the numeric wrapper
if it must survive an asynchronous handoff.

## Structured events

The severity vocabulary is `Ze::Debug`, `Info`, `Warning`, `Error`, and `Fatal`.
`ZeEventInfo` carries event metadata; `ZeEvent` combines it with a message
formatter. `ZeEVENT` captures file, line and function at the call site:

```cpp
#include <zlib/ZePlatform.hh>

auto event = ZeEVENT(Warning, "orders", "queue nearing capacity");
```

Messages may be literals, printable objects or stream-writing lambdas. For
values that outlive the call, capture owned data by value rather than retaining
references to temporary text. The template and type-erased event forms are
specified in [ZePlatform.hh](src/ZePlatform.hh).

## Exceptions

`ZeEXCEPT` constructs a `ZeException` with the same metadata and formatting
model. It constructs the value; use `throw` explicitly when propagation is
wanted:

```cpp
throw ZeEXCEPT(Error, "orders", "invalid order state");
```

Catch `const ZeException &` to inspect/format locally, or a mutable reference
when transferring it to `ZiLogEvent(ZuMv(e))`. `ZeMkEvent` and `ZeMkException`
are the explicit forms for wrappers that supply their own source metadata.
Severity alone is not a request to terminate the process.

## Passing events to logging

Applications that need sinks include `<zlib/ZiLog.hh>` and initialize/start
`ZiLog`. `ZiLOG(Error, "orders", error)` emits an event through that runtime;
`ZiLogEvent` forwards an existing event/exception. See the
[Zi logging guide](../zi/README.md#logging) for setup and shutdown. Keeping
error construction here and sink execution in Zi lets lower layers describe
failures without owning a logger thread.

## Examples and tests

Run `make -C ze test` from a configured build tree. The [test directory](test/)
contains executable API examples and boundary cases. See the top-level
[testing guide](docs/TESTING.md) for environment, diagnostics and port allocations,
and [dependencies](docs/DEPENDENCIES.md) for build prerequisites.
