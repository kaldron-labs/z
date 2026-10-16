`ZtlsVault` dependent apps can be configured to choose a `VaultMode` from among `Ephemeral`, `File`, `KeyRing`, `Module` and `Auto`.

All `VaultMode` options are implemented via the same base class API defined by pure virtual member functions.

If `VaultMode` is `Module`, a `module` path indicates the `.so` (Linux) or `.dll` (Windows).

- `Module` is a `Ztls::Vault` dynamic-loading module implementing the same back-end API as the other options. It resolves an instance of the base class API from the loaded module using a well-known symbol, in the usual way.

- `KeyRing` is the O.S./platform native credential store, i.e. Windows: Credential Manager or Linux: D-BUS Secrets Manager. Depending on the platform, one of these is built-in and bundled.

Due to Windows credential store blob size limits, there are two sub-variants of `KeyRing`:
- `Direct`, which directly stores the whole blob in the platform's credential store (this is
used under Linux with the D-BUS secrets manager), and
- `Secrets`, which stores the blob in an encrypted file that is decrypted using the secret
stored in the platform's credential store.
- On Windows, `Secrets` is auto-selected for `KeyRing` and cannot be overridden by app configuration
- On other OS, `Direct` is the default, but `Secrets` can be used instead
