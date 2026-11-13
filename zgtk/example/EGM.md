# ZGtk demonstration

## Overview

GTK demonstration with sortable rows, right-click callbacks and drag/drop.

## Interface

- `zgtkdemo` is a non-installed program with no parsed options; it loads `zgtkdemo.glade` from the working directory. [Makefile.am:noinst_PROGRAMS] [zgtkdemo.cc:main] [zgtkdemo.cc:start]

## Invariants

- The ten-row model uses persistent iterators; accepted paths have depth one and indices 0–9. Sorting reverses row order. [zgtkdemo.cc:TreeModel::get_iter] [zgtkdemo.cc:TreeModel::sort]
- Cell, column, and view references are held together and released in reverse order; the model reference is released after attachment to the view. [zgtkdemo.cc:start]
- The application attaches to scheduler thread 1. Window destruction, load failure, or SIGINT releases the main wait before scheduler stop and application detach. [zgtkdemo.cc:main]

## Side Effects

- Opens a GTK window, writes `zgtkdemo.log`, and prints click/drop values. [zgtkdemo.cc:main] [zgtkdemo.cc:start]

## Verification

With GTK enabled, run `make -j8 && make -C zgtk test` from the configured repository root. The demo requires a graphical environment and its Glade file; it was not executed.
