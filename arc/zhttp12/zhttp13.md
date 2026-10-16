# Zhttp URL and request-target cleanup

This refactor gives HTTP locator and request-target syntax one owner, removes
the duplicated target splitting, and makes the distinction between a URL, a
URI reference, and an HTTP request-target explicit.  It is an intentionally
breaking cleanup: propagate API changes through every dependent application,
test, example, and document; do not add compatibility aliases or forwarding
overloads.

The phases are cumulative.  Each phase concludes with an acceptance record and
a `GUIDELINES.md` audit.  Keep breaking propagation within a phase when
practical.  If a phase boundary is intentionally left unbuildable because the
next phase immediately propagates the same break, mark it as an interim rather
than a validation checkpoint: do not create a compatibility path and do not
attempt its conditional build/test criteria.  Carry those criteria into the
next buildable phase.

## Terminology and ownership decisions

### Keep `URL`

Retain `Zhttp::URL` and `ZhttpURL.hh`.  The type represents an absolute
`http` or `https` locator with an origin and a path/query used to construct an
HTTP request.  It is not a scheme-generic RFC 3986 URI implementation:

- only `http` and `https` schemes are accepted;
- HTTP default ports and origin comparison are intrinsic behavior;
- userinfo is rejected;
- the value is consumed directly by the HTTP client and redirect machinery.

Calling this type `URI` would overstate its supported syntax and obscure its
HTTP-specific semantics.  Use *URI reference* only for the relative-or-
absolute input accepted by redirect resolution.  A dedicated owning
`URIReference` type is not needed unless a future caller needs to retain one
independently of an HTTP URL.

Because the supported scheme set is closed, store it as an enum rather than
an owned string:

```c++
ZtEnumNS(ZhttpAPI, Scheme, int8_t, http, https);

namespace Scheme {
  inline uint16_t defltPort(T v) {
    switch (v) {
      case http: return 80;
      case https: return 443;
      default: return 0;
    }
  }
}
```

Parsing performs case-insensitive string-to-enum conversion once.  Printing
performs enum-to-static-name conversion.  `URL::secure()` and origin
comparisons use the enum directly.  Do not retain a `URLString scheme` or
repeatedly compare it with `"http"` and `"https"`.

The scheme definition includes its default port (`80` for HTTP, `443` for
HTTPS), exposed as `Scheme::defltPort(T)`.  It does not contain the URL's
authority port.  `URL::port` and `Origin::port` hold the effective port after
applying an explicit authority port or the scheme default; `explicitPort`
records whether it appeared on the wire.  Origin equality and hashing always
use the effective port, so `http://host` and `http://host:80` have the same
origin even though complete-URL printing can preserve the explicit-port
distinction.

Retain the host as a span into the parse buffer, but lowercase its ASCII bytes
in place during parsing.  This is intentionally a destructive parse over a
mutable span, consistent with other Z parsers: it obtains normalized origin
comparison and hashing without an owned lowercase copy or scratch buffer.
`OriginView` therefore consists of `Scheme::T`, the already-normalized host
span, the port, and literal metadata; its equality and hash operate directly on
those values.  An owning `Origin` used as a retained cache key stores the enum
and one copy of the normalized host, never a scheme string or a second
canonicalization.  Both hashes combine the enum value, normalized host bytes,
and effective port; equality and hashing use the same fields and default-port
normalization.

The mutation contract must be explicit: path, query, fragment, delimiters, and
all case-sensitive data remain byte-for-byte unchanged, while the host is
normalized in the supplied buffer.  Callers which require pristine wire bytes
must consume or snapshot them before destructive parsing; the URL abstraction
must not retain a second copy merely to offer both spellings.

Use the existing H1 header-name path as the implementation precedent:
`parseLine()` supplies `ZuSpan<uint8_t>`, `parseKV()` identifies the key, and
`Zhttp::normalize()` lowercases that span before exposing a const view to the
next layer.  Generalize/rename that small primitive to the unambiguous
`lowerASCII(ZuSpan<uint8_t>)`, use it from both `parseKV()` and the shared
authority parser, and delete any private lowercase-copy loop.  This is a
mutation utility, not URL policy, so it remains in `ZhttpUtil.hh`.

`ZfURI` keeps its existing name and purpose.  It is the framework facility for
URI-style quoting, percent decoding, and structured query serialization; it
does not own HTTP URL or request-target parsing.

### Introduce `RequestTarget`

An HTTP request-target is not generally a URL.  HTTP/1 can carry four forms:

```text
/path?query                 origin-form
http://host/path?query      absolute-form
host:port                   authority-form (CONNECT)
*                           asterisk-form
```

HTTP/2 and HTTP/3 represent the corresponding information with pseudo-header
fields, and extended CONNECT has no single H1 request-target equivalent.
Introduce a wire-neutral, non-owning `RequestTarget` view declared in
`ZhttpURL.hh`.  It classifies the four H1 forms plus extended CONNECT and
exposes the applicable components without itself allocating or copying source
bytes:

```c++
struct AuthorityView {
  ZuBSpan raw;
  ZuBSpan host;
  uint16_t port = 0;
  bool explicitPort = false;
  bool ipv6Literal = false;
  bool normalized = false;
};

ZtEnumNS(ZhttpAPI, TargetForm, int8_t,
  Origin, Absolute, Authority, Asterisk, ExtendedConnect);

struct RequestTarget {
  AuthorityView	authority;
  ZuBSpan	raw;
  ZuBSpan	path;
  ZuBSpan	query;
  ZuBSpan	protocol;
  Scheme::T	scheme = -1;
  TargetForm::T	form = TargetForm::Origin;
  bool		hasQuery = false;
};
```

The final spelling may follow nearby enum conventions, but the type must
retain these semantics.  Spans borrow the parser or caller input and are valid
only for the synchronous callback in which the view is supplied.  Owners such
as `Service::RequestInfo` copy the fields they retain.

Constructing the view itself never allocates.  H1 components point into the
mutable request-line buffer.  H2/H3 components point into the message-owned
pseudo-header strings which `Fields::Semantics` must already retain until the
section is complete; they cannot safely point into shared compression-table
entries.  That required decode-lifetime copy is not repeated by
`RequestTarget`.

`raw` is the contiguous protocol-native target when one exists: the H1
request-line target, H2/H3 `:path` for ordinary/asterisk requests, or
`:authority` for classic CONNECT.  It is empty for extended CONNECT because
that operation exists only as a pseudo-header tuple.  Consumers must use the
typed components rather than assuming every form has a reconstructable raw
span.  `scheme < 0` denotes absence; do not spend a separate `hasScheme` byte.

Do not call this type `URL`, `URI`, or `Path`: each would be wrong for at least
one request-target form.

### Preserve query presence

Every path/query split must preserve the distinction between no query and an
empty query:

```text
/path       hasQuery == false
/path?      hasQuery == true, query.empty()
```

The current two-output `splitTarget()` in `Zhttpd.hh` loses this distinction.
The private `URL_::splitTarget()` preserves it, but is inaccessible to other
users.  Replace both with one public zero-copy operation in `ZhttpURL.hh`, for
example:

```c++
struct PathQuery {
  ZuBSpan	path;
  ZuBSpan	query;
  bool		hasQuery = false;
};

ZhttpAPI PathQuery splitPathQuery(ZuBSpan);
```

`splitPathQuery()` performs only the structural split.  It does not decode,
normalize, add a leading slash, remove dot segments, or apply filesystem
policy.  Parsing a complete request-target is a separate operation which uses
this primitive.

### Parsing versus policy

`ZhttpURL.hh/.cc` owns syntax and wire semantics:

- HTTP(S) URL parsing and serialization;
- URI-reference resolution into an HTTP URL;
- authority parsing, including bracketed IPv6 and ports;
- zero-copy path/query splitting;
- classification and validation of HTTP request-target forms;
- synthesis of a `RequestTarget` from H2/H3 pseudo-header fields.

`Zhttpd.hh` retains application and filesystem policy:

- percent-decoding a path for filesystem lookup;
- rejecting NUL, traversal, hidden paths, or platform-invalid names;
- deciding whether to redirect directories;
- MIME selection and HTML/path quoting.

Do not decode or path-normalize before the HTTP layer has established the
request-target's structural spans.  Host lowercasing is the sole intentional
destructive normalization; path, query, and other case-sensitive octets remain
available to access logs, redirects, signatures, and proxy behavior.

## Proposed public API

The precise implementation may use overloads to avoid redundant parsing, but
the public roles must remain distinct:

```c++
ZtEnumNS(ZhttpAPI, URLParseCode, int8_t,
  OK, UnsupportedScheme, MissingHost, InvalidAuthority, InvalidPort,
  InvalidCharacter, InvalidReference);

struct URLParseError {
  uint32_t	offset = 0;
  URLParseCode::T code = URLParseCode::OK;
  bool ok() const { return code == URLParseCode::OK; }
};

struct PathQuery {
  ZuBSpan path;
  ZuBSpan query;
  bool hasQuery = false;

  template <typename S> void print(S &s) const {
    s << path;
    if (hasQuery) s << '?' << query;
  }
  friend ZuPrintFn ZuPrintType(PathQuery *);
};

ZhttpAPI PathQuery splitPathQuery(ZuBSpan);

struct AuthorityView {
  ZuBSpan raw;
  ZuBSpan host;
  uint16_t port = 0;
  bool explicitPort = false;
  bool ipv6Literal = false;
  bool normalized = false;

  template <typename S> void print(S &s) const {
    if (ipv6Literal) s << '[';
    s << host;
    if (ipv6Literal) s << ']';
    if (explicitPort) s << ':' << ZuBoxed(port);
  }
  friend ZuPrintFn ZuPrintType(AuthorityView *);
};

ZtEnumNS(ZhttpAPI, RequestTargetParseCode, int8_t,
  OK, InvalidForm, InvalidMethod, InvalidScheme, InvalidAuthority, InvalidPath,
  InvalidProtocol, InvalidCharacter);
ZtEnumNS(ZhttpAPI, RequestTargetField, int8_t,
  Raw, Scheme, Authority, Path, Protocol);

struct RequestTargetParseError {
  uint32_t offset = 0;
  RequestTargetParseCode::T code = RequestTargetParseCode::OK;
  RequestTargetField::T field = RequestTargetField::Raw;
  bool ok() const { return code == RequestTargetParseCode::OK; }
};

struct RequestTarget {
  AuthorityView authority;
  ZuBSpan raw;
  ZuBSpan path;
  ZuBSpan query;
  ZuBSpan protocol;
  Scheme::T scheme = -1;
  TargetForm::T form = TargetForm::Origin;
  bool hasQuery = false;

  static RequestTargetParseError parseH1(
    RequestTarget &, Method::T, ZuSpan<uint8_t> raw);
  static RequestTargetParseError fromPseudo(
    RequestTarget &, Method::T, Scheme::T, ZuSpan<uint8_t> authority,
    ZuBSpan path, ZuBSpan protocol);
};

struct OriginView {
  ZuBSpan host;                    // already lowercased in parse storage
  uint16_t port = 0;
  Scheme::T scheme = -1;
  bool ipv6Literal = false;

  bool equals(const OriginView &) const;
  friend bool operator ==(const OriginView &l, const OriginView &r) {
    return l.equals(r);
  }
  uint32_t hash() const;
  template <typename S> void print(S &s) const {
    s << Scheme::name(scheme) << "://";
    if (ipv6Literal) s << '[';
    s << host;
    if (ipv6Literal) s << ']';
    if (port != Scheme::defltPort(scheme)) s << ':' << ZuBoxed(port);
  }
  friend ZuPrintFn ZuPrintType(OriginView *);
};

struct Origin {
  URLString host;                  // one retained normalized copy
  uint16_t port = 0;
  Scheme::T scheme = -1;
  bool ipv6Literal = false;

  explicit Origin(const OriginView &);
  OriginView view() const;
  bool equals(const Origin &) const;
  friend bool operator ==(const Origin &l, const Origin &r) {
    return l.equals(r);
  }
  uint32_t hash() const;
};

struct URL {
  ZuBSpan	raw;
  ZuBSpan	authorityRaw;
  ZuBSpan	host;
  ZuBSpan	path;
  ZuBSpan	query;
  ZuBSpan	fragment;
  URLParseError	parseError;
  uint16_t	port = 0;
  Scheme::T	scheme = -1;
  bool		hasQuery = false;
  bool		hasFragment = false;
  bool		explicitPort = false;
  bool		ipv6Literal = false;

  URL();                           // invalid/null borrowed view
  explicit URL(ZuSpan<uint8_t>);

  URLParseError error() const { return parseError; }
  bool ok() const { return parseError.ok(); }
  bool secure() const { return scheme == Scheme::https; }
  AuthorityView authority() const;
  OriginView origin() const;
  PathQuery pathQuery() const;

  template <typename S> void print(S &s) const {
    s << Scheme::name(scheme) << "://" << authority() << path;
    if (hasQuery) s << '?' << query;
    if (hasFragment) s << '#' << fragment;
  }
  friend ZuPrintFn ZuPrintType(URL *);
};

class URLStorage {
public:
  URLStorage() = default;
  explicit URLStorage(ZuBSpan);

  URLParseError assign(ZuBSpan);
  URLParseError adopt(URLString &&);
  URLParseError resolve(const URL &base, ZuBSpan uriReference);
  URL url() const;
  bool ok() const;
  URLParseError error() const;
};
```

Declare public enums with `ZtEnumNS(..., int8_t, ...)`, consistent with
`GUIDELINES.md`; do not hand-write `enum T`.  Scheme enumerator names are the
wire spellings `Scheme::http` and `Scheme::https`, so `Scheme::name()` and
`Scheme::lookup()` compose with parsing and printing without a second name map.
Because URI schemes are case-insensitive, parsing mixed-case input uses
`ZuICmp` (or lowers a writable scheme span before lookup); delete the private
`URL_::equalCI()` implementation rather than introducing another comparison
loop.

Use distinct URL, request-target, and Alt-Svc public error domains.  They have
different caller actions and must not retain unrelated values in one enum as
the current `URLCode` does.  Their implementations may share a private syntax
error representation and map it at the public boundary.  Rename the current
inconsistent `URLCode`/`URLError` pair and propagate the break.
Request-target errors identify both a component and an offset because H2/H3
pseudo-header inputs are discontiguous; H1 errors use `RequestTargetField::Raw`.

`URL` is a parsed, non-owning view.  Its constructor destructively parses and
splits the supplied mutable span once; `authorityRaw` retains the contiguous
authority production needed by `AuthorityView::raw`, all component spans point
into that input, and the host bytes are lowercased there.  It never owns or copies
the scheme, authority, path, query, or fragment.  Delete the current owned
`scheme`, `host`, and combined `target` representation, the static output-
parameter `parse()` API, and the output-parameter `pathQuery()` overload.
Do not retain a convenience constructor from `ZuBSpan`/`ZuCSpan`: immutable
callers must make ownership and its one required copy explicit with
`URLStorage`.

For `http://host` the stored `URL::path` is the empty span found in the input,
so complete-URL printing does not invent a slash.  `URL::pathQuery()` supplies
the HTTP request-target semantic default `/` from static storage when that path
is empty, while preserving `hasQuery`; it still performs no scan, allocation,
or copy.  Test complete-URL printing and request-target printing separately.

`URLStorage` is the explicit owning counterpart for immutable inputs and URLs
which must outlive their source bytes.  It owns one URL byte buffer plus
component offsets,
not separately allocated component strings.  `url()` cheaply returns a
rebased `URL` view.  Copy and move operations preserve offsets rather than
embedded pointers, so moving storage cannot leave stale spans.  Naming may be
shortened if local precedent supports it, but ownership must remain explicit
at call sites.  `assign(ZuBSpan)` is the explicit copy boundary for immutable
input; `adopt(URLString &&)` transfers existing mutable storage and normalizes
it in place without another copy.  Distinct names avoid ambiguous conversion
between Z strings, byte spans, and the owning `URLString` type.

`URLStorage::assign()`, `adopt()`, and `resolve()` commit only after the
candidate parses successfully, leaving the previous owned URL unchanged on
error.  Constructing a borrowed `URL` is destructive even on failure: callers
may inspect the error and offset, but must not assume that host bytes preceding
the error retained their original case.

Make `URL`, `AuthorityView`, `OriginView`, and `PathQuery` normal `ZuPrint`
delegates: each defines `template <typename S> void print(S &s) const` and the
corresponding `friend ZuPrintFn ZuPrintType(T *)`.  `URL::print()` emits the
complete absolute URL; the smaller views emit only their grammar production.
Delete allocating `URL::str()`.  Change `URL::authority()` from an allocating
`URLString` result to a cheap `AuthorityView`; preserve the succinct name.  A
caller which actually needs an owned string obtains one naturally with
`URLString s; s << url;` or `s << url.authority();`; a Builder or `ZiTxStream`
writes the same view straight to the transport.  Printing must stream the
stored spans and punctuation directly, without assembling a temporary
contiguous value.

Do not add an allocating `requestTarget()` hot-path convenience.  The request
Builder consumes `URL::pathQuery()` and streams the printable view directly;
its `hasQuery` bit controls the optional `?`.  URL and authority serialization
should integrate with
`ZuPrint`/streaming so callers materialize a `URLString` only when they
explicitly need retained text.

Retain fragment and query spans plus their separate presence bits in `URL`.
Printing the complete URL includes the fragment; constructing the HTTP
request-target never does.  The view preserves explicit empty query and
fragment values without allocating.

Bracketed IPv6 validation must not change the representation: `host` remains
a span into the URL or header buffer.  Use or add a span-native numeric-address
parser so validation works equally for writable message storage and read-only
H2/H3 compression-table values.  Do not allocate termination scratch and do
not temporarily overwrite `]` merely to call a NUL-terminated OS API.  If the
framework lacks that entry point, add it to `ZiIP` and reuse it here; do not put
a second IPv6 numeric parser in Zhttp.

Factor authority handling into one read-only structural parser plus a mutable
normalization entry point.  The read-only path validates and fills
`AuthorityView` with `normalized == false`; it is needed when H2/H3 values alias
compression-table storage.  The mutable path calls the same parser, applies
`lowerASCII()` to the returned host subspan, and sets `normalized == true`.
`URL`, H1 request targets, and message-owned H2/H3 pseudo-header storage use the
mutable path.  Only a normalized `AuthorityView` may produce an `OriginView` or
an owning `Origin`.  Alt-Svc may inspect a read-only authority transiently and
normalizes directly into cache storage if it retains the value.

## File organization

`ZhttpURL.hh` declares only the public URL/target model and small value/view
types.  Non-trivial parsing remains out of line in `ZhttpURL.cc`; consolidation
means one semantic owner and public API, not forcing the complete parser
inline into a header.

`AltSvcValue`, `AltSvcCursor`, `AltSvcCache`, and their storage types are not
URL values.  Move them to `ZhttpAltSvc.hh/.cc`, while reusing the authoritative
authority parser from the URL component.  Delete the current owning `AltSvc`
parse-result object; a field is parsed by the cursor, `clear` is cursor state,
and printable lists use a non-owning `AltSvcValuesView` adaptor.  A small
`AltSvcClear` printable tag represents the complete `clear` field value.  Origin
semantics remain with
`URL`, because HTTP URL origins are used by both the client and Alt-Svc cache.
Provide a zero-copy `OriginView` (`Scheme::T`, host span, port) for comparisons
and an owning `Origin` snapshot only for retained hash/cache keys.

Apply the same view/owner rule to Alt-Svc.  `AltSvcValue` is a parsed borrowed
view: `protocolID` (the Alt-Svc `protocol-id`, such as `h3`) and an explicitly
supplied `host` are spans into the field value; the quoted `alt-authority` is
represented by `host`, `port`, and literal/omission flags.  The `:port`
shorthand borrows the effective host from the supplied `OriginView`.  Numeric
and derived fields (`port`, `maxAge`, `persist`, `h3`, `ipv6Literal`) are stored
by value, and a presence bit records whether the host was omitted so
serialization can preserve the shorthand.  Lowercase an explicit host in place
when the field bytes are message-owned and writable; an inherited host is
already normalized by its `OriginView`.  A value which aliases H2/H3
compression-table storage remains read-only and is normalized directly into
`AltSvcValueStorage` only if retained.  Do not copy it merely to make a
transient view lowercase.

As with `URL`, a mutable Alt-Svc parse may have lowercased earlier host spans
before reporting a later syntax error.  Cache state remains transactional, but
the caller must not rely on restoration of case-insensitive source bytes.

Do not name the left-hand token `alpn`: the Alt-Svc field grammar calls it
`protocol-id`.  It encodes/identifies the alternative protocol and is distinct
from the quoted `alt-authority`; it is not the entire `protocol-id="authority"`
production.  Preserve `protocolID` bytes case-sensitively.  Rename `h3ALPN()`
to a protocol-id-specific predicate such as `h3Protocol()` and make its exact
wire-token semantics explicit.

Parsing an Alt-Svc field must not first build an owning array of alternatives.
Use an allocation-free cursor/iterator or synchronous callback parser which
yields one `AltSvcValue` view at a time and enforces `maxAlternatives`.  The
caller consumes each view synchronously.  The client cache validates the whole
field before a second pass snapshots only values it retains.  `clear` is a
field-level result and cannot coexist with yielded alternatives.

Prefer an out-of-line cursor for the parsing state:

```c++
class AltSvcCursor {
public:
  AltSvcCursor(ZuBSpan, OriginView, unsigned maxAlternatives);
  bool next(AltSvcValue &);
  AltSvcParseError error() const;
  bool clear() const;
};
```

A mutable-message constructor/adapter may additionally accept
`ZuSpan<uint8_t>` and normalize host bytes as they are identified.  A small
templated `each(L &&)` wrapper may provide callback syntax where useful.  This
keeps the non-trivial parser in `ZhttpAltSvc.cc`, makes input provenance and
callback synchrony explicit, and avoids both `ZmFn` erasure and an owning result
array.

Preserve all-or-nothing parsing and cache update semantics.  The cache first
runs the allocation-free cursor to the end to validate the complete field,
`maxAlternatives`, and `clear` exclusivity.  Only after successful validation
and cache-capacity admission does it run a second cursor over the still-live
header span, snapshot live alternatives into a prospective cache entry, and
atomically replace or delete the origin entry.  A malformed trailing
alternative or rejected new origin therefore cannot partially change the cache
or allocate value storage.  The second linear scan is preferable to an owning
intermediate parse array.

Define an explicit owning `AltSvcValueStorage` (or equivalently named cache
record) for values which survive the header callback.  It owns one compact
buffer for the retained textual fields plus offsets and scalar metadata, not
separate strings for protocol ID and host.  Copy/move preserves offsets rather
than embedded self-pointers; callers reacquire a view after moving the owner.
The cache stores this owner; public parse results do not silently acquire
ownership.  For `:port`, retain no duplicate host: bind the cached value view
to the cache entry's owning `Origin`; an explicitly different host is copied
once into the value owner, lowercasing directly into that destination if the
source was read-only.

Make `AltSvcValue` a `ZuPrint` delegate too:

```c++
struct AltSvcValue {
  ZuBSpan protocolID;
  ZuBSpan host;
  uint32_t maxAge = 86400;
  uint16_t port = 0;
  bool hostOmitted = false;
  bool persist = false;
  bool h3 = false;
  bool ipv6Literal = false;

  template <typename S> void print(S &s) const {
    s << protocolID << "=\"";
    if (!hostOmitted) {
      if (ipv6Literal) s << '[';
      s << host;
      if (ipv6Literal) s << ']';
    }
    s << ':' << ZuBoxed(port) << "\"; ma=" << ZuBoxed(maxAge);
    if (persist) s << "; persist=1";
  }
  friend ZuPrintFn ZuPrintType(AltSvcValue *);
};
```

For example, the method emits `h3=":443"; ma=86400` or
`h3="example.com:443"; ma=60; persist=1`.  It streams the protocol ID,
alt-authority, and parameters directly to `S`; it does not construct an
intermediate `URLString`.  Its output is a canonical valid Alt-Svc alternative
with the same semantics.  Keep field-list printing on a small printable
view/adaptor which inserts commas between values; do not put cache ownership or
parsing into the printing interface.

This is a semantic serializer, not a byte-preserving proxy representation.
Unknown extension parameters which the parser deliberately ignores are omitted
from output; callers which must forward an Alt-Svc field byte-for-byte retain
the original header span instead of printing `AltSvcValue`.

If sharing authority internals between the two `.cc` files would otherwise
require exposing implementation details, make the reusable authority result a
small public value/view in `ZhttpURL.hh`; do not introduce a second parser or
an internal compatibility header.

Update `Zhttp.hh`, `Makefile.am`, README material, and boundary manifests for
the new Alt-Svc file.  Delete stale comments which describe `ZhttpURL` as also
owning Alt-Svc parsing.

## Build and test policy

Use only the existing clang/debug build.  Minimize rebuilding: batch each
phase, compile the smallest affected targets at stable checkpoints, and do not
run a build while a deliberately breaking propagation is incomplete.  Do not
clean the tree or configure gcc/release variants.

Interim validation is limited to focused deterministic functional unit tests.
Run the complete interoperability matrix only after the final repository and
`GUIDELINES.md` audits; the successful full matrix must be the final
validation action.  Every phase's listed build/test criteria are conditional on
that phase being a buildable checkpoint; an interim phase explicitly records
them as deferred rather than running a knowingly meaningless build.

## Phase 1 — Establish the shared syntax model

Add `Scheme`, `AuthorityView`, `PathQuery`, `TargetForm`, `RequestTarget`,
the three coherent parse-error domains, and the public split/parse declarations
to `ZhttpURL.hh`.  `AltSvcParseCode`/`AltSvcParseError` initially remain beside
the still-unsplit Alt-Svc declarations and move with them in phase 5; this is
the final error API, not a compatibility shim.
`RequestTarget::parseH1()` parses a request-line target, while
`RequestTarget::fromPseudo()` validates and synthesizes the same view from
H2/H3 pseudo-header components, including classic versus extended CONNECT.
Implement the shared primitives in `ZhttpURL.cc` by adapting the existing
private helpers rather than adding another parsing path.

Rename the existing in-place `Zhttp::normalize(ZuSpan<uint8_t>)` helper to
`lowerASCII()` and propagate its H1 `parseKV()` call.  Use the same helper for
mutable URL, authority, and eligible Alt-Svc host spans; do not introduce a
second ASCII-folding implementation.  Replace read-only case-insensitive
scheme and Alt-Svc parameter comparisons with `ZuICmp`, then delete
`URL_::equalCI()` and its scalar `lower()` helper.  Until phase 5 removes the
old owning Alt-Svc representation, implement `lowerCopy()` as one ordinary copy
followed by `lowerASCII()` over the destination rather than retaining another
folding loop.

Define and test these invariants:

- `splitPathQuery()` is zero-copy and never changes the input;
- `hasQuery` distinguishes absent and empty queries;
- fragments are never part of a request-target path or query;
- origin-form starts with `/`, except the distinct `*` form;
- absolute-form accepts only a valid HTTP(S) URL;
- an absolute-form URL with an empty textual path exposes the same static `/`
  semantic path as `URL::pathQuery()`, while `raw` retains the absolute target's
  structure and documented host normalization;
- H1 authority-form is accepted only for ordinary CONNECT and requires a
  valid host plus port;
- asterisk-form is accepted only where HTTP semantics allow it;
- extended CONNECT is represented by `TargetForm::ExtendedConnect` and retains
  its scheme, authority, path/query, and protocol spans without inventing an H1
  raw target;
- control characters, spaces, malformed percent escapes, invalid ports,
  malformed IPv6 literals, and embedded fragments are rejected with the
  correct source offset;
- validation does not percent-decode or dot-normalize the raw target.

The shared authority implementation provides read-only validation and mutable
normalization paths over the same structural parser.  The mutable path leaves
its host span in caller storage after lowercasing it in place.  Origin equality
and hashing consume only that normalized representation; neither relies on a
lowercase component copy.  Numeric-literal validation is span-native, and the
returned authority and URL views still point only into caller input.

Use the same authority parser for absolute URLs, CONNECT authority-form, and
Alt-Svc authority values, with explicit policy parameters for default or
required ports rather than caller-specific forks.  Its explicit-port policy
also distinguishes HTTP authorities, which reject port zero, from local socket
bind authorities such as QUIC migration's `127.0.0.1:0`, where zero requests an
ephemeral port; do not reintroduce a second host/port splitter for that case.

### Phase 1 acceptance criteria

- `ZhttpURL.hh/.cc` exposes and implements one path/query splitter, one
  authority parser, one HTTP URL parser, and one request-target classifier.
- `Scheme` is the single string-to-enum and enum-to-name authority; new
  request-target state stores `Scheme::T`, and the existing URL state is queued
  for the complete view migration in phase 2.
- Authority parsing lowercases hosts in caller-owned mutable storage; origin
  equality and hashing need no normalized host allocation.
- H1 header names and URL/authority hosts use the same `lowerASCII()` mutation
  primitive, and repository searches find no duplicate uppercase-to-lowercase
  loop in Zhttp parsing code.
- Read-only case-insensitive matching uses `ZuICmp`; `URL_::equalCI()` and its
  private comparison loop are gone.
- No new heap allocation occurs when splitting or parsing an origin-form
  request-target into borrowed views.
- Bracketed IPv6 validation uses the shared span-native `ZiIP` path and has
  focused valid/invalid tests over non-NUL-terminated subspans.
- Unit tests cover absent/empty/non-empty query distinctions, all four H1
  request-target forms, and extended CONNECT, including invalid method/form
  combinations.
- Existing URL and RFC 3986 relative-resolution cases still pass after error
  type renaming.
- Existing Alt-Svc parse tests use `AltSvcParseError`; no Alt-Svc outcome
  remains in the URL error domain.
- Audit phase 1 against `GUIDELINES.md` and repair naming, span lifetime,
  allocation, copying, container, formatting, layout, and Z-idiom issues.

## Phase 2 — Make URL parsing zero-copy and retention explicit

Replace the current component-owning `URL` with the mutable-span-constructed
view and add `URLStorage` for the minority of paths which require retention.
Parsing the input span establishes `host`, `path`, `query`, and `fragment`
boundaries
once; no method reparses a combined `target` string.

Centralize these operations on the same component representation:

- absolute HTTP(S) parsing;
- authority and origin construction;
- allocation-free URL streaming/printing;
- allocation-free request-target streaming from `PathQuery`;
- relative URI-reference resolution;
- dot-segment removal and path merging.

Resolution writes the merged result directly into the candidate
`URLStorage` buffer.  Delete the current temporary `absolute`, `path`, `query`,
and combined-target strings; use source spans plus destination offsets and
perform dot removal while emitting the one retained result.

Give `URL`, `AuthorityView`, `OriginView`, and `PathQuery` their `ZuPrint`
delegates during this phase and propagate callers from `str()`/`authority()` to
direct streaming.  This includes direct use by message Builders and
`ZiTxStream`; string materialization remains an explicit caller-side choice.

Propagate the `Origin` scheme/host representation break immediately through the
existing Alt-Svc cache and client call sites, even though their file and parse-
result cleanup occurs in phase 5.  Do not leave a temporary string-scheme
constructor or compatibility owner between phases.

`removeDots()` applies only during RFC 3986 reference resolution.  It must not
be silently applied to an inbound HTTP request-target or to the server's raw
access-log value.

Make URL lifetimes visible at every call site:

- parse stable mutable command/request input directly into a `URL` view;
- route immutable input through `URLStorage`, which copies once into its owned
  mutable buffer and then parses in place;
- keep that view only while its source span is guaranteed stable;
- use `URLStorage` when a URL crosses a callback, queue, retry, redirect, or
  asynchronous discovery boundary and its existing source owner does not
  already remain stable for that entire lifetime;
- never retain a `URL` view into a pooled `ZiIOBuf` after the synchronous
  parser callback returns.

Resolve a response `Location` synchronously while its header span is valid,
writing the result directly into attempt-owned `URLStorage`.  Delete the
intermediate `Attempt::location` copy and do not first copy, later parse, and
then copy the resolved URL again.  `URLStorage::resolve(base, location)` accepts
the read-only header span, emits absolute or relative results once into its own
mutable buffer, and parses/normalizes that destination in place.  This works
for H1 buffers and H2/H3 compression-table aliases without casting away const
or creating an intermediate URL view into the header value.

Update client request Builders (`zhttp.cc`, fallback/cancellation tests, and
other dependents) to use `URL::pathQuery()` without re-splitting or
materializing a request-target string.  Their `host(l)` callback passes the
printable `URL::authority()` view, not bare `URL::host`, so an explicit port is
preserved in H1 `Host`, H2/H3 `:authority`, and classic CONNECT.  Ensure a query
containing `/`, `?`, percent escapes, or an empty value round-trips correctly.
Protocol `host()` lambdas accept the printable value generically and pass it to
their stream/count-and-encode path; do not coerce it to `ZuCSpan` or materialize
a temporary merely because the former callback supplied a string.

### Phase 2 acceptance criteria

- Constructing `URL` from a mutable span performs no component allocation or
  copy, lowercases the host in place, and establishes every component boundary
  once.
- There is no const-span `URL` parsing overload which hides a normalization
  copy; immutable inputs use `URLStorage` explicitly.
- `http://host` retains an empty parsed path when printed as a URL but yields
  `/` when adapted to an HTTP request-target, without reparsing.
- URL and origin state store `Scheme::T`; no retained scheme string or repeated
  HTTP/HTTPS comparison remains.
- URL printing, request-target streaming, `origin()`, and redirect resolution
  agree on scheme, authority, explicit/default ports, path, query, and
  fragment semantics.
- Relative resolution constructs one destination buffer and no separately
  allocated component or combined-target temporaries.
- Repository searches find no `URL::str()` or allocating authority helper;
  `URL::authority()` returns a printable view and writes directly to the
  destination.
- Client Builders pass the complete printable authority through `host()`;
  explicit and non-default ports are not dropped.
- `URL` is a trivially movable borrowed view; `URLStorage` copy/move operations
  leave owned bytes and component offsets valid without stored self-pointers
  or corrective reparsing.
- Failed `URLStorage::assign()`/`adopt()`/`resolve()` calls preserve the prior
  owned URL;
  borrowed parse failure has the documented destructive-input semantics.
- No `URL` view into a receive buffer survives its synchronous callback;
  retained client state uses one explicit owner and no intermediate Location
  copy.
- All client Builders use the consolidated API; repository searches find no
  local URL path/query splitter.
- Focused URL, redirect, existing Alt-Svc cache, client cancellation, and
  fallback tests pass in the clang/debug build.
- Audit phase 2 against `GUIDELINES.md` and repair allocation, hidden-copy,
  temporary-string, ownership, naming, and performance issues.

## Phase 3 — Unify H1/H2/H3 request-target semantics

Parse and validate request targets at the protocol boundary, before calling
the application Parser sink.

For H1:

- change `parseOperation()` to accept the mutable `ZuSpan<uint8_t>` delivered by
  `parseLine<false>()`, and do not const-qualify the target slice before shared
  parsing has normalized any authority host;
- classify the request-line target as origin-, absolute-, authority-, or
  asterisk-form using the method;
- reject malformed or method-incompatible forms synchronously;
- preserve the structural target for logging/proxy use, subject only to the
  documented in-place host normalization for absolute-form and authority-form;
- expose parsed components without allocation through `RequestTarget`.

For H2/H3:

- synthesize the same `RequestTarget` view from `:method`, `:scheme`,
  `:authority`, `:path`, and optional `:protocol` semantics;
- validate ordinary, classic CONNECT, and extended CONNECT combinations in
  the shared target layer rather than maintaining subtly different protocol
  rules;
- accept `:path: *` only for the valid asterisk case;
- preserve the distinction between absent `:path` for classic CONNECT and an
  empty or malformed ordinary path.
- classify extended CONNECT as `TargetForm::ExtendedConnect`, retain
  `:protocol`, and leave `raw` empty rather than fabricating a contiguous
  request-target.

Parse `:scheme` through the same string-to-`Scheme::T` operation used by H1
absolute-form and `URL`.  Change `Fields::scheme` and dependent semantic state
to carry the enum after validation; do not continue passing a string merely so
each consumer can compare it again.

H2/H3 decoder callbacks may expose values which alias static or dynamic
compression-table storage; never mutate those spans.  `Fields::Semantics`
already has to retain pseudo-header values until `finish()`.  Copy `:authority`
exactly once into that message-owned `Fields::String`, normalize its host in
place there, and let the delivered `RequestTarget` borrow it.  This is required
message state, not scratch normalization storage.  Keep H1 allocation-free by
normalizing its request-line buffer directly.

Change the request Parser-sink callback to receive the typed view:

```c++
void operation(Method::T, const RequestTarget &);
```

Propagate this intentional break through `Fields::Semantics`, H1/H2/H3
parsers, `ClientMessage`, `Service`, fixtures, tests, and examples.  Do not
retain the old `(Method::T, ZuBSpan)` overload or detect it with SFINAE.
For extended CONNECT, the receive-side `:protocol` value is delivered only as
`RequestTarget::protocol` within this callback; `protocol()` remains a
transmit-Builder callback and is not a second Parser-sink notification.

Break the transmit Builder callback shape so the path/query production is one
printable value:

```c++
operation(l) -> l(method, target)
```

The current `l(method, path, query)` form cannot distinguish `/path` from
`/path?` because both supply an empty query span.  Do not add a fourth
primitive boolean argument or keep the old form as an overload.  `target` is
any `ZuPrint`-compatible object representing the complete path/query production;
the standard value is the small borrowed `PathQuery`, whose `hasQuery` bit
prints `?` even when `query` is empty.  Protocol lambdas forward and print the
actual type without coercing it to a span.  This retains custom `print(S&)` /
`operator <<` behavior and avoids allocating a receive-side `RequestTarget`
merely for transmission.

Keep the original transport-neutral division of Builder callbacks established
in `zhttp12.md`: `operation()` supplies method plus path/query, `host()` supplies
authority, and `protocol()` supplies the optional extended-CONNECT protocol.
This plan supersedes only zhttp12's three-argument
`l(method, path, query)` spelling with `l(method, target)`; it does not merge
or remove those succinct Builder members.
Serializers combine those semantic inputs as follows:

- ordinary requests and OPTIONS `*` print `target` as the H1 target or H2/H3
  `:path`; URL-backed Builders pass `URL::pathQuery()`;
- classic CONNECT requires an empty target value, uses `host()` as H1
  authority-form or H2/H3 `:authority`, and has no path/query on the wire;
- extended CONNECT is rejected by H1 and uses `target`, `host()`, and
  `protocol()` for the H2/H3 pseudo-header tuple.

The transport-neutral client Builder does not emit H1 absolute-form; accepting
and parsing absolute-form remains required for servers and proxies.  If a
future proxy transmit API needs it, add an explicit proxy-target callback
rather than overloading `PathQuery` with a complete URL.  Validate each
method/host/protocol combination before emitting any start line or pseudo-header
block.  As with custom header values, the bytes produced by an arbitrary
printable target remain the application Builder's contract; do not buffer them
solely to reparse outbound syntax.

### Phase 3 acceptance criteria

- H1/H2/H3 deliver the same typed `RequestTarget` semantics to applications.
- Receive-side extended-CONNECT consumers obtain `protocol` from the typed
  target; repository searches find no stale Parser-sink `protocol(ZuBSpan)`
  callback.
- The four H1 forms and the H2/H3-only extended-CONNECT form are distinct;
  `protocol` is populated only for the latter, and `raw` follows its documented
  protocol-native semantics.
- Validated H2/H3 scheme state is `Scheme::T`, with no protocol-specific
  `"http"`/`"https"` comparisons outside the shared parser.
- All transmit Builders use `operation(l) -> l(method, target)`.  URL-backed
  Builders pass `PathQuery`, custom printable target types remain intact, and
  absent versus empty queries are preserved on H1/H2/H3.
- A non-span target with custom `print(S&)` has functional coverage through H1
  direct streaming and H2/H3 count/encode paths.
- H1/H2/H3 derive classic CONNECT from `host()` consistently; H1 rejects
  extended CONNECT, while H2/H3 combine `target`, `host()`, and `protocol()`
  without fabricating a raw target.
- Invalid target forms fail before request dispatch; valid classic and
  extended CONNECT behavior remains distinct.
- The shared H2/H3 pseudo-header synthesis reports the offending
  `RequestTargetField` and field-local offset rather than an ambiguous offset
  into a nonexistent concatenation.  H2/H3 message parsers map any such
  failure to their existing invalid-field-section/protocol-error path; they do
  not invent a second public diagnostics channel merely to preserve the richer
  internal parse result.
- H1 absolute-form and OPTIONS `*` have explicit functional coverage.
- H2/H3 ordinary `:path`, empty query, OPTIONS `*`, classic CONNECT, and
  extended CONNECT have matching functional coverage.
- No protocol parser or Builder contains an independent `?` split or
  request-target form classifier.
- Focused parser, fields, H2 message, H3/QPACK, and message-engine tests pass
  in clang/debug.
- Audit phase 3 against `GUIDELINES.md`, especially borrowed-span lifetimes,
  callback synchrony, Rx ownership, copying, and compile-time dispatch.

## Phase 4 — Migrate `Service` and `zhttpd`

Change `Service::RequestInfo` to retain the information its workload needs
from the typed target.  For H1 and the H2/H3 forms with a native `raw` span,
retain that target once and represent path/query/authority subviews as offsets
where this remains clear and move-safe.  For extended CONNECT, retain the
required pseudo-header components independently; do not concatenate a fake raw
target merely for logging.  No workload calls a second splitter.

Delete `Zhttpd::splitTarget()`.  `StaticPlanner` consumes the parsed
path/query supplied by `RequestInfo`.  Keep `decodeNormalizePath()` and
filesystem security policy local to `Zhttpd`, but have it operate only on the
already selected raw path component.

Dispatch on `TargetForm` before filesystem planning.  Static-file handling may
consume origin-form and the path component of accepted absolute-form requests;
authority-form, asterisk-form, and extended CONNECT never fall through into
path decoding merely because their `path` span is empty or unusual.

Review redirects carefully:

- redirects which intentionally reproduce a contiguous request-target use the
  retained `raw` value, including its documented normalized host for H1
  absolute-form;
- filesystem-generated redirects serialize a path/query explicitly and do
  not concatenate unvalidated components;
- fragments are never emitted in an HTTP request-target;
- empty-query presence is preserved where required.

Update `ZhttpBoundary.md` so `splitTarget` is no longer listed as workload
code and the shared URL/target facilities are classified as reusable HTTP
mechanisms.

### Phase 4 acceptance criteria

- `Zhttpd.hh` contains no `splitTarget` or equivalent `?` scan.
- Each server request target is parsed once at the protocol boundary.  Each
  component needed beyond the callback is copied at most once into Rx-owned
  retained state; extended CONNECT does not incur an additional synthesized
  target copy.
- Static-file lookup receives only the path component; query bytes cannot
  enter filesystem normalization.
- Non-path target forms cannot reach `StaticPlanner`; method/form routing is
  explicit rather than inferred from an empty span.
- Access logging uses `raw` where the protocol has one and logs typed
  pseudo-header components for extended CONNECT; only case-insensitive host
  bytes may differ from received spelling, while path and query remain exact.
- Traversal, percent-escape, hidden-file, directory redirect, empty-query,
  absolute-form, CONNECT, and asterisk-form server tests pass.
- Audit phase 4 against `GUIDELINES.md`, focusing on Rx/Tx ownership, offsets
  into moved strings, scratch storage, security-policy placement, and copies.

## Phase 5 — Separate Alt-Svc and remove residue

Move Alt-Svc parsing and cache declarations from `ZhttpURL.hh` to
`ZhttpAltSvc.hh`, and their definitions from `ZhttpURL.cc` to
`ZhttpAltSvc.cc`.  Move the already-distinct
`AltSvcParseCode`/`AltSvcParseError` declarations with them.  Reuse the URL component's
authority parser; do not copy authority, IPv6, port, or case-folding logic.
Move Alt-Svc-only token/parameter/whitespace scanners with the parser, and
factor a helper into `ZhttpUtil.hh` only when it is genuinely shared; do not
leave Alt-Svc grammar helpers in `ZhttpURL.cc` merely to avoid moving them.

Keep `AltSvcParseCode` declared with `ZtEnumNS(..., int8_t, ...)`, like the
other public parse domains; do not hand-write `enum T` during the move.

At the same time, replace component-owning parse results with the borrowed
`AltSvcValue` view and allocation-free field parser described above.  The
response-header callback must consume the field synchronously while its source
span is valid.  Normalize in place only for writable message-owned bytes; never
mutate H2/H3 compression-table aliases.  Feed yielded views directly into
Alt-Svc policy; only cache-accepted alternatives become
`AltSvcValueStorage`.  Delete the current `AltSvc`/`AltSvcValues` owning parse
result.  Cache query APIs return borrowed cache views synchronously or copy
explicitly into caller-selected ownership, rather than disguising copies behind
the old type.  Use the validation-then-snapshot transaction above so this does
not weaken failure atomicity.

Use `AltSvcCache::get(origin, l)` for callback iteration on the cache-owning
shard.  Each callback receives an `AltSvcValue` view valid only for that call;
do not return a borrowed array which can outlive hash-node mutation.

Add `ZuPrint` integration for `AltSvcValue` and the comma-separated field
adapter.  Round-trip tests must stream directly into both a Z string and a
test stream/sink, reparse the result, and compare semantics.  Cover inherited
host (`:port`), explicit reg-name, bracketed IPv6, default and explicit
`ma`, `persist`, multiple alternatives, and `clear`.  Printing `clear` belongs
to `AltSvcClear`; it is not an `AltSvcValue`.

Split `ZhttpURLTest` into focused URL/request-target and Alt-Svc tests if that
improves target ownership (`ZhttpURLTest` and `ZhttpAltSvcTest`).  Update build
manifests and public aggregate includes.  Preserve source compatibility only
where an aggregate include naturally provides both components; do not leave
type aliases in `ZhttpURL.hh`.

Perform repository-wide searches for:

- `splitTarget` and local scans for `?` used to parse URL/request targets;
- the old output-parameter `pathQuery` API;
- `URLCode`/`URLError` after the selected coherent rename;
- old Parser-sink `(Method::T, ZuBSpan)` operations;
- Alt-Svc declarations or implementation remaining in `ZhttpURL`;
- old `AltSvcValue::alpn` or `h3ALPN` names which conflate `protocol-id` with an
  ALPN value;
- the old component-owning `lowerCopy()` helper;
- ad hoc authority, port, bracketed-IPv6, dot-segment, or scheme parsing;
- duplicate ASCII lowercasing loops which should use `lowerASCII()`.

Classify same-named DNS discovery `target` values as unrelated endpoint/DNS
terminology; do not rename them merely because the word overlaps.

### Phase 5 acceptance criteria

- `ZhttpURL.hh/.cc` owns only HTTP URL, origin, authority, URI-reference
  resolution, and request-target syntax.
- `ZhttpAltSvc.hh/.cc` owns Alt-Svc values, parsing, and caching while sharing
  the single authority parser.
- Parsing an Alt-Svc response field allocates no alternative array or component
  strings; only alternatives retained by the cache acquire explicit storage.
- A malformed field and a rejected `maxAlternatives` bound leave the prior cache
  entry unchanged; `clear` deletes only after complete-field validation.
- Cache reads iterate borrowed values synchronously on the owning shard; no
  cache-node span escapes the callback.
- Explicit Alt-Svc hosts are lowercased in writable message storage or directly
  into the one retained cache owner; inherited hosts reuse normalized `Origin`
  storage, compression tables are never mutated, and `protocolID` bytes are not
  case-folded.
- `AltSvcValue`, `AltSvcValuesView`, and `AltSvcClear` stream through `ZuPrint`
  without an intermediate string and produce reparsable, semantically
  equivalent output.
- Searches find exactly one implementation of every URL/request-target
  primitive and no compatibility APIs.
- Build manifests, aggregate headers, README text, examples, and boundary
  documentation reflect the new organization.
- All affected functional unit tests pass in the clang/debug build.
- Audit phase 5 against `GUIDELINES.md` and repair dead code, public-header
  dependencies, heap IDs, naming, layout, allocation, and copying issues.

## Phase 6 — Integrated validation

Complete the repository-wide parser/name searches and audit the entire change
against `GUIDELINES.md`.  Repair every identified ownership, allocation, copy,
parsing, naming, layout, sharding, or performance issue.  Then build the
complete repository in the existing clang/debug configuration and run the
complete deterministic functional test set, with particular coverage for:

- absolute HTTP and HTTPS URLs, default and explicit ports, reg-names, and
  bracketed IPv6;
- invalid authorities, userinfo, control characters, spaces, malformed
  percent escapes, invalid ports, and unsupported schemes;
- absent, empty, and non-empty query values;
- fragment spans, explicit empty fragments, complete-URL printing, and
  unconditional fragment omission from HTTP request-targets;
- RFC 3986 relative-reference resolution and dot-segment cases;
- all HTTP request-target forms and invalid method/form pairings;
- H1 absolute-form, origin-form, authority-form, and asterisk-form;
- H2/H3 ordinary requests, classic CONNECT, and extended CONNECT;
- protocol-native target/component logging versus decoded filesystem lookup;
- redirects which preserve query presence without emitting fragments;
- zero-copy request-target parsing and stable retained `Service` offsets;
- Alt-Svc IPv6/port parsing and cache behavior after the file split;
- direct `ZuPrint` streaming and semantic print/parse round trips for URL,
  authority, origin, path/query, and Alt-Svc views;

If the build or a functional test exposes a defect, repair it and repeat the
repository/`GUIDELINES.md` audit, complete clang/debug build, and affected
functional tests until the stable checkpoint is clean.

Only after those steps pass, run the full H1/H2/H3 interoperability matrix as
the final validation action.  If it exposes a defect, repair it, repeat the
affected audit/build/functional checks, and make a successful full matrix the
new final action.

### Phase 6 acceptance criteria

- The complete repository builds and all functional tests pass under
  clang/debug.
- The final repository and `GUIDELINES.md` audits are clean before
  interoperability testing begins.
- Client requests, redirects, server dispatch, access logs, static-file
  planning, H1, H2, H3, CONNECT, and Alt-Svc all use the consolidated model.
- H1 request-targets are split without copying.  H2/H3 pseudo-header values are
  retained once for section lifetime, then split without secondary component
  copies.  No path is percent-decoded or normalized at the protocol boundary;
  case-insensitive hosts are lowercased at the first message-owned mutable
  boundary, and shared compression-table storage is never mutated.
- Repository-wide searches find no duplicate URL, authority, path/query,
  URI-reference, or request-target parser.
- Public terminology is consistent: `URL` for absolute HTTP(S) locators,
  `RequestTarget` for HTTP request targets, and `ZfURI` for generic URI
  encoding/decoding facilities.
- Audit phase 6 against `GUIDELINES.md` and repair every remaining ownership,
  allocation, copy, naming, layout, sharding, and Z-idiom issue before the final
  interoperability run.
- The final full interoperability matrix passes, and no validation action is
  performed after it.
