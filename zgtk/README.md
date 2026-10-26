# ZGtk: GTK integration

ZGtk connects GTK 3's main context and object interfaces to Z scheduling and
C++ application types. Its facilities cover scheduler integration, signal
callbacks, `GValue` handling and custom tree models. Include individual headers
such as `<zlib/ZGtkApp.hh>` and link `libZGtk` with GTK and framework dependencies.
Enable GTK when configuring (do not pass `z.config -G`).

## Application scheduling

`ZGtk::App` attaches the GTK main loop to a selected `ZmScheduler` destination:

```cpp
#include <zlib/ZGtkApp.hh>

// scheduler is an existing, running ZmScheduler.
ZGtk::App app;
app.attach(&scheduler, 1);
app.run([] {
  gtk_init(nullptr, nullptr);
  // Construct widgets and connect signals on the GTK thread.
});
```

`run()` queues work to that destination; `invoke()` may execute immediately
when already there. Keep widget/model mutation on the GTK thread and post
owned snapshots from I/O or database workers. `i18n(domain, dataDir)` configures
the localization domain. `detach(callback)` releases the integration
asynchronously; retain the app and scheduler until that callback has run.

[example/zgtkdemo.cc](example/zgtkdemo.cc) shows scheduler attachment, a Builder
UI and model setup. Its `.glade` file is loaded relative to the working
directory, so run the demo from `zgtk/example` after building.

## Signal callbacks

`ZGtk::callback(lambda)` adapts a suitable callable to `GCallback` for GTK
signal registration:

```cpp
#include <zlib/ZGtkCallback.hh>

g_signal_connect(button, "clicked", ZGtk::callback(
  [](GtkButton *, gpointer context) {
    // Recover application state from context.
  }), context);
```

This fragment assumes existing `button` and `context` values. Use a stateless
lambda with the signal's exact ABI and pass state through GTK's user-data
argument. The adapter does not allocate a closure to keep arbitrary captures
alive. Disconnect signals or otherwise guarantee that context objects outlive
all callbacks.

## GValue handling

`ZGtk::Value` derives from `GValue`, initializes its type and unsets it on
destruction. Use the getter/setter matching the active `GType`:

```cpp
#include <zlib/ZuLib.hh>
#include <zlib/ZGtkValue.hh>

ZGtk::Value value{G_TYPE_INT};
value.set_int(42);
auto answer = value.get_int();
value.unset();
```

Default construction leaves the value uninitialized; call `init(type)` before
setting it. Unset an existing value before initializing another type. Treat
these as managed GLib values rather than copying their underlying bytes.
[ZGtkValueTest](test/ZGtkValueTest.cc) exercises the basic lifecycle.

## Custom tree models

`ZGtkTreeModel.hh` adapts C++ implementations to GTK's tree-model interfaces;
`ZGtk::TreeSortable<Impl, N>` adds sorting support. Implement the GTK-facing
methods for column types, iterator/path conversion, values, traversal and node
references. The demo's `TreeModel` is a flat sortable list with application
iterator data fitted into `GtkTreeIter` storage.

Keep the declared column `GType` consistent with each returned `Value`. Emit
the appropriate GTK row-inserted/deleted/changed/reordered notifications when
the backing data changes; implementing a C++ container alone does not notify
views. Iterator persistence flags must match the lifetime guarantees of your
model. Use the [model header](src/ZGtkTreeModel.hh) and
[demo](example/zgtkdemo.cc) as the interface reference.

## Examples and tests

Run `make -C zgtk test` from a configured build tree. The [test directory](test/)
contains executable API examples and boundary cases. See the top-level
[testing guide](../TESTING.md) for environment, diagnostics and port allocations,
and [dependencies](../DEPENDENCIES.md) for build prerequisites.
