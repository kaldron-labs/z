# Autobahn WebSocket tests

These tests are opt-in and require Docker.

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

The client reserves a 1 MiB echo burst in each direction plus 128 control/
retirement slots (1198 Rx slots at 980 bytes, 192 Tx slots at 16 KiB).
Use `--rx-ring-size=N --tx-ring-size=N` to tune larger or concurrent runs;
valid counts are 2 through 32768. These bound pooled working sets, not message
length. The suite still assembles and echoes complete messages.
