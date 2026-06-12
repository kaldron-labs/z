`http3_plan.md` is a plan to unify the `Zhttp` API so that apps can use HTTP without
concerning themselves with the HTTP protocol version. `Zhttp` should prefer HTTP/3 via
`Zquic`, falling back to HTTP 1.1 via `Ztls`.

`Zhttp` `Headers` is a generic `ZuMatcher` header key handler that leverages `ZuMatcher`'s
compile-time Aho-Corasick to efficiently and incrementally parse header fields and make them
available to applications without copying or memory allocation. Header keys in the buffer
are modified in place to be consistently-cased so `ZuMatcher` can be used. Applications
can extend the default compile-time set of header keys that are matched by passing a
`ZuStringTL` of `Keys` into `Request` or `Response`. Keys and values are not copied,
and no memory allocation is performed during parsing - buffers are modified in-place
as needed and key and value extraction uses spans (slices) of the buffer data without copying.
This efficiency should be preserved for both HTTP 1.1 and HTTP/3.

`http3_plan.md` must require this principle.

HTTP/3 QPACK support:
- `QPackKVs` is a `ZuStringTL<...>`
- `QPackKV2ID` is a lookup `ZuTypeList<ZuUnsigned<X>, ...>`
  where X is the QPACK static table index (ID) for the corresponding QPackKV string
- `QPackID2KV` is a `ZuTypeList<ZuUnsigned<X>, void, ...>`, which maps QPACK IDs back to kv
- `QPackKeys` is a `ZuStringTL<...>`
- `QPackKey2ID` is a lookup `ZuTypeList<ZuUnsigned<X>, ...>,`
  where X is the QPACK static table index (ID) for the corresponding QPackKey string
- `QPackID2Key` is a `ZuTypeList<ZuUnsigned<X>, void, ...>`, which maps QPACK IDs back to key
- while on-the-wire data will remain in `ZiIOBuf`, temporary uncompressed data should be
  predominantly on-stack; see `ZuBase64Test.cc` `enc()` for an example of encoding to
  a temporary on-stack buffer
- since `Zhttp` is above `Zt`, use `ZtLocalArray` for on-stack arrays to stage
  temporary uncompressed data (either decoded from network buffers, or being encoded
  to network buffers);
-  the goal is to reduce heap memory allocation to a minimum, and potentially eliminate
  `HeaderBytes` entirely; apps should interface with `Zhttp` via inversion-of-control callback
  mechanisms, where `Zhttp` decodes/uncompresses to on-stack temporary storage then calls
  the app with the data

HTTP/3 QPACK parse pseudocode:
```
// parse QPACK, obtain QPACK static table id
if (/* QPACK literal */) {
  // process literal
  // use ZtLocalArray to decode to stack-allocated strings, then call app with that
  // as with HTTP/1.1
} else /* QPACK static table ID */ {
  ZuSwitch::dispatch<...>(id, [...](auto ID) {
    using KV = ZuType<ID, QPackID2KV>;
    if constexpr (!ZuIsSame<KV, void>{}) {
      // call app kv(...)
    }
    using Key = ZuType<I, QPackID2Key>;
    if constexpr (!ZuIsSame<Key, void>{}) {
      // use ZtLocalArray to decode to stack-allocated string, then call app with that
    }
  }
}
```

HTTP/3 QPACK build:
- compile-time determination of which KVs and Keys should be QPACK static table encoded
  on transmit (ZuTypeIndex<QPackKVs, KV>{} and ZuTypeIndex<QPackKeys, Key>{})
