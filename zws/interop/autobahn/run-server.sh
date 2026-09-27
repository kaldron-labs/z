#!/bin/sh

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
image=${AUTOBAHN_IMAGE:-crossbario/autobahn-testsuite}
. "$here/../../../zi/itest/zi-test-residue.sh"
zi_residue_init "zwsautobahnserver"
trap 'status=$?; zi_residue_finish "$status"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
sed 's#"outdir": "./reports/[^"]*"#"outdir": "/reports/server"#' \
  "$here/fuzzingclient.json" >"$ZI_RESIDUE_DIR/fuzzingclient.json"

docker run --rm --add-host=host.docker.internal:host-gateway \
  -v "$here:/config:ro" -v "$ZI_RESIDUE_DIR:/reports" \
  --user "$(id -u):$(id -g)" -w /config "$image" \
  wstest -m fuzzingclient -s /reports/fuzzingclient.json
