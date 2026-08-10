# Zhttp executable boundary manifest

This manifest covers the complete program-only source closure:
`zhttp.cc` plus `zhttpput.hh` for `zhttp`, and `zhttpd.cc`,
`zhttpdutil.cc`, `zhttpd.hh`, and `zhttpput.hh` for `zhttpd`. Both programs
use the adjacent, non-installed `runtime.hh` semaphore bridge.
All reusable HTTP mechanism is provided by installed `Zhttp` headers.

## `zhttp.cc`

| Category | Top-level declarations | Application responsibility |
| --- | --- | --- |
| CLI parsing | `Http3Mode`, timeout defaults, `Options`, `usage`, `parseDrop`, `validateOptions` | Declare, parse, validate, and document command-line workload and configuration choices. |
| Protocol-specific configuration | `Http2Mode`, `Http3Mode`, `parseMigrationLocal`, `migrationMode`, `migrationConfigured`, `migrationOnOpen`, `quicHeartbeat`, `mxParams` | Convert CLI values into multiplex, TLS, and QUIC configuration. Authority syntax is parsed by the shared `ZhttpURL` facility. |
| Workload selection | `RequestHeaders`, `ResponseHeaders`, `URL`, `MaxRedirects`, `RespBodyMax`, `Request_`, `RequestQ`, `Request`, `TxQ`, `Client`, `initReq` | Describe GET and typed-JSON PUT requests, own the intrusive transmit queue, incrementally generate a bounded workload, and immediately retire responses without persistent archival. |
| Workload-specific request/response handling | `redirectStatus`, `resetResponse` | Interpret response status for output policy and reset application response state after a library-managed redirect. |
| Output-file handling | `HdrString`, `outputPath`, `closeBody`, `truncateOutputPath` | Select, open, truncate, write, and close response output files. |
| Reporting | `printMemDiag`, `hotLog`, `ReqLogCtx`, `reqLogCtx`, `reqLogPrefix`, `logFraming`, `logConnected_`, `logConnected` | Format application, framing, connection, memory, hash, and heap diagnostics. |
| CLI parsing | `main` | Wire logging and multiplex setup, construct public protocol configuration, start the bounded client workload, report completion, and select exit status. |

`Client` owns only application request intent, response consumption, the
Tx-owned request queue, incremental generation, and non-persistent archival.
It delegates output operations and reporting to the separately classified
helpers above; it owns no routing, attempt, transport, parser, pool, redirect,
fallback, or protocol lifecycle mechanism.

## `zhttpput.hh`

| Category | Top-level declarations | Application responsibility |
| --- | --- | --- |
| Workload-specific request/response handling | `String`, `Record`, `Record::JSON`, `equals`, `load` | Define the shared typed JSON PUT record and its `ZfJSON` encode/decode validation. |

## `zhttpd.cc`

| Category | Top-level declarations | Application responsibility |
| --- | --- | --- |
| CLI parsing | `usage`, `loadOptions` | Validate and document command-line choices. |
| Protocol-specific configuration | `parseDrop`, `migrationMode`, `quicHeartbeat`, `mxParams`, `ReqBodyMax`, `ReqHeaders`, `FixedRespHeaders`, `Server` | Convert CLI values into multiplex, TCP, TLS, QUIC, and public server configuration. |
| Workload-specific request/response handling | `Response_`, `ResponseQ`, `Parser`, `App` | Copy callback-scoped request data, preserve request order through the application WorkQ, plan responses, and submit intrusive response Builders with `Link::send()`. |
| Reporting | `printMemDiag` | Format memory, hash, and heap diagnostics. |
| Workload selection | `prepareProcess` | Apply the selected foreground/daemon process policy. |
| CLI parsing | `main` | Make trap registration, logging, workload state, public `Zhttp::Server`, wait policy, teardown, and process exit status explicit. |

`App::listening`, `App::listenFailed`, `App::connected`, and
`App::disconnected` are reporting callbacks; the `App` type does
not own listener state or lifecycle decisions.

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
| Output-file handling | `fileChunks`, `sendSpanChunks` | Bound response-file and generated-body chunks presented to the public server body sink. |
| Reporting | `LogSink` | Format and emit workload access records. |
| Workload selection | `Application` | Share static-file workload/configuration mechanics with QIR without owning signal, wait, logging, or process lifecycle policy. |

## `runtime.hh`

| Category | Top-level declarations | Application responsibility |
| --- | --- | --- |
| Process mechanics | `ZhttpUtil::Runtime` | Provide only the local semaphore post/wait bridge; each `main` visibly owns trap registration, timeout/diagnostic policy, and teardown. |

The automated `ZhttpBoundaryTest` rejects native transport headers, internal
link calls, parser/builder selection, HPACK/H2 frame/session/wire code,
application-local ALPN manipulation, discovery/cache/pool machinery, blocking
coordination, old borrowed-span body callbacks, and program-local
hub/runtime coordinators in this closure.
It also derives executable sources from `Makefile.am` and quoted local-header
edges from the reviewed files, rejecting any unreviewed addition to the
program-only closure.
