# Zv: Framework server-side utilities

Zv adds file loading/saving and runtime parameter adapters to Zf's configuration
codecs. It supports Z's native configuration syntax, TOML and YAML, and maps
configuration nodes into thread, scheduler, multiplexer, ring and stack
parameters. Include the relevant `<zlib/Zv*.hh>` header and link `libZv`.
The object metadata and in-memory codecs are documented in [Zf](../zf/README.md).

## Loading native configuration

`ZvCf::load(path)` returns a tuple containing the root file's byte count and
an owning configuration tree. File and parse failures throw `ZeException`.

```cpp
#include <zlib/ZvCf.hh>

void readConfig(const Zi::Path &path) {
  auto result = ZvCf::load(path);
  auto root = ZuMv(result.p<1>());
  // Use root with ZfCf::handler<T>(root) or inspect its AnyNode fields.
}
```

Native configuration supports `${NAME}` expansion, `${TOPDIR}` and `${CURDIR}`,
and `%include(path)`. Nested includes retain the top directory and update the
current directory for each file; recursive includes and malformed directives
produce diagnostics with include context. An optional `ZfCf::PctFn` callback
handles application-specific percent directives. The
[file-loading tests](test/ZvCfTest.cc) demonstrate these behaviors and error paths.

## Typed configuration and updates

Declare the `Cf` facet with `ZfStruct`, then bind a node through
`ZfCf::handler<T>`. Constructor fields are collected for loading; fields marked
`Mutable` participate in updates. Keep the tree alive while borrowed fields
refer into it, or use owning application fields for configuration retained
after parsing. Parsing configuration does not automatically apply live changes
to running services; arrange those updates on each component's owning thread.

The runtime adapters accept a node and optional existing parameter object:

```cpp
#include <zlib/ZvThreadParams.hh>

ZmThreadParams threadParams(const ZfCf::AnyNode *node) {
  return ZvThreadParams(node, ZmThreadParams{});
}
```

| Header | Binding |
| --- | --- |
| `ZvThreadParams.hh` | Stack size, priority, partition and CPU set |
| `ZvMxParams.hh` | Scheduler/thread parameters, Rx/Tx destinations, connection options and multiplexer settings |
| `ZvRingParams.hh` | In-process/shared-memory ring parameters |
| `ZvStackParams.hh` | Stack configuration |

Use [ZvMxParams.hh](src/ZvMxParams.hh) for the supported field names, bounds
and nested scheduler layout rather than assuming every fluent parameter has a
configuration field. Null-node handling in an adapter preserves its defaults.

## TOML and YAML

`ZvTOML::load(path, limits)` and `ZvYAML::load(path, limits)` return file-size/tree
tuples through their respective codecs. Pass the matching tree to `ZfTOML` or
`ZfYAML` handlers; a native `Cf` tree is not a universal interchange format for
all three loaders. Declare the corresponding `TOML` or `YAML` facet on reflected
types.

`ZvTOML::save(path, object)`, `ZvYAML::save(path, object)` and `ZvCf::save`
write reflected objects through a file-backed transmit stream. `saveUpd` and
`saveDel` select update/delete field filters; these are filtered serializations,
not in-place edits to an existing file. The save path opens output for writing
and flushes it, reporting file/output failures as exceptions. It does not
promise a transactional temporary-file-and-rename update.

See [TOML tests](test/ZvTOMLTest.cc), [YAML tests](test/ZvYAMLTest.cc), and the
[TOML coverage audit](test/TOML-docs/AUDIT.md) for supported syntax and limits.
The `interop/` directory compares TOML output against an independent parser.

## Examples and tests

Run `make -C zv test` from a configured build tree. The [test directory](test/)
contains executable API examples and boundary cases. See the top-level
[testing guide](docs/TESTING.md) for environment, diagnostics and port allocations,
and [dependencies](docs/DEPENDENCIES.md) for build prerequisites.
