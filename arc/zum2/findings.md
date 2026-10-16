4. **Static lookup tables implement enum-to-RAG mapping.**
   `zcmd/src/Ztel.hh:50-52`, `:61-63`, and `:98-100` use fixed `values[]`
   tables; `zdb_pq/src/ZdbPQ.cc:22` starts a fixed `names[]` OID table.
   These are the fixed-size lookup-table flag.  Prefer direct enum values,
   generated enum metadata, or a framework mapping that keeps the mapping and
   enum definition synchronized.

5. **Repository-owned code uses C stdio and printf-style formatting where Z
   I/O/formatting is available.**  Confirmed live occurrences include:

   - `zrest/itest/zrestmatrix.cc:276-279,375` (`printf`/`fwrite` for TAP and
     diagnostics);
   - `zdash/test/ZdashSchemaTest.cc:20-22` (`FILE`/`fopen` just to test schema
     presence);
   - `zf/test/ZfTOMLTest.cc:648-665` and
     `ztls/test/ZtlsBufHookTest.cc:249-268` (raw file fixture I/O);
   - `zi/itest/zringmatrix.cc:143-148` and
     `zquic/test/ZquicInteropTest.hh:69-80,247-266` (raw file diagnostic and
     interop I/O);
   - `zhttp/itest/zhttpmatrix.cc:750-782` and
     `zhttp/interop/zhttp3interoptest.cc:1001-1014`
     (`FILE` plus `fprintf` to create Caddy configuration);
   - `zproxy/src/zproxy.cc:720` (direct `fwrite`/`fflush` of command output).

   Replace ordinary repository I/O with `ZiFile` and construct output with Z
   stream formatting.  C-library calls required at an external API boundary
   (for example OpenSSL callback formatting and `ZmRandom`'s low-level
   `/dev/urandom` bootstrap) were not counted as findings.

6. **`thread_local` is used above the `zm` layer.**
   `ztls/src/ZtlsOpenSSL.cc:399` stores the ticket key in
   `ticket_key_tls_`.  Use `ZmSpecific`/`ZmTLS`, which provides controlled
   destruction and cross-module behavior.  The `zu`-layer `thread_local`
   formatters were not counted because `zu` cannot depend on `zm`.
