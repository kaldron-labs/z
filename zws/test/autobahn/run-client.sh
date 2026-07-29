#!/bin/sh

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
image=${AUTOBAHN_IMAGE:-crossbario/autobahn-testsuite}

docker run --rm -p 9001:9001 -v "$here:/config" -w /config "$image" \
  wstest -m fuzzingserver -s fuzzingserver.json
