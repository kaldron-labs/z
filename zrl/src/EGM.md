# Zrl source contract

## Overview

Terminal input/output, readline and CLI lifecycle, keymap-driven editing, filesystem completion, and in-memory history.

## Interface

- C `Zrl_readline/start/stop/running` and C++ namespace wrappers provide singleton line input. `App` supplies editing, signal, completion and indexed-history callbacks; `Config` controls editor limits. [Zrl.hh:Zrl_readline] [ZrlApp.hh:App] [ZrlConfig.hh:Config]
- `CLI` owns a scheduler-backed editor lifecycle; `Terminal` exposes scheduled lifecycle, virtual-key input, password input, line splice/redraw and output. [ZrlCLI.hh:CLI] [ZrlTerminal.hh:Terminal]
- `Editor` is a command-sequence VM with keymaps, scheduler-thread lifecycle, password input, printing and binding dumps. `Globber` provides App-compatible filesystem completion; `History` supplies bounded in-memory indexed history callbacks. [ZrlEditor.hh:Editor] [ZrlGlobber.hh:Globber] [ZrlHistory.hh:History]
- `Line` owns UTF-8 bytes and byte/display-position indexes, exposing glyph/word motion, searching, substrings, and reflow. Completion/history use application callbacks. [ZrlLine.hh:Line] [ZrlEditor.cc:initComplete] [ZrlEditor.cc:histLoad]
- `libZrl.la` installs public headers and Emacs/Vi keymaps and links Zi, Zf, Ze, Zt, Zm, Zu, and platform terminal libraries. [Makefile.am:lib_LTLIBRARIES] [Makefile.am:dist_pkgdata_DATA]

## Invariants

- Successful readline results are malloc-allocated NUL-terminated text for caller free; callback spans borrow editor storage and must be copied for retention. [Zrl.cc:process] [ZrlApp.hh:CompSpliceFn]
- Terminal retains a raw scheduler pointer, dispatches to its configured thread, and requires line mutations through splice. CLI completion synchronization uses file-static done/openOK shared across instances. [ZrlTerminal.hh:open] [ZrlTerminal.hh:splice] [ZrlCLI.cc:done] [ZrlCLI.cc:openOK]
- Editor is noncopyable/nonmovable; loadMap requires initialization, map selection belongs on the terminal thread, and getpass is pre-start or terminal-thread-only. [ZrlEditor.hh:Editor] [ZrlEditor.hh:loadMap] [ZrlEditor.hh:getpass]
- Key dispatch resolves exact key, literal equivalent, then wildcard; synthetic redispatch is bounded. Movement respects prompt and glyph/word boundaries. Map mode indices supplied by callers are trusted. [ZrlEditor.cc:process] [ZrlEditor.cc:motion] [ZrlEditor.cc:cmdMode]
- Line reflow requires an offset within stored bytes and terminal width of at least two; wide glyphs remain intact through padding. Editor history callbacks capture stack references and are used synchronously. [ZrlLine.cc:reflow] [ZrlEditor.cc:searchFwd]
- Terminal cursor positions count display cells including full-width glyphs. isOpen/running marshal synchronous queries; stop queues work, clears the scheduler wake callback and wakes platform I/O. [ZrlTerminal.cc:outNoWrap] [ZrlTerminal.cc:isOpen] [ZrlTerminal.cc:stop]

## Side Effects

- CLI reads ZRL_MAPS/ZRL_MAP/ZRL_MAPID, loads keymaps, and reports load exceptions. Readline installs globbing/history callbacks and can raise platform signals. [ZrlCLI.cc:init] [Zrl.cc:Context]
- Globber scans filesystem completions and splices quoted paths into the line. History copies nonempty saves and synchronously invokes load callbacks over stored bytes. Editor print schedules terminal work and redraws afterward. [ZrlGlobber.cc:init] [ZrlGlobber.cc:subst] [ZrlHistory.cc:save] [ZrlHistory.cc:load] [ZrlEditor.hh:print]
- Loading keymaps reads bounded files and can throw on I/O/parse failures. Editing records undo and updates terminal cursor/text/registers. Enter saves history, calls the application, clears the line, and prompts again; end callbacks can occur both in a stopping handler and dispatch. [ZrlEditor.cc:loadMap] [ZrlEditor.cc:splice] [ZrlEditor.cc:cmdEnter] [ZrlEditor.cc:process__]
- Undo/redo, insertion, paste, transforms and completion mutate terminal content through splices; completion listing writes rows and redraws. History replacement saves drafts when requested and resets undo/motion state. [ZrlEditor.cc:cmdUndo] [ZrlEditor.cc:edit] [ZrlEditor.cc:histLoad]
- Linux terminal opening creates tty/epoll/wake-pipe resources and changes SIGWINCH handling; start/stop changes/restores termios. Windows paths manage console handles, modes and codepages. Password input temporarily disables echo and restores input modes. [ZrlTerminal.cc:open_] [ZrlTerminal.cc:start_] [ZrlTerminal.cc:stop_] [ZrlTerminal.cc:close_] [ZrlTerminal.cc:getpass]
- Terminal splice replaces bytes, reflows display mappings, renders affected rows and updates the cursor. Rendering appends output but does not itself flush it; clear resets line/cursor while cls also emits screen clearing. [ZrlTerminal.cc:splice] [ZrlTerminal.cc:clear] [ZrlTerminal.cc:cls] [ZrlTerminal.cc:redraw]

## Verification

From the configured repository root, run `make -j8 && make -C zrl test`. Commands were not executed for this documentation change. [../Makefile.am:test]
