# Ze source contract

## Overview

Native error wrappers and structured diagnostic event construction and formatting.

## Interface

- `ZeError`, `ZeOK`, `ZeLastError`, and `ZeLastSockError` wrap platform error codes and expose error messages. [ZePlatform.hh:ZeError]
- `Ze::severity`, `Ze::strerror`, `Ze::file`, and `Ze::function` format severity, native errors, and source metadata. [ZePlatform.cc:Ze::severity]
- `ZeEventInfo` records timestamp, thread ID, source location, component, and severity; `ZeEvent<L>`, `ZeAnyEvent`, and `ZeException` carry printable message callables. [ZePlatform.hh:ZeEventInfo] [ZePlatform.hh:ZeEvent]
- `ZeMkEvent`, `ZeMkException`, `ZeEVENT`, and `ZeEXCEPT` construct events or exceptions; the macros capture source location. [ZePlatform.hh:ZeEVENT] [ZePlatform.hh:ZeEXCEPT]
- `ZeString`, `ZeLogBuf`, and `ZeMsgFn` supply event string storage and message delegates. [ZePlatform.hh:ZeString] [ZePlatform.hh:ZeLogBuf] [ZePlatform.hh:ZeMsgFn]
- Build target `libZe.la` installs `ZeLib.hh` and `ZePlatform.hh`, linking Zt, Zm, and Zu. [Makefile.am:lib_LTLIBRARIES]

## Invariants

- `ZeError` defaults to the platform success code; logical negation tests equality with `Ze::OK()`. [ZePlatform.hh:ZeError]
- Generic `ZeEvent<L>` is move-only; the type-erased `ZeEvent<ZeMsgFn>` supports copying. [ZePlatform.hh:ZeEvent]
- Event metadata stores caller-supplied file, function, and component as `ZuCSpan`; character-array message adaptation retains a character pointer. [ZePlatform.hh:ZeEventInfo] [ZePlatform.hh:ZeMsg_::fn]

## Side Effects

- Metadata capture reads the clock and current thread ID. [ZePlatform.hh:ZeEventInfo::ZeEventInfo]
- Printing executes message callables and writes to the destination stream. Extracting `fn()` moves the stored callable into the returned delegate, including through a const event. [ZePlatform.hh:ZeEvent::fn_] [ZePlatform.hh:ZeAnyEvent::print]
- Windows native-error formatting lazily allocates a Winsock lookup table and reuses thread-specific conversion buffers. [ZePlatform.cc:ZePlatform_WSAErrors] [ZePlatform.cc:Ze::strerror]

## Verification

From the configured repository build root, run `make -j8`, then `make -C ze test`. The module test target is defined in `ze/Makefile.am`. These commands were not run for this documentation change.
