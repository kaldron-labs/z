#!/bin/sh

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
image=${AUTOBAHN_IMAGE:-crossbario/autobahn-testsuite}

docker run --rm --add-host=host.docker.internal:host-gateway \
  -v "$here:/config" -w /config "$image" \
  wstest -m fuzzingclient -s fuzzingclient.json
