# ZGtk

## Overview

GTK scheduling, value/callback adapters, and tree models.

## Subdirectories & Boundaries

- [src](./src/EGM.md): library contracts.
- [example](./example/EGM.md): interactive demonstration.

## Interface

`ZGtk::App`, `Value`, and tree-model templates integrate GTK with framework scheduling and data. [src/ZGtkApp.hh:ZGtk::App] [src/ZGtkTreeModel.hh:TreeModel]

## Invariants

Application and scheduler lifetimes span attachment and detachment completion; model ownership rules are detailed in the source contract. [src/ZGtkApp.hh:App::detach] [src/ZGtkTreeModel.hh:TreeHierarchy::Parent]

## Side Effects

Initializes GTK, installs event sources, dispatches scheduler work, and emits model notifications. [src/ZGtkApp.cc:App::attach_] [src/ZGtkTreeModel.hh:TreeArray::add]

## Verification

From the configured repository root, run `make -j8 && make -C zgtk/test test` with GTK enabled. Commands were not executed for this documentation change.
