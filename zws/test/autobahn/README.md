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
../ZwsAutobahnClient ws://127.0.0.1:9001
```

Reports are written below `reports/server` and `reports/client`.  Override
`AUTOBAHN_IMAGE` to pin a particular testsuite image.
