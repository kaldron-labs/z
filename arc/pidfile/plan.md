# ZiPIDFile implementation plan

## Outcome

Extract PID-file path resolution, acquisition, reading, and writing from
`ZiDaemon` into a reusable `ZiPIDFile` component. A PID file will be addressed
by a relative name beneath the platform runtime directory, and will remain
unreadable until the complete PID has been written. `ZiDaemon` will retain the
component through orderly teardown and all callers will pass names rather than
absolute paths.

## Design decisions

- Preserve the historical idempotent exclusive-create protocol using the
  existing `ZiFile` API: call `open()` with
  `WriteOnly | Create | Exclusive | NoFollow | GC` and mode `0`. The returned
  handle remains writable by its creator while independent readers fail to
  open the pathname. Write the complete PID through that same handle, then
  change its mode to the normalized `0644` equivalent as the final publication
  step. Do not create it initially readable or reopen it between creation and
  publication.
- Add only the missing general `ZiFile` operation needed by this sequence: a
  handle-based mode setter (following the repository's naming convention) that
  changes mode `0` to normalized `0644` without resolving the pathname again.
  Make `ZiFile`'s existing create-mode argument and the new setter enforce the
  equivalent deny-read/publish behavior on every supported platform. Do not add
  `ZiPIDFile`-specific flags, publication states, or portable error enums when
  ordinary `ZiFile::open()` success/failure is sufficient.
- When exclusive creation fails, attempt a normal read open as the historical
  algorithm does. If that open also fails because the creator has not made the
  file readable yet, sleep for one second and retry the complete
  exclusive-create/open-read sequence at the same fixed interval. Do not
  exponentially back off, parse partially written contents, or report the
  transient state as `Running`.
- Open/create one stable inode/file and never recover from a stale PID by
  unlinking and recreating it. Use no-follow semantics and verify that the
  opened object is a regular file before trusting or modifying it.
- Read and parse a published existing PID, then use the established platform
  process-liveness check. A live PID returns `Running`; a PID that does not name
  a live process is stale and the same file is overwritten. Put the stale file
  back to mode `0` through its open handle before truncating it, write the
  complete decimal PID from offset zero, and change to normalized `0644` only
  after the full write succeeds. This also fixes the existing trailing-byte bug
  when a shorter PID replaces a longer one.
- Keep `ZiPIDFile` platform-neutral: use only `ZiFile` operations for exclusive
  mode-`0` creation, open/read, mode changes, seek/truncate/write, and removal.
  All OS-specific filesystem implementation belongs in `ZiFile.cc`;
  process-liveness implementation remains isolated in `ZiPIDFile.cc`.
- On orderly `ZiPIDFile` teardown, remove the PID file after verifying it is
  still the file/PID owned by this process. Do not require cleanup after a hard
  crash or abort; the next process handles the lingering file through the PID
  liveness check and stale overwrite path.
- Accept a relative PID-file name in the production API. Resolve its prefix in
  `ZiPIDFile` through a platform-neutral `ZiFile::tmpDir()` facility that returns
  the mainstream temporary/runtime directory writable by an unprivileged
  caller (`/tmp` on conventional Unix systems and the canonical system API
  result elsewhere). Do not use `/var/run` when it requires elevated
  privileges, and do not use the reboot-persistent `/var/tmp` for ephemeral PID
  state. Accept validated relative subpaths, create missing intervening
  directories on demand with normalized mode `0755`, and allow those
  directories to linger. Provide a public explicit-directory overload/seam so
  tests can exercise the identical logic under `ZiTestResidue` without
  modifying the host temporary directory.

## Implementation steps

1. Extend `zi/src/ZiFile.{hh,cc}` with a handle-based mode setter so an already
   open file can move between mode `0` and normalized `0644`. Ensure the
   existing `open(..., mode)` argument and this setter provide equivalent access
   semantics on every supported platform; no new PID-specific flag or error
   classification is needed. Document that a mode-`0` file cannot be opened by
   an independent reader but remains accessible through its creator's existing
   handle. Add direct cross-platform coverage to `zi/test/ZiFileTest.cc`. Also
   add `ZiFile::tmpDir()` for the platform's
   canonical unprivileged temporary/runtime base and, if existing primitives
   cannot safely express it, a recursive directory-creation operation that
   validates each component without following links/reparse points.

2. Add `zi/src/ZiPIDFile.hh` and `zi/src/ZiPIDFile.cc` following the local header
   skeleton, naming, modelines, export annotations, and hard-tab layout.
   Define the result contract (`OK`, `Error`, `Running`), the resolved path and
   last-error accessors needed by callers/logging, and ownership of the
   `ZiFile` during initialization plus the resolved path/current PID needed for
   identity-safe orderly removal. Ensure every failure path closes its handle.
   Do not include native filesystem headers or native error/flag handling in
   this component.

3. Implement path construction in `ZiPIDFile.cc`. Start from
   `ZiFile::tmpDir()`, parse the supplied relative subpath component by
   component, reject absolute/rooted names plus empty, `.` and `..` components,
   and create each missing intervening directory on demand. Treat an
   already-existing component as success only after verifying through `ZiFile`
   that it is a real directory and not a link/reparse point. Leave created
   directories in place and create them with normalized mode `0755`. Expose the
   resolved PID path for precise errors and add focused cases for canonical base
   selection, separator normalization, nested creation, repeat use of lingering
   directories, hostile intermediate components, and absolute/traversal
   rejection. Support a public explicit base-directory argument for tests and
   other intentional overrides; the default production path still uses
   `ZiFile::tmpDir()`.

4. Implement acquisition as a retrying `ZiFile` state machine. Attempt
   `open(path, WriteOnly | Create | Exclusive | NoFollow | GC, 0)` first. If it
   fails, attempt the normal existing-file open. If that also fails while the
   creator has the file at mode `0`, sleep for one second and restart at
   exclusive creation, using the same fixed interval without exponential
   backoff. Validate regular-file identity through `ZiFile` before reading or
   modifying an existing file.

5. For an existing published file, read and strictly parse the PID and perform
   the current process-liveness check. Return `Running` when it names a live
   process. Otherwise set mode `0` through the open stale-file handle, truncate
   and seek to zero, format `Zm::getPID()` into bounded stack storage, write all
   bytes through that handle, and set normalized mode `0644` only after the
   complete write returns. Do not call `sync()`/`fsync()` or require crash
   durability. Write the PID as bare decimal digits with no trailing newline.
   Use the same write/mode-change sequence after successful exclusive creation.
   If writing or the final mode change fails, retain no apparently readable
   partial PID file. Verify daemonization/re-invocation ordering so only the
   final process publishes its PID.

6. Refactor `zi/src/ZiDaemon.{hh,cc}`. Remove `ZiFile`, PID parsing, process
   probing, and platform liveness checks from `ZiDaemon.cc`; after Unix
   daemonization or Windows re-invocation, delegate PID-file acquisition to a
   process-lifetime `ZiPIDFile`. Map its result into the daemon-init result
   contract, and log failures/running-owner details using copied path/error/PID
   values that are safe for `ZiLOG`'s asynchronous execution. Its orderly
   destructor removes the PID file; abnormal termination may leave it stale.
   Preserve the current working-tree API change to `const Zi::Path &` where it
   remains appropriate, while making the argument semantics explicitly
   relative.

7. Register `ZiPIDFile.hh` in `pkginclude_HEADERS` and `ZiPIDFile.cc` in
   `libZi_la_SOURCES` in `zi/src/Makefile.am`; add only the direct includes each
   new or changed file requires.

8. Update every `ZiDaemon` dependent found in the tree:

   - `zhttp/util/zhttpd.{cc,hh}`: keep `--pidfile` as a name, remove any
     absolute-path assumptions/conversion, and change help/error wording from
     path to runtime-directory-relative filename.
   - `zrest/example/zrestd.cc` and its option metadata: make the same semantic
     and help-text change.
   - `zi/test/ZiDaemonTest.cc`: stop feeding temporary absolute paths through
     `ZiDaemon`; retain daemonization coverage while moving PID-file mechanics
     to direct `ZiPIDFile` tests using the explicit test directory.

9. Add or split out TAP tests under `zi/test` and register any new binary in
   `zi/test/Makefile.am`. Cover fresh creation, exact PID contents, overwrite of
   a stale file, truncation of a longer stale PID, malformed/empty
   stale content, exclusive-create versus existing-file branches, a PID naming
   a live process, stale overwrite followed by successful rereading,
   orderly removal, crash-style stale retention/recovery,
   invalid/non-regular/symlink targets, path-policy rejection, and surfaced I/O
   errors. Add a deterministic publication-race test that pauses one helper
   after exclusive mode-`0` creation but before the PID write/final mode change;
   verify through `ZiFile` that a contender cannot open or parse the file, takes
   the documented sleep/retry path, and succeeds or reports `Running` only after
   it becomes readable. Test the same API-level behavior on every supported
   platform without embedding native flags or error numbers in
   `ZiPIDFileTest`.
   Hold the publisher across more than one fixed one-second retry to verify the
   interval does not back off. Synchronize the final mode change/release with
   pipes or equivalent process primitives rather than relying on timing for
   completion. Confine any unavoidable platform-specific assertions to
   `ZiFileTest`. Assert bare-decimal contents, absence of a trailing newline,
   normalized `0755` for newly created intermediate directories, use of the
   explicit test base, and that publication does not invoke a durability sync.

## Verification

1. Rebuild the changed library before its tests:
   `make -C zi/src -j8` followed by `make -C zi/test -j8`.
2. Run the focused TAP binaries through the build tree (including the new
   `ZiPIDFileTest`, if split, and `ZiDaemonTest`) and then
   `make -C zi/test test`.
3. Rebuild the changed dependents with
   `make -C zhttp/util -j8` and `make -C zrest/example -j8`.
4. Run top-level `make -j8` and `make test` to catch exported-header,
   automake, and cross-module regressions.
5. Compile and run the PID-file tests on both Linux and MinGW/Windows, checking
   mode-`0` read denial, normalized mode transitions, runtime-prefix resolution,
   orderly removal, and warning-free builds on current gcc and clang.

## Clarified requirements

- Do not require advisory or mandatory file locking. Its semantics and support
  are not sufficiently consistent across the target platforms, and the PID-file
  protocol must not depend on it.
- Use exclusive unpublished creation followed by publication as the portable
  race-avoidance mechanism. When a published file already exists, read its PID:
  return `Running` if it identifies a live process; otherwise treat the contents
  as stale and overwrite the file through the same mode-`0`/write/mode-`0644`
  sequence.
- Remove the PID file during orderly `ZiPIDFile` teardown. A hard crash or abort
  may leave it behind; the next initialization uses PID liveness to recognize
  and overwrite the stale file.
- Do not introduce `Unpublished`, `publish()`, `unpublish()`, or PID-specific
  error APIs. Use the existing `ZiFile::open(..., mode = 0)` behavior and add
  only a general handle-based mode setter with the normalized cross-platform
  semantics required to switch between `0` and `0644`.
- Retry an unpublished file every one second using a fixed, non-exponential
  interval. PID publication is expected to complete quickly.
- Publish PID files as `0644`, interpreted through `ZiFile`'s normalized
  platform equivalent.
- Resolve PID files beneath the platform's canonical temporary/runtime
  directory writable by an unprivileged caller. Do not assume root and do not
  select `/var/run` if it is not generally writable; conventional Unix uses
  `/tmp`, while other systems use their mainstream temporary-directory API.
- Permit relative subpaths. Create missing intervening directories on demand,
  use normalized mode `0755`, leave them in place, and reject unsafe
  rooted/traversal/link components.
- The final mode change follows the completed write directly; do not call
  `sync()`/`fsync()` or otherwise require crash durability.
- Store the PID as bare decimal digits without a trailing newline.
- Expose an explicit base-directory argument for testing; ordinary callers use
  the canonical default returned by `ZiFile::tmpDir()`.

## Open questions and requirement ambiguities

None currently identified.
