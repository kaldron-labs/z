#!/bin/sh
set -e
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:4096 -out ZtlsServer.key
openssl req -new -x509 -key ZtlsServer.key -subj "/CN=localhost/O=org/C=US" \
  -days 36500 -out ZtlsServer.crt
