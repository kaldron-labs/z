# Dashboard OAuth integration test

`make test` starts a loopback OAuth fixture, seeds a disposable Vault, and runs
the real `ZDashOAuth::run` flow twice. It checks that each run refreshes the
stored credential without opening a browser and persists the rotation for the
next process.
