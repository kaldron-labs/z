# ZGtk library

## Overview

GTK 3 integration with `ZmScheduler`, GValue and callback adapters, and CRTP models for flat and hierarchical data.

## Interface

- `ZGtk::App` exposes locale configuration, scheduler attachment, asynchronous detachment, and shard dispatch through `run`/`invoke`. [ZGtkApp.hh:ZGtk::App]
- `ZGtk::callback` adapts callables to `GCallback`; `ZGtk::Value` wraps GValue initialization, cleanup, scalar values, and GLib ownership-specific accessors. [ZGtkCallback.hh:ZGtk::callback] [ZGtkValue.hh:ZGtk::Value]
- `TreeModel<Impl>` registers GObjects implementing `GtkTreeModel` and `GtkTreeSortable`, delegates model operations, and provides click and same-application drag/drop adapters. `TreeSortable` tracks sorting state. [ZGtkTreeModel.hh:TreeModel] [ZGtkTreeModel.hh:TreeSortable]
- `TreeArray` supplies a flat model using application-provided `Iter`, `Data`, loading, formatting, and row operations. `TreeHierarchy` supplies row/child/leaf types, owning `Parent`, tuple-backed `Branch`, and model traversal/mutation notifications. [ZGtkTreeModel.hh:TreeArray] [ZGtkTreeModel.hh:TreeHierarchy]
- `libZGtk.la` installs the five public headers listed in `pkginclude_HEADERS` and links GTK and framework libraries. [Makefile.am:lib_LTLIBRARIES] [Makefile.am:pkginclude_HEADERS]

## Invariants

- The application and scheduler must outlive attachment and queued detachment completion: queued work and the wake callback retain raw pointers. There is no automatic `App` destructor teardown. [ZGtkApp.hh:App::detach] [ZGtkApp.cc:App::attach_] [ZGtkApp.cc:App::detach_]
- `Value` destruction releases initialized contents; static, take, and duplicate accessors preserve the corresponding GLib ownership choices. The class defines no custom copy or move operations. [ZGtkValue.hh:Value::~Value] [ZGtkValue.hh:Value::set_static_string] [ZGtkValue.hh:Value::take_variant]
- Model iterators must be POD and fit in `GtkTreeIter`. Both models advertise persistent iterators and have no-op node reference callbacks. Flat-model string rendering reuses a static buffer and assumes single-threaded use. [ZGtkTreeModel.hh:TreeArray] [ZGtkTreeModel.hh:TreeArray::get_value] [ZGtkTreeModel.hh:TreeHierarchy::Model]
- `Parent` owns and deletes child pointers; `Branch` indexes tuple-owned children. Leaf, parent, and branch objects prohibit copying and moving. [ZGtkTreeModel.hh:TreeHierarchy::Parent] [ZGtkTreeModel.hh:TreeHierarchy::Branch] [ZGtkTreeModel.hh:TreeHierarchy::Leaf]
- Constructed implementations occupy GObject-managed storage; the registered finalizer invokes the implementation destructor. [ZGtkTreeModel.hh:TreeModel::gtype_init] [ZGtkTreeModel.hh:TreeModel::ctor]

## Side Effects

- First attachment initializes GTK and optionally gettext. Windows attachment sets `GTK_CSD=0` and `GTK_THEME=win32`. [ZGtkApp.cc:App::attach_]
- Attachment installs a GSource and scheduler wake callback; dispatch enters/exits GTK loops, wakes the default context, and queues continuations. Detachment destroys the source. [ZGtkApp.cc:App::attach_] [ZGtkApp.cc:App::run_] [ZGtkApp.cc:App::detach_]
- Model construction registers GTypes; drag setup installs widget handlers. Click handling can change selection; drag/drop allocates and frees GTK paths/events and transfers a same-application list pointer. [ZGtkTreeModel.hh:TreeModel::gtype_init] [ZGtkTreeModel.hh:TreeModel::click] [ZGtkTreeModel.hh:TreeModel::drag] [ZGtkTreeModel.hh:TreeModel::drop_]
- Sorting and mutation emit GTK notifications. Hierarchical deletion updates storage before notification; flat-array deletion notifies before row removal. [ZGtkTreeModel.hh:TreeArray::del] [ZGtkTreeModel.hh:TreeHierarchy::Model::del]

## Verification

With GTK enabled in a configured repository build, run `make -j8 && make -C zgtk test` from the repository root. The module test target invokes unit and integration targets. Commands were checked against `zgtk/Makefile.am` but not executed for this documentation change.
