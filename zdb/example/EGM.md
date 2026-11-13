# Database saga example

## Overview

In-memory two-shard account transfer using a four-phase saga.

## Interface

- Non-installed `zdbsaga` takes no arguments and transfers 125 between accounts initially holding 1000 and 250. [Makefile.am:noinst_PROGRAMS] [zdbsaga.cc:main] [zdbsaga.cc:run]
- `BalanceTransfer` inserts a pending transfer, debits, credits, then marks completion; the first three phases define compensation. [zdbsaga.cc:BalanceTransfer]

## Invariants

- Moved phase completions retain the definition and raw saga pointer across shard work; invoke completion after commit as the final use in the callback. [zdbsaga.cc:BalanceTransfer]
- Shard selection is `(id - 1) & 1`; IDs are keys, balances/status are mutable. [zdbsaga.cc:accountShard] [zdbsaga.cc:Account] [zdbsaga.cc:Transfer]
- Activation and completion are awaited before database stop, table-reference release, multiplexer stop, and database finalization. [zdbsaga.cc:run]

## Side Effects

- Starts six configured threads, mutates an in-memory store, prints final balances, and logs to stderr. [zdbsaga.cc:config] [zdbsaga.cc:run] [zdbsaga.cc:main]

## Verification

From the configured repository root, build with `make -j8`, then run `./zdb/example/zdbsaga`. Success requires Complete status and balances 875/375, with exit status zero. [zdbsaga.cc:transferComplete] [zdbsaga.cc:run] The example was not executed.
