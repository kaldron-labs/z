Vault identity has four levels: `program` → `account` → `scope` → `name`.
- `program` defaults to `ZiLog::program()`, i.e. the tail name of the process executable
- `account` defaults to `<user>@localhost`, where `<user>` is `Zi::username()`
- `scope` defaults to global
- `name` should be the purpose of the credential, e.g. `"oauth"`
