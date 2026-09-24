# Ztls tests

`make -C ztls/test test` runs the TAP tests, including `ZtlsKEMTest`.

To exercise ML-KEM-768 PEM interoperability with the OpenSSL CLI in the
current build configuration, run from the repository root:

```sh
dir=$(mktemp -d)
./ztls/test/ZtlsKEMTest --save "$dir/z-private.pem" "$dir/z-public.pem"
openssl pkey -in "$dir/z-private.pem" -noout
openssl pkey -pubin -in "$dir/z-public.pem" -noout
openssl genpkey -algorithm ML-KEM-768 -out "$dir/openssl-private.pem"
openssl pkey -in "$dir/openssl-private.pem" -pubout -out "$dir/openssl-public.pem"
./ztls/test/ZtlsKEMTest --load "$dir/openssl-private.pem" "$dir/openssl-public.pem"
```

The temporary directory contains private-key material; remove it after the
interop check using the platform's appropriate secure workflow.
