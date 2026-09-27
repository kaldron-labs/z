#!/bin/sh

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
image=${AUTOBAHN_IMAGE:-crossbario/autobahn-testsuite}
. "$here/../../../zi/itest/zi-test-residue.sh"
zi_residue_init "zwsautobahnclient"
trap 'status=$?; zi_residue_finish "$status"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
sed 's#"outdir": "./reports/[^"]*"#"outdir": "/reports/client"#' \
  "$here/fuzzingserver.json" >"$ZI_RESIDUE_DIR/fuzzingserver.json"

docker run --rm -p 9001:9001 -v "$here:/config:ro" -v "$ZI_RESIDUE_DIR:/reports" \
  --user "$(id -u):$(id -g)" -w /config "$image" \
  wstest -m fuzzingserver -s /reports/fuzzingserver.json
