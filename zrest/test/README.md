# zrest unit tests

Build with `make -C zrest/test -j8`, then run `make -C zrest/test test`.
`ZrestExampleTest` covers the OAuth wire schema, typed paths, and PKCE
issuance and validation without starting network services.
`ZrestTest` covers catalog composition, fixed and streamed body codecs,
borrowed input, incremental SSE reception, and completion-token arbitration.

`signatureBodyTest` and `signatureQueryTest` exercise `REST::Signature` with
HMAC-SHA256 hooks for JSON/URI requests and responses and URI queries. They
check independent known MAC values, exclusion of a populated stale signature
from normal Tx, unchanged source objects, ordinary Rx loading and application
verification against the unsigned wire bytes. Altered payloads, altered
signatures and missing signatures fail verification. Signing-disabled cases
also suppress the marked field. The existing `signedBodyTest` checks that
`prefixBody` bytes are excluded from the span passed to both body signing hooks.
