# Zrl

## Overview

Terminal input/output, readline and CLI lifecycle, keymap-driven editing, filesystem completion, and in-memory history.

## Subdirectories & Boundaries

- [src](./src/EGM.md): library and installed keymaps.
- [example](./example/EGM.md): readline, CLI, editor and terminal examples.

## Interface

- C readline functions and C++ CLI/Editor/Terminal APIs accept application completion, history, and signal callbacks. [src/Zrl.hh:Zrl_readline] [src/ZrlApp.hh:App]

## Invariants

- Readline callers free successful allocated lines; callback spans borrow storage. Terminal operations obey scheduler-thread affinity and use splice for line mutation. [src/Zrl.cc:process] [src/ZrlTerminal.hh:splice]

## Side Effects

- Starts scheduler work, reads/writes terminal devices, changes terminal modes and signal handlers, reads keymaps and filesystem completions, and mutates in-memory history. Cleanup restores terminal settings and releases resources. [src/ZrlCLI.cc:init] [src/ZrlTerminal.cc:open_] [src/ZrlTerminal.cc:stop_] [src/ZrlGlobber.cc:init] [src/ZrlHistory.cc:save]

## Verification

From the configured repository root, run `make -j8 && make -C zrl test`. Commands were not executed for this documentation change. [Makefile.am:test]
