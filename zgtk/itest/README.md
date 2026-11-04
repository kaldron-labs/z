# GTK integration tests

Run `make -C zgtk/itest test` after the default build. The fixture uses an
isolated Xvfb display and the real GTK/GLib event loop.

`zgtkstartuptest` queues work during attachment before the GLib wake handler
is installed. A second job published by that work must progress without an
external GUI event. Semaphores establish queue order; timeouts are watchdogs.
The old installer entered an idle GTK loop ahead of the pending job, losing
its earlier scheduler wake. Install the wake handler before queuing that loop.
Both the successful and failing paths detach and stop through real callbacks.

The regression allocates no network transport and changes no production state.
