# Zdash source contract

## Overview

GTK telemetry dashboard with OAuth authentication, WebSocket subscriptions, and an installed module-session ABI.

## Interface

- Installed module ABI exposes `Module::run(ModuleSession&)`, `FactoryFn`, and lookup symbol `ZdashModule`; ModuleHost/ModuleSession expose thread-specific requests, GTK callbacks, widgets, and session control. [zdash_module.hh:Module] [zdash_module.hh:ModuleHost] [zdash_module.hh:ModuleSession]
- OAuth configuration supplies issuer/client identity, scope, callback port, timeout, and loopback testing; loadConfig reads and parses a bounded configuration file. [zdash_oauth.hh:Config] [zdash_oauth.hh:loadConfig]
- Subscription frames use Ztc envelopes; reply acceptance matches subscription, source generation, publisher and group. Source containers hold application, alert, heap/hash/thread/queue, connection, engine, and database telemetry. [zdash.cc:requestFrame] [zdash.cc:accepts] [zdash.cc:Containers]
- GTK Model/View/Details expose hierarchical telemetry keys and printable detail fields, with grouping for heaps, sockets, links, and databases. [zdash.cc:Model] [zdash.cc:View] [zdash.cc:Details]
- `zdash` accepts config, WebSocket URL, device ID, CA, browser-suppression, and help options; normal sessions use an OAuth lease, while ZDASH_TEST loads a module factory. The build installs zdash, zdash_module.hh, and zdash.glade. [zdash.cc:main] [zdash.cc:Options] [Makefile.am:bin_PROGRAMS] [Makefile.am:dist_pkgdata_DATA]

## Invariants

- Posted ModuleRun work must not outlive its session; closing follows Rx/refresh drain, closed means actual window destruction, and SourceView identity lasts until retirement. [zdash_module.hh:ModuleRun] [zdash_module.hh:ModuleSession] [zdash_module.hh:SourceView]
- OAuth endpoints require HTTPS except explicit localhost test mode. Discovery validates issuer and code/refresh/S256 support; callbacks require matching state and unique recognized query keys. [zdash_oauth.hh:endpointURL] [zdash_oauth.hh:metadataJSON] [zdash_oauth.hh:CallbackApp]
- Lease token borrows vault credential storage until refresh; token JSON requires nonempty access token, positive expiry, and Bearer type. Refresh requires rotation and persistence. [zdash_oauth.hh:Lease] [zdash_oauth.hh:tokenJSON] [zdash_oauth.hh:run]
- Source strings outlive their borrowed application key spans and rows; row/item links are raw pointers. Details guards cached iterators with a GtkTreeRowReference and borrows its model. [zdash.cc:Item__] [zdash.cc:Row] [zdash.cc:Details]
- Frames are capped at 1 MiB; TelHdr aligns queued payloads for 64-bit scalars without crossing threads with a connection pointer. [zdash.cc:FrameMax] [zdash.cc:TelHdr]
- Rx owns assembly/subscriptions/routes; GTK owns sources/models. Main waits on semaphore handoffs, and transmit formatting runs synchronously on Rx before owned output transfers to Tx. [zdash.cc:session] [zdash.cc:send]
- Binary messages are bounded and verified before queueing; acceptance checks source/subscription identity. Counters increment before ring publication, and GTK consumes borrowed ring bytes before releasing them. [zdash.cc:receive_] [zdash.cc:accepts] [zdash.cc:processTelemetry_] [zdash.cc:gtkRefresh_]
- Routes replace only older generations and cancel retired detail subscriptions. GTK removes rows before their source storage. [zdash.cc:discover_] [zdash.cc:retire_] [zdash.cc:removeSource_]

## Side Effects

- OAuth starts multiplex/logging threads, performs HTTP discovery/token exchanges, loads/saves vault credentials, opens a browser unless suppressed, and binds the callback listener on loopback. Request/response guards clear secret-bearing mutable buffers. [zdash_oauth.hh:run] [zdash_oauth.hh:openBrowser] [zdash_oauth.hh:Request] [zdash_oauth.hh:HTTPData]
- Views create GTK columns, retain icons, and install callbacks; selection/refresh emits row insert/delete/change signals and clears retired selection. [zdash.cc:View] [zdash.cc:Details]
- Connections send inventory/detail subscriptions, deliver module callbacks, and wake main on failure. Renewal closes/drains transport while preserving GTK/client state. Shutdown stops transport, closes GTK admission, drains callbacks, destroys models before sources, and closes ring storage. [zdash.cc:connected] [zdash.cc:renew] [zdash.cc:final] [zdash.cc:gtkFinal_]

## Verification

With GTK enabled in the configured build, run `make -j8 && make -C zdash test` from the repository root. Zdash is excluded from the default top-level SUBDIRS, so the explicit module test invocation builds and verifies it. The module target includes unit and integration suites. Commands were not executed for this documentation change. [../Makefile.am:test]
