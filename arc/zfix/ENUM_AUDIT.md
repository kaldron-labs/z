# Enum and string domains

Managed configuration uses `ZtEnum` for closed choices. Wire views retain the
received bytes: recognizing a configured value and preserving an unsupported
peer value are separate responsibilities. This follows the enum and string
rules in [GUIDELINES.md](docs/GUIDELINES.md).

| Domain | Representation and reason |
| --- | --- |
| Session mode | `Transport`: `Raw`, `FIX4`, `FIX44A`, `FIXT11`, preserving their ordinals. These are implementation choices, independent of the negotiated wire version. |
| Managed BeginString | `Version`: `V4_0` through `V4_4`, then `VT1_1`. Its `Wire` map supplies `FIX.4.0` through `FIX.4.4` and `FIXT.1.1`. |
| Managed application version | `AppVersion`: `V2_7`, `V3_0`, `V4_0` through `V4_4`, `V5_0`, `V5_0_SP1`, `V5_0_SP2`, `Latest`. Its `Wire` map supplies decimal strings `0` through `10`. |
| Utility mode, version and scenario | Independent utility enums parse the CLI vocabulary once. The native adapter explicitly converts them to library configuration; independent QuickFIX tools do not acquire a native session dependency. |
| Probe fixture case | `ProbeCase` and its name map describe the finite set of fixture cases. |

Version and application-version configuration fields default to their `N`
sentinel when unset; valid values are below `N`. Mode defaults to `FIX4`.
Zero is a real version ordinal, including application version `V2_7`, and must
not mean absent. Canonical wire conversion uses `ZtEnumMap` with `s2v()`/`v2s()`,
rather than a second switch or string table.
The framework supports separate readable and wire maps in
[ZtEnum.hh](../zt/src/ZtEnum.hh).

The [native workload adapter](util/load_native.hh) directly assigns matching
version ordinals and adds the fixed `Transport::FIX4` offset to utility mode
ordinals. `static_assert` checks their correspondence; no mapping switch or
native-library dependency is introduced into the shared workload definitions.

Application-version value `10` is confirmed by the primary
[FIX Session Layer standard, June 2020, §11.2, printed page 78](https://www.fixtrading.org/wp-content/uploads/download-manager-files/FIX_Session_Layer_June_2020.pdf#page=78):
its code-set table assigns it to `FIXLatest`. Sections 10 and 5.3.7 distinguish
standard application-version fields 1128/1137 from custom version strings
1129/1408. The pinned QuickFIX 1.16 dictionary stops at `9`; that older dictionary
does not justify omitting `Latest` from the native enum. This representation
does not add new session-profile behavior or select a business dictionary.

The following values remain strings or byte spans:

- `Fmt::Header`, `FrameView` and `Session::Logon` preserve raw BeginString and
  application-version fields. Managed lifecycle checks compare them with the
  configured wire names. Framing and standalone decoding do not reject a value
  merely because the managed enum does not recognize it.
- `Context.version` remains a borrowed wire span. It reports received metadata;
  replacing it with another lookup or an additional enum member would duplicate
  information already available in the retained header.
- MsgType is an open catalog key. The canonical example already uses `U1`.
  The [FIX standard, §11.7](https://www.fixtrading.org/wp-content/uploads/download-manager-files/FIX_Session_Layer_June_2020.pdf#page=79)
  permits privately defined message types beginning with `U`. The seven built-in
  controls already have `Session::Msg` union alternatives; that classification
  does not close the business-message domain.
- `SessionConfig.id` is the opaque application-defined session key used alone
  for attachment and checkpoint identity. `Checkpoint.id` owns its string;
  `stateMatches`/`stampState` compare only ID and epoch. `HeaderConfig header`
  separately holds version, CompID, SubID and LocationID values. Those wire IDs
  are strings agreed by applications and validated against current connection
  configuration. There is no qualifier field: applications may encode any
  chosen qualifiers in their ID without a library-imposed tuple or routing rule.
  TestReqID is an opaque challenge echoed by the peer; it also remains a string.
- Discriminator values belong to each application catalog. Text, custom version
  identifiers and message encoding names remain strings; secure data and
  signatures remain byte spans. Section 10 of the FIX standard specifies
  MessageEncoding(347) as a string, and the pinned FIXT dictionary does not close
  its value set to the historical encodings listed by older FIX dictionaries.

Application ownership does not make a standard FIX code set arbitrary. The
remaining strings below in [the example schema](example/zfix.hh) and
[the workload model](util/load.hh) are concrete typed-domain candidates:

| Member | FIX field |
| --- | --- |
| `handling` | HandlInst(21) |
| `side` | Side(54) |
| `type` | OrdType(40) |
| `source` | PartyIDSource(447) |
| `kind` | ExecType(150) |
| `status` | OrdStatus(39) |

The pinned [QuickFIX FIX44 dictionary](https://github.com/quickfix/quickfix/blob/2ce8a60667d95a55cdc57a210f165e19cb757126/spec/FIX44.xml)
declares all six as enumerated `CHAR` fields. Local `ZtEnum` domains with wire
maps would demonstrate typed business fields more clearly than string literals.
They would use existing codec `Enum` metadata; they do not require a global FIX
dictionary in the session library.

This remains a separate schema decision. The example currently accepts broader
or unknown string values in several of these fields; the workload restricts
some fields through its own validation. Maps containing only values generated
by these programs would narrow accepted input. Any conversion must choose the
supported standard-version maps and deliberately define unknown/error handling,
then propagate that contract through both native and independent SDK adapters.
Raw wire views and application-defined identifiers remain distinct from these
known standard code sets.

Legacy [UFix.hpp](ufix/UFix.hpp) already separates `UFixTransport`, `UFixTag8`
and `UFixVersion`. [UFixRxMsg.hpp](ufix/UFixRxMsg.hpp) converts application
version 1128 while retaining MsgType and peer IDs as strings. These are useful
local precedents; their older version lists are not the current authority, and
the human-readable `UFixVersion` map is not the numeric ApplVerID wire map.

The enum migration's earlier passing snapshot is retained under
`/tmp/zfix-enum-audit/`. The subsequent opaque-ID repair passed 318 unit scopes,
225 TCP/TLS integration scopes, 76 canonical interoperability process cases,
24 lifecycle cases, six probe cases and both standalone example pairs, using
the same Linux x64 Clang/ASan configuration and dependency libraries.
Its independent SDK dependency gate exposed the existing QuickFIX TCP shutdown
race documented in [interop/README.md](interop/README.md); that gate failed and
was not suppressed or repaired by a retry. Current logs and hashes are retained
under `/tmp/zfix-session-id/`. Business-schema candidates above remain audit
findings, not implemented schema changes.
