# Zt

## Overview

Dynamic arrays and strings, bitmaps and windows, regex and text adapters, and timezone/platform utilities.

## Subdirectories & Boundaries

- [src](./src/EGM.md): installed library interfaces and implementation.

## Interface

- The [source contract](./src/EGM.md) covers `ZtArray`, `ZtString`, regex, formatting, windows, and platform utilities.

## Invariants

- Container ownership distinguishes copied, adopted, built-in, and shadow storage; regex captures borrow source bytes. [src/ZtArray.hh:own_] [src/ZtString.hh:shadow_] [src/ZtRegex.hh:capture]

## Side Effects

- Container mutation can allocate and invoke callbacks; regex operations manage PCRE resources. Timezone calculations lock and modify process timezone state. [src/ZtArray.hh:splice] [src/ZtRegex.cc:study] [src/ZtTimeZone.cc:Zt_TzGuard]

## Verification

From the configured repository root, run `make -j8 && make -C zt test`. Commands were not executed for this documentation change. [Makefile.am:test]
