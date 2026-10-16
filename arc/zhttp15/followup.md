1. `Link` up/down API needs cleaning up:
- `bool up() const;`: returns if up
- `void up(bool v);`: sets up (`true`)/down (`false`)
- `isDown()`: delete and update callers

2. `Parser::version(ZuBSpan)` should be deleted:
- it's H1-only and unnecessary (it reports the "HTTP/1.1" string, which has no significance to protocol-neutral applications)

3. `Parser::xferCompression(...)` should probably be deleted, it's H1-only
- is it deprecated? if deprecated, delete it
- what is the mainstream use? if negligible, delete it
- is it ever used for REST APIs? if never used for REST APIs, delete it
- if needed (not deprecated, non-negligible use and sometimes used for REST APIs), it should be handled internally using on-the-fly decompression with scratch buffers

4. `Parser::headers(..., endStream)` looks wrong:
- why is the `endStream` parameter needed?
- is this intended to be called before `header` is called for each header in the section?
- why not just pass a section to `header` via an additional parameter?
- why is the enum named `Fields::Section` not `Headers::Section`?

5. `Parser::BodyMax` should be run-time `Server`/`Client`/`Pool` configuration:
- delete it

6. `Parser::chunked` and `Parser::contentLength` should be deleted:
- add `Parser::bodyInfo(BodyType::T, uint64_t length)`:
  - `BodyType` is a `ZtEnum` with `None, Streamed, Fixed`:
    - `Streamed`: used for H1 chunked or H2/H3 streamed (implies `!length`),
    - `None`: implies `!length`
    - `Fixed`: implies `length >= 0`
      - Note: `Fixed` and `length == 0` is a valid combination for an empty fixed body

7. `ZiMxLoopTest`:
- this test is flaky and occasionally hangs
- investigate, attempt to reproduce, diagnose and fix
