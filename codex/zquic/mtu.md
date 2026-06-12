• The “socket fragmentation controls” issue is: QUIC must keep UDP datagrams from being fragmented
  at IP layer, but the socket knobs for doing that are platform-specific.

  QUIC’s rule is clear: UDP datagrams must not be IP-fragmented, and IPv4 should set DF where
  possible. Anything above 1200 bytes should be governed by QUIC PMTUD/DPLPMTUD, not by accidental
  kernel fragmentation. See RFC 9000 section 14: https://www.ietf.org/rfc/rfc9000.html#section-14

  On current Linux kernels, this is workable and fairly rich:

  - IPv4: setsockopt(IPPROTO_IP, IP_MTU_DISCOVER, ...)
  - IPv6: setsockopt(IPPROTO_IPV6, IPV6_MTU_DISCOVER, ...)
  - Useful modes: IP_PMTUDISC_DO / IPV6_PMTUDISC_DO, or *_PROBE when Zquic wants DPLPMTUD to be
    the authority and still send DF probes above the kernel’s cached PMTU.

  - IP_RECVERR / IPV6_RECVERR plus MSG_ERRQUEUE can expose ICMP Packet Too Big / fragmentation-
    needed info, including discovered MTU on Linux.

  - IP_MTU / IPV6_MTU can query known PMTU, but only on connected sockets.

  Linux docs: https://www.man7.org/linux/man-pages/man2/IP_MTU_DISCOVER.2const.html and
  https://www.man7.org/linux/man-pages/man2/IP_RECVERR.2const.html

  On Windows 11+ through MinGW/MSYS2, the basic controls also exist, but they are Winsock
  controls, not Linux-compatible ones:

  - IPv4: IP_DONTFRAGMENT, IP_MTU_DISCOVER, IP_MTU, IP_USER_MTU
  - IPv6: IPV6_MTU_DISCOVER, IPV6_MTU, IPV6_USER_MTU; IPV6_DONTFRAG exists in current mingw-w64
    headers.

  - IP_MTU_DISCOVER = IP_PMTUDISC_DO makes oversized datagram sends fail instead of fragmenting.
  - IP_PMTUDISC_PROBE is available in current mingw-w64 headers and Windows docs for PMTUD state,
    but the enum values differ from Linux, so portable code must never share raw numeric
    constants.

  - IP_MTU / IPV6_MTU also require a connected socket.
  - There is no Linux-style portable MSG_ERRQUEUE model to build the design around.

  Windows docs: https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options
  and https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ipv6-socket-options

  So the plan item means: add a small Zquic platform helper that tries to disable fragmentation /
  enable PMTU behavior per socket family, logs unsupported cases, and keeps Zquic’s own per-path
  DPLPMTUD as the source of truth. On Linux, use the richer PMTU/error-queue support. On Windows/
  MinGW, use Winsock’s DF/PMTUD options and treat send errors like WSAEMSGSIZE as local MTU
  feedback. In both cases, Zquic should still packetize conservatively and never depend on OS
  fragmentation.

› does DPLPMTUD require connected UDP sockets?

• No.

  DPLPMTUD does not require connected UDP sockets. It works at the packetization layer: Zquic
  sends QUIC probe packets of candidate sizes, observes whether those QUIC packets are
  acknowledged or lost, and adjusts the per-path maximum datagram size.

  Connected UDP is only useful for OS PMTU conveniences:

  - Linux IP_MTU / IPV6_MTU queries require a connected socket.
  - Windows IP_MTU / IPV6_MTU also require a connected socket.
  - Linux unconnected sockets can still receive PMTU/error-queue updates with IP_RECVERR, but
    Zquic would need to validate and route those errors itself.

  For Zquic, the right model is:

  - Server: use unconnected UDP, because one socket serves many peers.
  - Client: connected UDP is optional, but not required.
  - DPLPMTUD state lives per path/link: local address, remote address, ports, possibly interface,
    and QUIC connection/path state.

  - Kernel PMTU/ICMP info is advisory input only; QUIC ACK/loss of PMTU probes remains
    authoritative.
