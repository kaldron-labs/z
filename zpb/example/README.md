# Zpb example

`zpbexample` builds `zpbexample.proto` with `zprotoc`, then exchanges one
message between generated `example::Note` and handwritten `HandNote` metadata.
The handwritten fields use implicit `ZfStruct`-order numbers; only the signed
repeated field needs `PB::ZigZag`. The generated string owns its bytes, while
the handwritten `ZuCSpan` borrows from the wire buffer during loading.

With the configured Clang debug build, build and run only this Zpb program:

```sh
make -C zpb/src -j4 zprotoc
make -C zpb/example -j4 zpbexample
./zpb/example/zpbexample
```

Successful output is `Zpb example OK`.
