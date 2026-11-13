# Zu numeric benchmarks

## Overview

Formatting and interpolation-search timing programs.

## Interface

- `zuboxbench` compares six numeric formatting variants without arguments; `zusearchbench except|noexcept N` repeats interpolation searches using the selected comparison callback. [zuboxbench.cc:main] [zusearchbench.cc:usage]
- The manifest builds both programs as non-installed executables against libZu. [Makefile.am:noinst_PROGRAMS] [Makefile.am:LDADD]

## Invariants

- Formatting accuracy checks precede timing and abort on failure. [zuboxbench.cc:CHECK2] [zuboxbench.cc:fail2]
- Search uses a fixed 1000-element generated array and fixed target. Output reads `array[r]` and `array[r-1]` without boundary checks, assuming a valid interior result and useful iteration input. [zusearchbench.cc:main] [zusearchbench.cc:search_noexcept] [zusearchbench.cc:search_except]

## Side Effects

- Formatting uses CLOCK_REALTIME and prints timings. Search allocates with malloc and relies on process exit for reclamation. Invalid syntax exits one; caught exceptions print diagnostics. [zuboxbench.cc:main] [zusearchbench.cc:main] [zusearchbench.cc:usage]

## Verification

Run `make -j8 && make -C zu/test test` from the configured repository root for the separate unit suite, including ZuBoxTest and ZuSearchTest. The benchmark manifest has no test target. Benchmarks were not executed.
