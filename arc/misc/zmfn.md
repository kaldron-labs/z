# ZmFn Heap ID Plan

## Goal

Enrich public and internal `using ...Fn = ZmFn<...>` delegate aliases with
explicit `ZmFnHeapID<"...">` parameters so stateful lambdas that spill through
`ZmLambda` allocate under meaningful, tunable heap IDs instead of the default
`"ZmFn"` heap.

`ZeMsgFn` in `ze/src/ZePlatform.hh` is the model:

```c++
using ZeMsgFn = ZmFn<
  void(ZeLogBuf &, const ZeEventInfo &),
  ZmFnHeapID<"ZeException">>;
```

## Rules

- Use the owning module/component as the heap ID prefix, matching nearby
  `ZtArrayHeapID`, `ZtStringHeapID`, `ZmHashHeapID`, and queue/list heap IDs.
- Prefer one heap ID per semantic callback family, not one catch-all per module,
  so allocation telemetry points back to the API surface that accepted the
  stateful lambda.
- Keep the signature unchanged and add only the named template parameter:
  `ZmFn<Sig, ZmFnHeapID<"Module.Component.Callback">>`.
- Where an alias is nested in a template whose owning container already exposes
  `HeapID`/`Sharded`, propagate those template heap properties with
  `ZmFnHeapID_<HeapID, ZmFnSharded<Sharded>>` rather than inventing a string.
- Where an alias signature embeds a raw `ZmFn<...>` parameter, first introduce a
  named alias for the embedded delegate so it can also receive a heap ID.
- Do not convert a template lambda parameter to a typed `ZmFn` earlier than the
  storage boundary. If the lambda can be moved as-is into a scheduler/ring post,
  preserve it as a forwarding reference and construct the `ZmFn` only where it
  is stored in a `ZmFn` field/container or an API explicitly requires that type.
- Template callback parameters use the local convention `template <typename L>`
  with parameter name `L &&l`. Typed `ZmFn` aliases are accepted by value as
  `fn` and then moved into storage/captures.
- Do not edit archived design notes under `arc/` as part of the code change.
- Tests/benchmarks can either keep default `ZmFn` where they deliberately test
  defaults, or use test-scoped heap IDs when the alias models ordinary callback
  usage.

## Proposed Changes

### ze

- `ze/src/ZePlatform.hh`
  - Already done: `ZeMsgFn` uses `ZmFnHeapID<"ZeException">`.

### ztcp / ztls / zquic

- `ztcp/src/Ztcp.hh`
  - `Ztcp::ErrorFn`: `ZmFnHeapID<"Ztcp.ErrorFn">`.
- `ztls/src/Ztls.hh`
  - `Ztls::ErrorFn`: `ZmFnHeapID<"Ztls.ErrorFn">`.
- `zquic/src/Zquic.hh`
  - `Zquic::ErrorFn`: `ZmFnHeapID<"Zquic.ErrorFn">`.
- `zquic/src/ZquicCliLink.hh`
  - Add `using CloseFn = ZmFn<void(), ZmFnHeapID<"ZquicCliLink.CloseFn">>;`
    and use it for `m_closeFn` and explicit close-completion construction.
  - Rename close-after-connection-close terminology to disconnect completion
    terminology and keep public/template close callbacks as `L &&l` until they
    cross into `CloseFn` storage.
- `zquic/src/Zquic_.hh`
  - Rename endpoint drain callbacks to disconnect callbacks:
    `EndpointDiscFn`, `EndpointDiscFns`, `m_discFns`, `takeDiscFns_()`,
    `discFns_()`, and `runDiscFns_()`.
  - Add `ZmFnHeapID<"Zquic.Endpoint.DiscFn">` to `EndpointDiscFn`.
- `zquic/src/ZquicCrypto.hh`
  - `CryptoStream::DequeueFn`: `ZmFnHeapID<"Zquic.Crypto.DequeueFn">`.
  - `CryptoStream::DeliveryFn`: `ZmFnHeapID<"Zquic.Crypto.DeliveryFn">`.
- `zquic/src/ZquicRecovery.hh`
  - `AckTracker::DequeueFn`: `ZmFnHeapID<"Zquic.Ack.DequeueFn">`.

### zi

- `zi/src/ZiEventLoop.hh`
  - `StartFn`: `ZmFnHeapID<"ZiEventLoop.StartFn">`.
  - `StopFn`: `ZmFnHeapID<"ZiEventLoop.StopFn">`.
  - `FailFn`: `ZmFnHeapID<"ZiEventLoop.FailFn">`.
  - `SocketSendFn`: `ZmFnHeapID<"ZiEventLoop.Socket.SendFn">`.
  - `SocketRecvFn`: `ZmFnHeapID<"ZiEventLoop.Socket.RecvFn">`.
  - `HandleWriteFn`: `ZmFnHeapID<"ZiEventLoop.Handle.WriteFn">`.
  - `HandleReadFn`: `ZmFnHeapID<"ZiEventLoop.Handle.ReadFn">`.
- `zi/src/ZiMultiplex.hh`
  - `FilterFn`: `ZmFnHeapID<"ZiMultiplex.FilterFn">`.
  - `ZiFailFn`: `ZmFnHeapID<"ZiMultiplex.FailFn">`.
  - `ZiListenFn`: `ZmFnHeapID<"ZiMultiplex.ListenFn">`.
  - `ZiConnectFn`: `ZmFnHeapID<"ZiMultiplex.ConnectFn">`.
  - `Zi_Overlapped::Executed`: `ZmFnHeapID<"ZiMultiplex.OverlappedFn">`.
- `zi/src/ZiResolver.hh`
  - `ResolveFn`: `ZmFnHeapID<"ZiResolver.ResolveFn">`.
  - `NameFn`: `ZmFnHeapID<"ZiResolver.NameFn">`.
  - `QueryFn`: `ZmFnHeapID<"ZiResolver.QueryFn">`.
  - `TxtFn`: `ZmFnHeapID<"ZiResolver.TxtFn">`.
- `zi/src/ZiIOContext.hh`
  - `ZiIOFn`: `ZmFnHeapID<"ZiIOFn">`.

### zcmd / ztel

- `zcmd/src/ZcmdHost.hh`
  - `Host::Fn`: `ZmFnHeapID<"Zcmd.Host.Fn">`.
  - Add `using FinalFn = ZmFn<void(), ZmFnHeapID<"ZcmdHost.FinalFn">>;`
    and use it for `finalFn()` and `m_finalFn`.
- `zcmd/src/ZcmdDispatcher.hh`
  - `Dispatcher::Fn`: `ZmFnHeapID<"Zcmd.Dispatcher.Fn">`.
  - `Dispatcher::DefltFn`: `ZmFnHeapID<"Zcmd.Dispatcher.DefltFn">`.
- `zcmd/src/ZtelClient.hh`
  - `Ztel::AckFn`: `ZmFnHeapID<"Ztel.Client.AckFn">`.
- `zcmd/src/zcmd.cc`
  - `Telcap::Fn`: `ZmFnHeapID<"zcmd.TelcapFn">`.

### zdb

- `zdb/src/Zdb.hh`
  - `Count__::Fn`: `ZmFnHeapID<"Zdb.Count.Fn">`.
  - `Select__::Fn`: `ZmFnHeapID<"Zdb.Select.Fn">`.
  - `Find__::Fn`: `ZmFnHeapID<"Zdb.Find.Fn">`.
  - Add `using InitTableFn = ZmFn<AnyTable *(DB *, TableCf *),
    ZmFnHeapID<"Zdb.InitTableFn">>;` and use it for `initTable_()`.
  - Add `using AllTableFn = ZmFn<void(bool), ZmFnHeapID<"Zdb.AllTableFn">>;`.
  - `DB::AllFn`: change the second parameter from raw `ZmFn<void(bool)>` to
    `AllTableFn`, then add `ZmFnHeapID<"Zdb.AllFn">`.
  - `DB::AllDoneFn`: `ZmFnHeapID<"Zdb.AllDoneFn">`.
  - Update call sites in `zdb/src/Zdb.cc` and `zcmd/src/ZtelServer.hh` that
    currently spell the inner done callback as `ZmFn<void(bool)>`.
- `zdb/src/ZdbStore.hh`
  - `FailFn`: `ZmFnHeapID<"Zdb.Store.FailFn">`.
  - `StartFn`: `ZmFnHeapID<"Zdb.Store.StartFn">`.
  - `StopFn`: `ZmFnHeapID<"Zdb.Store.StopFn">`.
  - `OpenFn`: `ZmFnHeapID<"Zdb.Store.OpenFn">`.
  - `CloseFn`: `ZmFnHeapID<"Zdb.Store.CloseFn">`.
  - `CountFn`: `ZmFnHeapID<"Zdb.Store.CountFn">`.
  - `TupleFn`: `ZmFnHeapID<"Zdb.Store.TupleFn">`.
  - `RowFn`: `ZmFnHeapID<"Zdb.Store.RowFn">`.
  - `CommitFn`: `ZmFnHeapID<"Zdb.Store.CommitFn">`.

### zdf

- `zdf/src/ZdfStore.hh`
  - `OpenFn`: `ZmFnHeapID<"Zdf.Store.OpenFn">`.
  - `OpenDFFn`: `ZmFnHeapID<"Zdf.Store.OpenDFFn">`.
  - `OpenSeriesFn`: `ZmFnHeapID<"Zdf.Store.OpenSeriesFn">`.
- `zdf/src/ZdfSeries.hh`
  - `ErrorFn`: `ZmFnHeapID<"Zdf.Series.ErrorFn">`.
  - `StopFn`: `ZmFnHeapID<"Zdf.Series.StopFn">`.
  - `Reader<Decoder>::Ctrl::Fn`: `ZmFnHeapID<"Zdf.Series.ReadFn">`.
  - `WriteFn`: `ZmFnHeapID<"Zdf.Series.WriteFn">`.
- `zdf/src/Zdf.hh`
  - `DataFrame::WriteFn`: `ZmFnHeapID<"Zdf.DataFrame.WriteFn">`.

### zgtk

- `zgtk/src/ZGtkApp.hh`
  - `DetachFn`: `ZmFnHeapID<"ZGtk.App.DetachFn">`.

### zrl

- `zrl/src/ZrlApp.hh`
  - `ErrorFn`: `ZmFnHeapID<"Zrl.App.ErrorFn">`.
  - `OpenFn`: `ZmFnHeapID<"Zrl.App.OpenFn">`.
  - `CloseFn`: `ZmFnHeapID<"Zrl.App.CloseFn">`.
  - `PromptFn`: `ZmFnHeapID<"Zrl.App.PromptFn">`.
  - `EnterFn`: `ZmFnHeapID<"Zrl.App.EnterFn">`.
  - `EndFn`: `ZmFnHeapID<"Zrl.App.EndFn">`.
  - `SigFn`: `ZmFnHeapID<"Zrl.App.SignalFn">`.
  - `CompSpliceFn`: `ZmFnHeapID<"Zrl.App.CompSpliceFn">`.
  - `CompIterFn`: `ZmFnHeapID<"Zrl.App.CompIterFn">`.
  - `CompInitFn`: `ZmFnHeapID<"Zrl.App.CompInitFn">`.
  - `CompStartFn`: `ZmFnHeapID<"Zrl.App.CompStartFn">`.
  - `CompSubstFn`: `ZmFnHeapID<"Zrl.App.CompSubstFn">`.
  - `CompNextFn`: `ZmFnHeapID<"Zrl.App.CompNextFn">`.
  - `CompFinalFn`: `ZmFnHeapID<"Zrl.App.CompFinalFn">`.
  - `HistFn`: `ZmFnHeapID<"Zrl.App.HistFn">`.
  - `HistSaveFn`: `ZmFnHeapID<"Zrl.App.HistSaveFn">`.
  - `HistLoadFn`: `ZmFnHeapID<"Zrl.App.HistLoadFn">`.
- `zrl/src/ZrlTerminal.hh`
  - `Terminal::ErrorFn`: `ZmFnHeapID<"Zrl.Terminal.ErrorFn">`.
  - `Terminal::OpenFn`: `ZmFnHeapID<"Zrl.Terminal.OpenFn">`.
  - `Terminal::CloseFn`: `ZmFnHeapID<"Zrl.Terminal.CloseFn">`.
  - `Terminal::StartFn`: `ZmFnHeapID<"Zrl.Terminal.StartFn">`.
  - `Terminal::KeyFn`: `ZmFnHeapID<"Zrl.Terminal.KeyFn">`.
- `zrl/src/ZrlEditor.hh`
  - `Editor::StartFn`: `ZmFnHeapID<"Zrl.Editor.StartFn">`.

### zum

- `zum/src/ZumServer.hh`
  - `OpenFn`: `ZmFnHeapID<"Zum.Server.OpenFn">`.
  - `BootstrapFn`: `ZmFnHeapID<"Zum.Server.BootstrapFn">`.
  - `ResponseFn`: `ZmFnHeapID<"Zum.Server.ResponseFn">`.
  - `SessionFn`: `ZmFnHeapID<"Zum.Server.SessionFn">`.
  - `LoginFn`: `ZmFnHeapID<"Zum.Server.LoginFn">`.

### zfb

- `zfb/src/Zfb.hh`
  - `Zfb::Load::LoadFn`: `ZmFnHeapID<"Zfb.LoadFn">`.

### zm

- `zm/src/ZmCache.hh`
  - `FindFn`: use the cache hash heap and sharding:
    `ZmFn<void(Node *), ZmFnHeapID<HeapID{}() + ".FindFn"_z, ZmFnSharded<Sharded>>>`.
- `zm/src/ZmPolyCache.hh`
  - `FindFn`: use the poly-cache heap:
    `ZmFn<void(Node *), ZmFnHeapID<HeapID{}() + ".FindFn"_z>>`.
  - If `PolyHash` also exposes `Sharded`, add the same
    `ZmFnSharded<Sharded>` propagation used by `ZmCache`.
- `zm/src/ZmHeap.hh`
  - `ZmHeapMgr::AllFn`: `ZmFnHeapID<"ZmHeapMgr.AllFn">`.
- `zm/src/ZmHashMgr.hh`
  - `ZmHashMgr::AllFn`: `ZmFnHeapID<"ZmHashMgr.AllFn">`.
- `zm/src/ZmScheduler.hh`
  - `WakeFn`: `ZmFnHeapID<"ZmScheduler.WakeFn">`.
  - `ThreadFn`: `ZmFnHeapID<"ZmScheduler.ThreadFn">`.
  - Keep scheduler entry points as `L &&l` and construct the internal ring
    `Fn` at the enqueue boundary.

### test aliases

- `zm/test/ZmFnTest.cc`
  - Keep `TestFn = ZmFn<void(int, int)>` if it is intentionally testing default
    behavior.
  - Add a separate test alias using `ZmFnHeapID<"ZmFnTest.TestFn">` if coverage
    for alias-level heap IDs is desired beyond the existing direct
    `ZmFn<void(), ZmFnHeapID<"Foo">>` test.

## Verification

1. Run `rg -n -U 'using\\s+[A-Za-z0-9_]*Fn\\s*=\\s*ZmFn\\s*<[^;]*;'`
   and confirm every source alias either has a `ZmFnHeapID` or is documented as
   a default-behavior test.
2. Run `rg -n 'ZmFn<void\\(bool\\)>|ZmFn<>|ZmFn<[^,>]+>' zdb/src zcmd/src`
   around the `DB::all()` call sites to catch any raw inner callback spelling
   that should become `AllTableFn`.
3. Run `rg -n 'finalFn\\(ZmFn<>\\)|ZtArray<ZmFn<>|m_closeFn\\s*=\\s*ZmFn<>|ZmFn<>\\s+m_closeFn' zcmd/src zquic/src`
   and confirm the remaining raw callback storage has been converted to
   `FinalFn` or `CloseFn`.
4. Run `rg -n 'template <typename Fn>|\\bFn &&fn_|\\bL l\\b'` over touched
   source paths and confirm remaining hits are not template callback parameters
   covered by the naming convention.
5. Build changed layers in dependency order, at minimum:
   - `make -C zm/src -j8`
   - `make -C ze/src -j8`
   - `make -C zi/src -j8`
   - `make -C zdb/src -j8`
   - `make -C zcmd/src -j8`
   - `make -C ztcp/src -j8`
   - `make -C ztls/src -j8`
   - `make -C zquic/src -j8`
   - `make -C zdf/src -j8`
   - `make -C zrl/src -j8`
   - `make -C zum/src -j8`
   - `make -C zfb/src -j8`
6. Run relevant tests after a successful build, or a top-level `make -j8` and
   `make test` if the branch is already configured for full verification.
   A top-level `make -j8` passed after the callback alias and naming changes.
