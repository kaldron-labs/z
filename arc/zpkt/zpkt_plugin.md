- `Zpkt::Plugin`:
  - encapsulates:
    - DPDK global state including EAL
    - global state needed for AF_XDP
  - virtual base class
  - returned from dynamically-loaded module entry point
  - passed a configuration
  - uses `ZfCf` and `ZfStruct` to scan and load the configuration

- `AF_XDP` and `DPDK` will implement `Plugin` in separate shared libraries / DLLs
  - AF_XDP - `libZpkt_xdp.so`
  - DPDK - `libZpkt_dpdk.so`
  - `AF_XDP` library and code will be moved to `zpkt/xdp`
  - `DPDK` library and code will be moved to `zpkt/dpdk`

- plugins will be loaded dynamically at runtime by `Zpkt::Env`

- plugin initialization and configuration will be from `ZfCf` (e.g. EAL for DPDK)

- `Zpkt::PluginFn` is defined in `ZpktPlugin.hh`:
```
namespace Zpkt {

struct Plugin : public ZmPolymorph { ... };

// module entry point
typedef Plugin *(*PluginFn)();

}

extern "C" {
  typedef Zpkt::PluginFn ZpktPluginFn;
}

#define ZpktPluginFnSym	"ZpktPlugin"	// module symbol
```

- `Zpkt` environment initialization (called by `zutcp`) looks like this:
  - `Zpkt::Env::init(const ZfCf::AnyNode *cf)`
  - `PluginLoadCf` is:
  ```
  ZuDerive(CfString, (ZtString<ZtStringHeapID<"Zpkt.CfString">>));
  struct PluginLoadCf {
    CfString	module;
  };
  ZfStruct(ZpktAPI, (PluginLoadCf, Cf),
    (module, (Mutable, Required),		String));
  ```
  - `Zpkt::Env::m_plugin` is declared `ZmRef<Plugin>    m_plugin;`
  - `Zpkt::Env::init` looks like this:
  ```
  void Zpkt::Env::init(const ZfCf::AnyNode *cf) {
    PluginLoadCf pluginCf;
    ZfCf::handler<PluginLoadCf>(cf).update(pluginCf);
    ZiModule module_;
    auto &path = pluginCf.module;
    ZeString e; // dlerror() returns a string
    if (module_.load(path, pluginCf.preload ? ZiModule::Pre : 0, &e) < 0)
      throw ZeEXCEPT(Fatal, "Zpkt", ([path = ZeString{path}, e](auto &s) {
	s << "failed to load \"" << path << "\": " << e; }));
    auto pluginFn =
      reinterpret_cast<PluginFn>(module_.resolve(ZpktPluginFnSym, &e));
    if (!pluginFn) {
      module_.unload();
      throw ZeEXCEPT(Fatal, "Zpkt", ([path = ZeString{path}, e](auto &s) {
	s << "failed to resolve \"" ZpktPluginFnSym "\" in \""
	  << path << "\": " << e; }));
    }
    m_plugin = (*pluginFn)();
    m_plugin->init(cf); // can throw
  }
  ```

- `Zpkt::Instance` is renamed to `Zpkt::Nif` (for network interface)
   and becomes a virtual base class

- `Zpkt::Env::nif(ZuCSpan name)` is defined `Nif *nif(ZuCSpan name) { return m_plugin->nif(name); }`
  - `Zpkt::Plugin::nif(ZuCSpan name)` is declared `virtual Nif *nif(ZuCSpan name) = 0;`

- `Env::final` (called by `zutcp`) calls `m_plugin->final()`

- `virtual ... Plugin::init(const ZfCf::AnyNode *cf)` is passed a
   configuration used to load a `ZfStruct`-described plugin configuration, including
   all network interfaces to be used subsequently
  - the configuration passed to `Env::init` that is passed onto `Plugin::init` should include:
    ```
    interfaces {
      foo0 { ... }
      foo1 { ... }
      ...
    }
    ```
    where `foo0` and `foo1` are network interface names that map to the interface-specific configurations for each
  - for DPDK an example skeleton could be:
    ```
    module .../libZpkt_dpdk.so
    eal {
      lcores ...
      ...
    }
    interfaces {
      veth0 { // "veth0" is the name
        spin 1000
        rxInterval 50
      }
    }
    ```
  - IMPORTANT `ifindex` is not specified for either AF_XDP or DPDK - the name is used to lookup the interface
  - IMPORTANT `name` is not specified as "name X" - it is the key
