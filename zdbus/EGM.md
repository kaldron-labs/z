# Zdbus

## Overview

Linux D-Bus integration with typed message adaptation and explicit scheduler ownership.

## Subdirectories & Boundaries

- [src](./src/EGM.md): client/server transport, envelopes, and typed adapters.
- [example](./example/EGM.md): session-bus GetId demonstration.
- [util](./util/EGM.md): private daemon launcher and external-libdbus reference peer.

## Interface

`ZdbusConnection`, `ZdbusClient`, and `ZdbusServer` supply transport and lifecycle; builder/parser aliases and typed catalogs adapt application schemas. [src/ZdbusConnection.hh:Connection] [src/ZdbusClient.hh:Client] [src/ZdbusServer.hh:Server] [src/ZdbusAdapter.hh:BuildAdapter]

## Invariants

Stop completion precedes finalization and destruction. Rx/Tx ownership and retained-buffer requirements are specified by the source contract. Local subscription registration and bus match installation are distinct. [src/ZdbusConnection.cc:Connection::final] [src/ZdbusEnvelope.hh:FrameInfo] [src/ZdbusClient.cc:Client::rxSubscribe_] [src/ZdbusClient.cc:Client::addMatch]

## Side Effects

Library operations connect Unix sockets, post scheduler work, invoke callbacks, and exchange D-Bus traffic. Utilities can spawn a daemon or connect a libdbus reference peer. [src/ZdbusConnection.cc:Connection::connect_] [util/zdbusbus.cc:main] [util/zdbuspeer.cc:connect]

## Verification

On a configured Linux build, run `make -j8 && make -C zdbus test` from the repository root. Unit, integration, and interop targets are defined by the module Makefile. No builds or tests were executed for this documentation change.
