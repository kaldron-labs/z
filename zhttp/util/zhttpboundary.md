# Zhttp executable boundary manifest

This manifest covers the complete program-only source closure:
`zhttp.cc` plus `zhttpput.hh` for `zhttp`, and `zhttpd.cc` plus
`zhttpdapp.cc`, `zhttpdutil.cc`, `zhttpd.hh`, and `zhttpput.hh` for `zhttpd`.
All reusable HTTP mechanism is provided by installed `Zhttp` headers.

## `zhttp.cc`

| Category | Top-level declarations | Application responsibility |
| --- | --- | --- |
| CLI parsing | `Http3Mode`, timeout defaults, `Options`, `usage`, `parseDrop`, `validateOptions` | Declare, parse, validate, and document command-line workload and configuration choices. |
| Protocol-specific configuration | `Http2Mode`, `Http3Mode`, `parseMigrationLocal`, `migrationMode`, `migrationConfigured`, `migrationOnOpen`, `quicHeartbeat`, `mxParams` | Convert CLI values into multiplex, TLS, and QUIC configuration. Authority syntax is parsed by the shared `ZhttpURL` facility. |
| Workload selection | `RequestHeaders`, `ResponseHeaders`, `URL`, `MaxRedirects`, `RespBodyMax`, `Req`, `State`, `initReq`, `ClientCallbacks` | Describe submitted GET and typed-JSON PUT requests and implement the single protocol-neutral response callback contract. |
| Workload-specific request/response handling | `redirectStatus`, `resetResponse` | Interpret response status for output policy and reset application response state after a library-managed redirect. |
| Output-file handling | `HdrString`, `outputPath`, `closeBody`, `truncateOutputPath` | Select, open, truncate, write, and close response output files. |
| Reporting | `printMemDiag`, `hotLog`, `ReqLogCtx`, `reqLogCtx`, `reqLogPrefix`, `logFraming`, `logConnected_`, `logConnected` | Format application, framing, connection, memory, hash, and heap diagnostics. |
| CLI parsing | `main` | Wire logging and multiplex setup, construct public protocol configuration, submit the selected workload to `Zhttp::Client`, report completion, and select exit status. |

`ClientCallbacks` owns only application request intent and response
consumption. It delegates output operations and reporting to the separately
classified helpers above; it owns no routing, attempt, transport, parser,
builder, pool, redirect, fallback, or lifecycle mechanism.

## `zhttpput.hh`

| Category | Top-level declarations | Application responsibility |
| --- | --- | --- |
| Workload-specific request/response handling | `String`, `Record`, `Record::JSON`, `equals`, `load` | Define the shared typed JSON PUT record and its `ZfJSON` encode/decode validation. |

## `zhttpdapp.cc`

| Category | Top-level declarations | Application responsibility |
| --- | --- | --- |
| CLI parsing | `usage`, `loadOptions` | Validate and document command-line choices. |
| Protocol-specific configuration | `parseDrop`, `migrationMode`, `quicHeartbeat`, `mxParams`, `ReqBodyMax`, `ReqHeaders`, `RespHeaders`, `Service` | Convert CLI values into multiplex, TCP, TLS, QUIC, and public service configuration. |
| Workload-specific request/response handling | `sendBody`, `Workload` | Adapt the static planner's response plan to the single protocol-neutral service callback contract. |
| Reporting | `printMemDiag` | Format memory, hash, and heap diagnostics. |
| Workload selection | `prepareProcess` | Apply the selected foreground/daemon process policy. |
| CLI parsing | `Zhttpd::run` | Wire logging, workload state, public `Zhttp::Service`, and process exit status. |

`Workload::listening`, `Workload::listenFailed`, `Workload::connected`, and
`Workload::disconnected` are reporting callbacks; the `Workload` type does
not own listener state or lifecycle decisions.

## `zhttpd.cc`

| Category | Top-level declarations | Application responsibility |
| --- | --- | --- |
| CLI parsing | `main` | Forward command-line arguments to `Zhttpd::run`. |

## `zhttpdutil.cc`

| Category | Top-level declarations | Application responsibility |
| --- | --- | --- |
| Protocol-specific configuration | `ZtEnumImplNS(Http2Mode)` | Materialize the static server's CLI HTTP/2 mode mapping out of line. |

## `zhttpd.hh`

| Category | Top-level declarations | Application responsibility |
| --- | --- | --- |
| CLI parsing | `CLI`, `Http2Mode`, `Forward`, `Options`, `parseForward`, `parseAuth`, `loadOptions`, `validate` | Define and validate static-server workload options. |
| Workload selection | `FileChunk`, `MimeFileMax`, `DateBufSize`, `DirEntriesBuiltin`, `DirNameBuiltin`, `HdrString`, `PathOffsets`, `State`, `initFileState` | Own bounded static-workload configuration and state. |
| Workload-specific request/response handling | `ResponsePlan`, `MimeMap`, `StaticPlanner`, `isspace__`, `lower__`, `lower`, `ieq`, `httpDate`, `parseHTTPDate`, `pathComponent`, `decodeNormalizePath`, `staticPath`, `htmlEsc`, `hostName`, `constTimeEqual`, `basicAuthValue` | Plan authorization, redirects, normalized static paths, MIME, conditional and range responses, directory listings, and response metadata. URL and request-target splitting is reusable Zhttp mechanism. |
| Output-file handling | `fileChunks`, `sendSpanChunks` | Bound response-file and generated-body chunks presented to the public service body sink. |
| Reporting | `LogSink` | Format and emit workload access records. |
| CLI parsing | `run` declaration | Expose the executable entry point to application tests. |

The automated `ZhttpBoundaryTest` rejects native transport headers, internal
link calls, parser/builder selection, HPACK/H2 frame/session/wire code,
application-local ALPN manipulation, discovery/cache/pool machinery, blocking
coordination, old borrowed-span body callbacks, and program-local
hub/runtime coordinators in this closure.
It also derives executable sources from `Makefile.am` and quoted local-header
edges from the reviewed files, rejecting any unreviewed addition to the
program-only closure.
