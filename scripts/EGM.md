# Repository scripts

## Overview

Build and run QUIC interoperability images, collect and report results, convert TOML, and maintain development inputs.

## Interface

- `qir-build-image` builds through `make zhttp-qir-image` or an explicit staged `--root`; `--tag`, `--platform`, `--push`, and build/configuration switches control the image operation. [qir-build-image:usage]
- `qir-run-local --runner RUNNER --image IMAGE` exercises each selected peer with the local implementation in both client and server roles. [qir-run-local:usage] [qir-run-local:csv_words]
- `qir-collect-results` copies runner artifacts into `--out/--name`; `qir-report` writes a Markdown result matrix and failure-artifact listing. [qir-collect-results:usage] [qir-report:case_result]
- `qir-upload-image --source SOURCE --target TARGET` tags and publishes an image. [qir-upload-image:run]
- `toml-to-json INPUT` parses TOML using `tomllib` or `tomli` and writes JSON to stdout. [toml-to-json:json.dump]
- `gtags_hook.pl` lists regular, non-symlink source/schema files; `mingw_fixm4.sh` updates configure/m4 inputs with a sed substitution. [gtags_hook.pl:wanted] [mingw_fixm4.sh:sed]

## Invariants

- Image builds require the `/usr/local` prefix and reject a dirty worktree unless explicitly allowed. A staged root must contain executable `zhttpqir`, `qir_endpoint.sh`, and `usr/local/lib`. [qir-build-image:--prefix] [qir-build-image:--root] [qir-build-image:ZHTTP_QIR_ALLOW_DIRTY]
- The runner must contain `run.py` and `implementations_quic.json`; comma-separated peer/test lists cannot contain empty items. Registration is backed up and restored on exit and handled signals. [qir-run-local:validate_csv] [qir-run-local:restore_registration]
- Report results require exactly one matching JSON testcase; absent, malformed, or ambiguous results become unknown. Successful dry-run rows without JSON become planned; status 127 maps to unsupported. [qir-report:case_result] [qir-report:run_result]

## Side Effects

- Image construction can reconfigure, clean, build, stage libraries, and create Docker images. `--push` publishes in the staged-root buildx branch. [qir-build-image:--root] [qir-build-image:--push]
- Runner execution temporarily edits registration, writes compatibility code and result artifacts, and removes prior logs/results unless `--keep-logs`. Even `--dry-run` writes files and temporarily changes registration. [qir-run-local:--dry-run] [qir-run-local:--keep-logs] [qir-run-local:restore_registration]
- Container cleanup invokes `docker compose down -v --remove-orphans` and `docker rm -f server client sim`. [qir-run-local:cleanup_runner_containers]
- Collection replaces artifact destinations; reporting replaces its output file; upload tags and pushes images; the MinGW sed script edits inputs with `.bak` backups. [qir-collect-results:copy_if_exists] [qir-report:tmp] [qir-upload-image:run] [mingw_fixm4.sh:sed]

## Verification

From the repository root, syntax-check shell scripts with `for script in qir-build-image qir-run-local qir-collect-results qir-report qir-upload-image mingw_fixm4.sh; do sh -n "scripts/$script" || exit; done` and the tag hook with `perl -c scripts/gtags_hook.pl`. For configured interop build verification, run `make -j8 zhttp-qir-build`; the target is defined in `/Makefile.am`. These checks were not run for this documentation change. Image upload and runner execution have the side effects described above.
