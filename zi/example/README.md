# ZiPB example

`zipbexample` builds `zipbexample.proto` with `zprotoc`, then exchanges one
message between generated `example::Note` and handwritten `HandNote` metadata.
The handwritten fields use implicit `ZfStruct`-order numbers; only the signed
repeated field needs `PB::ZigZag`. The generated string owns its bytes, while
the handwritten `ZuCSpan` borrows from the wire buffer during loading.

With the configured Clang debug build, build and run only this ZiPB program:

```sh
make -C zi/src -j4 zprotoc
make -C zi/example -j4 zipbexample
./zi/example/zipbexample
```

Successful output is `ZiPB example OK`.
