# Terminal examples

## Overview

Readline, CLI, editor, and terminal examples.

## Interface

- `zrl` reads and echoes lines until readline returns null; `zrlcli` installs prompt, enter, signal, completion, and history callbacks and exits on `quit`. [zrl.cc:main] [zrlcli.cc:main]
- `zrleditor` supplies indexed history callbacks; `zrlterminal` prints virtual key names and exits on `q`. All four executables are non-installed. [zrleditor.cc:main] [zrlterminal.cc:process] [Makefile.am:noinst_PROGRAMS]

## Invariants

- Each successful readline allocation is freed after printing. [zrl.cc:main]
- CLI follows init/open/start/join/stop/close/final; failed open exits one. Editor completion releases its semaphore, and cleanup precedes scheduler stop. [zrlcli.cc:main] [zrleditor.cc:main]

## Side Effects

- Reads terminal input and prints prompts, text, diagnostics, or key maps. Editor history callbacks grow/update an array; terminal processing splices rendered keys into its line and writes it. [zrlcli.cc:main] [zrleditor.cc:main] [zrlterminal.cc:process]

## Verification

Run `make -j8 && make -C zrl/test test` from the configured repository root for the separate module suite. The examples build through their default target and have no automated test recipe. [Makefile.am:noinst_PROGRAMS] Interactive examples were not executed.
