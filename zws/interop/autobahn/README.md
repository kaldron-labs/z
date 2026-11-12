# Autobahn WebSocket tests

These tests are opt-in and require Docker and Python 3.11+ (or Python with Tomli).
The checked-in TOML configurations are rendered as JSON in the owned report
directory before invoking Autobahn.

To test the Zws server, start the plain H1 echo example from the build tree:

```sh
../../src/zwsd --address=0.0.0.0 --port=9001
```

Then run:

```sh
./run-server.sh
```

To test the Zws client, start the Autobahn fuzzing server:

```sh
./run-client.sh
```

Then, from the build tree, run:

```sh
../zwsautobahnclient ws://127.0.0.1:9001
```

Each runner writes reports in an owned residue directory under `ZI_LOGDIR`
(or the working directory). A passing run removes its reports; a failed run
prints and retains the directory, with the newest eight failures kept.
Override `AUTOBAHN_IMAGE` to pin a particular testsuite image.

The transport hub owns its receive allocator. The suite assembles and echoes
complete messages.
