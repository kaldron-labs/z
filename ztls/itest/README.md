# Vault integration test

`ztlsvaultdbustest` starts an isolated session bus using the in-tree
`zdbus/util/zdbusbus` fixture, then serves Secret Service calls with
`ZdbusServer`. Run `make -C zdbus/util -j8` once, followed by
`make -C ztls/itest -j8 && make -C ztls/itest test` in the current build.
The test does not access the user's desktop keyring.
