picotls has been amended to provide hooks for buffer allocation and free:
- hooks are ptls_buffer_alloc and ptls_buffer_free
- picotls implementation source is in ../picotls/lib
  - picotls installed headers are in `/usr/include/picotls.h` and `/usr/include/picotls/*.h`
  - this is a local fork that is different than the canonical open source version
- we can use these hooks to improve the zero-copy implementation and eliminate the need for HandshakeBufSize
- examine ptls_buffer_alloc and ptls_buffer_free definitions and default implementations
- the overall approach:
  - the buffer provided to picotls will set origin set to the owning Zi::IOBuf following ptls_buffer_init()
    - pseudo-code:
    ```
    ZmRef<ZiIOBuf> zbuf = new IOBufAlloc{...};
    ptls_buffer_t buf;
    ptls_buffer_init(&buf, zbuf->data(), zbuf->size);
    buf.origin = zbuf.ptr();
    ```
  - in the ptls_buffer_alloc hook, Zi::IOBuf::ensure(new_capacity) will be used to re-allocate the buffer
    - pseudo-code:
    ```
    if (!buf->origin) { // picotls internal
        // same code as default ptls_buffer_alloc, except use Zi::IOBuf::valloc and vfree instead of malloc and free
    } else { // application - origin points to ZiIOBuf
        ZiIOBuf *zbuf = static_cast<ZiIOBuf *>(buf->origin);
        // ...
        zbuf->ensure(new_capacity);
        // ...
    }
    ```
  - in the ptls_buffer_free hook, Zi::IOBuf::free() will be used to free the buffer
    - pseudo-code:
    ```
    if (!buf->origin) { // picotls internal
        // same code as default ptls_buffer_alloc, except use Zi::IOBuf::vfree instead of free
    } else { // application - origin points to ZiIOBuf
        ZiIOBuf *zbuf = static_cast<ZiIOBuf *>(buf->origin);
        // ...
        zbuf->free(buf->base);
        // ...
    }
    ```
- GOAL: take advantage of these new hooks to make the ztls zero-copy code simpler and more resilient
- IMPORTANT: DO NOT use canonical online references for picotls; this is using a local fork
