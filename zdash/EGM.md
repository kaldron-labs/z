# Zdash

## Overview

GTK telemetry dashboard with OAuth authentication, WebSocket subscriptions, and an installed module-session ABI.

## Subdirectories & Boundaries

- [src](./src/EGM.md): application, OAuth workflow, and module interface.

## Interface

- The dashboard consumes Ztc telemetry over WebSocket and presents GTK source/detail models; modules implement the installed session ABI. [src/zdash.cc:App] [src/zdash_module.hh:Module]

## Invariants

- Rx owns subscriptions and assembly, GTK owns models and sources, and queued payloads carry identity without connection pointers. Source retirement removes rows before storage. [src/zdash.cc:session] [src/zdash.cc:TelHdr] [src/zdash.cc:removeSource_]

## Side Effects

- Authentication uses HTTP, a loopback callback listener, browser launch, and credential-vault persistence; the session starts workers and opens GTK windows and network connections. Shutdown drains transport and GTK callbacks before freeing state. [src/zdash_oauth.hh:run] [src/zdash.cc:final]

## Verification

With GTK enabled in the configured build, run `make -j8 && make -C zdash test` from the repository root. Zdash is excluded from the default top-level SUBDIRS, so the explicit module test invocation builds and verifies it. Commands were not executed for this documentation change. [Makefile.am:test]
