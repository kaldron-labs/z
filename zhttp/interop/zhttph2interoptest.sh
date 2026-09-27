#!/bin/sh

set -eu

matrix=${ZHTTP_MATRIX:-./zhttpmatrix}
zhttp=${ZHTTP_CLIENT:-./zhttp}
zhttpd=${ZHTTP_SERVER:-./zhttpd}
port=${ZHTTP_H2_PORT:-18443}
nghttpd_port=${ZHTTP_H2_NGHTTPD_PORT:-18444}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$here/../../zi/itest/zi-test-residue.sh"
if test -n "${ZHTTP_H2_ARTIFACTS:-}"; then
  ZI_LOGDIR=$ZHTTP_H2_ARTIFACTS
  export ZI_LOGDIR
fi
zi_residue_init zhttp-h2-interop
work=$ZI_RESIDUE_DIR
failed=0
server_pid=
nghttpd_pid=

cleanup()
{
  status=$?
  if test -n "$server_pid"; then
    kill -TERM "$server_pid" 2>/dev/null || :
    wait "$server_pid" 2>/dev/null || :
  fi
  if test -n "$nghttpd_pid"; then
    kill -TERM "$nghttpd_pid" 2>/dev/null || :
    wait "$nghttpd_pid" 2>/dev/null || :
  fi
  if test "$failed" -ne 0; then status=$failed; fi
  zi_residue_finish "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir "$work/www"
printf 'zhttp-h2-interop\n' >"$work/www/index.html"
{
  clang++ --version 2>/dev/null | sed -n '1p'
  curl --version 2>/dev/null | sed -n '1p'
  caddy version 2>/dev/null || :
  h2spec --version 2>/dev/null || :
  nghttp --version 2>/dev/null || :
  nghttpd --version 2>/dev/null || :
} >"$work/tools.txt"

echo 'TAP version 14'
echo '1..7'

if test -x "$matrix" && test -x "$zhttp" && test -x "$zhttpd" &&
    command -v openssl >/dev/null &&
    command -v curl >/dev/null; then
  echo 'ok 1 - core offline interoperability tools'
else
  echo 'not ok 1 - core offline interoperability tools'
  failed=1
fi

if "$matrix" --case=zhttp-zhttpd/h2-tls/j5n10 \
    >"$work/zhttp-zhttpd.tap" 2>&1; then
  echo 'ok 2 - Zhttp client to Zhttp server over H2'
else
  echo 'not ok 2 - Zhttp client to Zhttp server over H2'
  failed=1
fi

if "$matrix" --case=curl-zhttpd/h2-tls/j5n10 \
    >"$work/curl-zhttpd.tap" 2>&1; then
  echo 'ok 3 - curl client to Zhttp server over H2'
else
  echo 'not ok 3 - curl client to Zhttp server over H2'
  failed=1
fi

if ! command -v caddy >/dev/null; then
  echo 'ok 4 - Zhttp client to Caddy server over H2 # SKIP Caddy is not installed'
elif "$matrix" --case=zhttp-caddy/h2-tls/j5n10 \
    >"$work/zhttp-caddy.tap" 2>&1; then
  echo 'ok 4 - Zhttp client to Caddy server over H2'
else
  echo 'not ok 4 - Zhttp client to Caddy server over H2'
  failed=1
fi

openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -subj /CN=localhost \
  -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' \
  -keyout "$work/key.pem" -out "$work/cert.pem" \
  >"$work/openssl.out" 2>"$work/openssl.err"

"$zhttpd" "$work/www" --https --http2=force --addr 127.0.0.1 \
  --port "$port" --cert "$work/cert.pem" --key "$work/key.pem" \
  --no-server-id >"$work/zhttpd.out" 2>"$work/zhttpd.err" &
server_pid=$!
ready=0
for n in 1 2 3 4 5 6 7 8 9 10; do
  if curl --http2 --cacert "$work/cert.pem" --fail --silent \
      --show-error --connect-timeout 1 --max-time 2 \
      "https://localhost:$port/index.html" >"$work/ready.body" 2>/dev/null; then
    ready=1
    break
  fi
done

if ! command -v h2spec >/dev/null; then
  echo 'ok 5 - h2spec against Zhttp server # SKIP h2spec is not installed'
elif test "$ready" -eq 1 &&
    h2spec --host localhost --port "$port" --tls --insecure \
      >"$work/h2spec.out" 2>"$work/h2spec.err"; then
  echo 'ok 5 - h2spec against Zhttp server'
else
  echo 'not ok 5 - h2spec against Zhttp server'
  failed=1
fi

if ! command -v nghttp >/dev/null; then
  echo 'ok 6 - nghttp client to Zhttp server # SKIP nghttp is not installed'
elif test "$ready" -eq 1 &&
    nghttp --no-verify-peer -nv "https://localhost:$port/index.html" \
      >"$work/nghttp.out" 2>"$work/nghttp.err"; then
  echo 'ok 6 - nghttp client to Zhttp server'
else
  echo 'not ok 6 - nghttp client to Zhttp server'
  failed=1
fi

kill -TERM "$server_pid" 2>/dev/null || :
wait "$server_pid" 2>/dev/null || :
server_pid=

if ! command -v nghttpd >/dev/null; then
  echo 'ok 7 - Zhttp client to nghttpd server # SKIP nghttpd is not installed'
else
  nghttpd -d "$work/www" "$nghttpd_port" "$work/key.pem" "$work/cert.pem" \
    >"$work/nghttpd.out" 2>"$work/nghttpd.err" &
  nghttpd_pid=$!
  ready=0
  for n in 1 2 3 4 5 6 7 8 9 10; do
    if curl --http2 --cacert "$work/cert.pem" --fail --silent \
	--show-error --connect-timeout 1 --max-time 2 \
	"https://localhost:$nghttpd_port/index.html" \
	>"$work/nghttpd-ready.body" 2>/dev/null; then
      ready=1
      break
    fi
  done
  if test "$ready" -eq 1 &&
      "$zhttp" --http3=disable --http2=force -c "$work/cert.pem" \
	-o "$work/zhttp-nghttpd.body" \
	"https://localhost:$nghttpd_port/index.html" \
	>"$work/zhttp-nghttpd.out" 2>"$work/zhttp-nghttpd.err" &&
      cmp "$work/www/index.html" "$work/zhttp-nghttpd.body"; then
    echo 'ok 7 - Zhttp client to nghttpd server'
  else
    echo 'not ok 7 - Zhttp client to nghttpd server'
    failed=1
  fi
fi

exit "$failed"
