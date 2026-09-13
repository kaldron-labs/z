# zrest interoperability tests

`make -C zrest/itest test` runs `zrestmatrix`, which supervises the C++ and Go
clients and servers over verified localhost TLS. The Go tool and the pinned Go
module dependencies are required for the full matrix; without Go the driver
reports a TAP skip.

The example separates the OAuth roles even when one process hosts both server
roles:

```text
external user agent -> authorization server (`zrestd` or Go server)
native client       -> discovered token and revocation endpoints
native client       -> `/api/ping` resource server with a bearer token
```

The `ping` application has issuer `/oauth2/ping`. Both `zrest-native` and
`zrest-go` are public clients of that issuer and obtain tokens for the same
`/api/ping` audience. Discovery starts at
`/.well-known/oauth-authorization-server/oauth2/ping`; the advertised OAuth
endpoints are beneath `/oauth2/ping/v1/`. Authorization uses an external
headless user-agent fixture, an ephemeral loopback callback, authorization code
plus PKCE `S256`, and issuer validation. The native clients never receive the
demonstration user's password.

Build the complete `zrest` module first so `zrest/example` and
`zrest/interop/go` contain their peer executables. The matrix generates a
temporary certificate, uses isolated loopback ports, and applies the same
15-second failure deadline to every case.  It terminates child processes on
failure and retains temporary files only for failure diagnosis.  It covers both
C++/Go client-server directions, token expiry and refresh rotation, concurrent
and fractionally paced pings, revocation, and negative OAuth and bearer-token
cases. Its twelve cases also force HTTP/2 and HTTP/3, exercise raw HTTP/1.1
authority handling and transport-scheme rejection, prove isolation between two
active application registrations, and verify deterministic reclamation with
each in-memory state limit reduced to one.

For a manual C++ run, start `zrest/example/zrestd` with a localhost certificate,
then run `zrest/example/zrest` with the exact issuer and resource URLs, the CA
path, and a browser command. `zrest/itest/zrestua` is suitable as that command
for deterministic local testing.

This is an educational authorization server. Its in-memory users, grants,
tokens, sessions, and signing key are intentionally lost on restart; it is not
a deployable identity service.
