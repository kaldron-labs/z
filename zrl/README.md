# Zrl: terminal editing and interactive command lines

Zrl provides a terminal abstraction, editable lines, key maps, history and
completion, with both a blocking readline-style API and a callback-driven CLI.
Include `<zlib/Zrl.hh>` for the simple interface or the individual editor/CLI
headers for integration, and link `libZrl` with its dependencies.

## Blocking line input

```cpp
#include <stdio.h>
#include <stdlib.h>
#include <zlib/Zrl.hh>

int main() {
  while (auto line = Zrl::readline("app> ")) {
    puts(line);
    ::free(line);
  }
}
```

The returned line is caller-owned and released with `free()`; a null result
ends the loop. `Zrl::start`, `stop` and `running` expose optional lifecycle
control. C-linkage entry points use the `Zrl_` prefix. The complete minimal
program is [example/zrl.cc](example/zrl.cc).

## Callback-driven CLI

`Zrl::CLI` combines terminal input and editor behavior through `Zrl::App`
callbacks. Initialize it with prompt, enter, error and signal handlers:

```cpp
#include <iostream>
#include <zlib/ZrlCLI.hh>

Zrl::CLI cli;
cli.init(Zrl::App{
  .error = [](ZuCSpan message) { std::cerr << message << '\n'; },
  .prompt = [](Zrl::Prompt &prompt) { if (!prompt) prompt = "app> "; },
  .enter = [](ZuCSpan line) -> bool { return line == "quit"; }
});
```

This is a setup fragment; the lifecycle is `open()`, `start()`, `join()`,
`stop()`, `close()`, `final()`, checking `open()` before starting. Returning
`true` from the example's enter callback requests termination. Input views
are borrowed; copy text needed by work that outlives the callback. See
[example/zrlcli.cc](example/zrlcli.cc) for signal handling and full teardown.

## History and completion

`Zrl::History` provides bounded history. Pass its `saveFn()` and `loadFn()` as
`histSave` and `histLoad` callbacks. `Zrl::Globber` supplies filesystem completion
through `initFn()`, `startFn()`, `substFn()` and `nextFn()`, assigned to the
corresponding `comp*` callbacks. Keep the history/globber objects alive for the
CLI's entire callback lifetime.

Applications can replace these callbacks with domain-specific commands,
identifiers or persistent history. The callback signatures in
[ZrlApp.hh](src/ZrlApp.hh) define the integration boundary; the CLI example
shows the standard history/globber wiring.

## Editor and key maps

`Zrl::Editor` handles editing commands, movement, search, undo/redo, completion
and history navigation. `ZrlConfig.hh` exposes limits such as `maxLineLen`,
`maxCompPages`, `maxUndo`, key-sequence timing and mode-stack depth.
[ZrlEmacs.map](src/ZrlEmacs.map) and [ZrlVi.map](src/ZrlVi.map) supply editing
maps; `dumpVKeys()` and `dumpMaps()` help inspect active bindings.

Use [example/zrleditor.cc](example/zrleditor.cc) when embedding the editor
without the complete CLI. `Zrl::Line` tracks editable line contents and display
positioning; [line tests](test/ZrlLineTest.cc) cover its behavior.

## Terminal facilities

`Zrl::Terminal` handles the terminal-facing layer and `ZrlTerminfo` provides
capability access. Start with [example/zrlterminal.cc](example/zrlterminal.cc)
for direct terminal integration. These facilities expect a terminal environment;
line-editor programs should be run interactively rather than treated as a pipe
parser. Release terminal resources during shutdown so modes are restored.

## Examples and tests

Run `make -C zrl test` from a configured build tree. The [test directory](test/)
contains executable API examples and boundary cases. See the top-level
[testing guide](docs/TESTING.md) for environment, diagnostics and port allocations,
and [dependencies](docs/DEPENDENCIES.md) for build prerequisites.
