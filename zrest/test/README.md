# zrest unit tests

Build with `make -C zrest/test -j8`, then run `make -C zrest/test test`.
`ZrestExampleTest` covers the OAuth wire schema, typed paths, and PKCE
issuance and validation without starting network services.
`ZrestTest` covers catalog composition, fixed and streamed body codecs,
borrowed input, incremental SSE reception, and completion-token arbitration.
