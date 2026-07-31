# Separation of Concerns, Encapsulation of HTTP Transport Functionality
- `zhttpclient` implements transport-related and payload-related code together
- the goal of this work is to:
  - factor out transport-related code into new library headers `ZhttpH1.hh` and `ZhttpH3.hh`
    (with any non-template implementation code in `.cc`)
  - permit applications like `zhttpclient` to focus on messaging (payloads)
    - "payload" in this context includes transport-agnostic HTTP header and body handling

## Separation of Concerns into Layers
- Link/Engine related code should be distinguished from application payload code
- Application messaging is at a higher layer than HTTP related code
- `Zhttp` should isolate the following, so `zhttpclient` does not have to
  implement transport-specific handling and can focus on transport-agnostic
  message payload handling:
  - `Parser`:
    ```
    void reset();
    void operation(Method::T method, ZuBSpan path);
    void status(unsigned);
    template <typename Key> void header(ZuBSpan value);
    template <typename Key, typename Value> void header();
    void contentLength(uint64_t);
    void xferCompression(XferCompression::T);
    void chunked();
    void body(ZuBSpan);
    ```
  - `Builder`:
    ```
    void reset();
    void operation(Method::T method, ZuBSpan path);
    void status(unsigned);
    template <typename Key> void header(ZuBSpan value);
    template <typename Key, typename Value> void header();
    void contentLength(uint64_t);
    void body(ZuBSpan);
    void complete(ParserState::T); // NOTE - for the app, this should always be "end of message", not "end of stream"
    ```
- `Zhttp.hh` can depend on `ZhttpH1.hh` and `ZhttpH3.hh`for the protocol-specific code that it
  currently implements:
  - `H1` code can be isolated into `ZhttpH1.hh`
  - `H3` code can be isolated into `ZhttpH3.hh`
  - `Zhttp.hh` can include both `ZhttpH1.hh` and `ZhttpH3.hh`
- `ZhttpH1.hh`/`ZhttpH3.hh` should add template `Link`/`Engine` etc. (the app will
  fill out application-level functionality with CRTP), and wrap up the
  following code from `zhttpclient` in a coherent template library that `zhttpclient` can use:
  - `State` needs to be split:
    - `zhttpclient` application-level members:
      ```
      URL	    	url;
      Options   	options;
      Protocol::T	protocol = Protocol::H1;
      ZtString<>	location;
      unsigned  	status = 0;
      ZiFile    	bodyFile;
      int64_t   	contentLength = -1;
      uint64_t  	bodyBytes = 0;
      unsigned  	bodyChunks = 0;
      bool	    	bodyFileOpen = false;
      bool	    	chunked = false;
      bool	    	redirect = false;
      bool	    	framingLogged = false;
      bool	    	done = false;
      bool	    	failed = false;
      ```
    - everything else is transport-level
  - `H3` code:
    - `openH3LocalStreams`
    - `QUICClient`
    - `resolveForQUIC`
  - `H1`/`H3` code:
    - `CliLink`
  - transport-related functions that `zhttpclient` can call, but they
    are probably not ideally named or factored; they implement behavior that applications
    will want to control, so need to be made available by `ZhttpH1.hh`/`ZhttpH3.hh`:
    - `runH3DNSFirst`
    - `runH1AltSvcFirst`

## Acceptance Criteria
- `zhttpclient` code volume is significantly reduced
- Most HTTP protocol-specific handling is implemented in `ZhttpH1.hh` or `ZhttpH3.hh`:
  - H1 specific in `ZhttpH1.hh`
  - H3 specific in `ZhttpH3.hh`
- `zhttpclient` continues to depend on `Zhttp.hh` for the application-level abstractions
  it needs to get it's job done; it focuses on specific request/response code (the `run`
  functions), overall application startup/shutdown, configuration, CLI options, etc.
  - it shold not implement:
    - `openH3LocalStreams`
    - `CliLink`
    - `QUICClient`
    - `resolveForQUIC`
  - Note: these are transport-related functions that `zhttpclient` can call, but they
    are probably not ideally named or factored; they implement behavior that applications
    will want to control, so need to be made available by `Zhttp.hh`
    - `runH3DNSFirst`
    - `runH1AltSvcFirst`
