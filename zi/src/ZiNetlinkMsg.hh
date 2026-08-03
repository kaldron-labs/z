// -*- mode: c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
// vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i4

#ifndef ZiNetlinkMsg_HH
#define ZiNetlinkMsg_HH

#include <stdint.h>

#include <ZiNetlinkC.h>

#include <zlib/ZuAssert.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZiPlatform.hh>

class ZiConnection;

class ZiNetlinkHdr {
  ZiNetlinkHdr(const ZiNetlinkHdr &);
  ZiNetlinkHdr &operator =(const ZiNetlinkHdr &);

public:
  enum { PADDING = NLMSG_HDRLEN - sizeof(struct nlmsghdr) };

  ZuInline uint32_t hdrSize() const { return NLMSG_HDRLEN; }
  ZuInline uint32_t len() const { return m_n.nlmsg_len; }
  ZuInline uint32_t dataLen() const { return m_n.nlmsg_len - NLMSG_HDRLEN; }

  ZuInline uint16_t type() const { return m_n.nlmsg_type; }
  ZuInline uint16_t flags() const { return m_n.nlmsg_flags; }
  ZuInline uint32_t seq() const { return m_n.nlmsg_seq; }
  ZuInline uint32_t pid() const { return m_n.nlmsg_pid; }

  ZuInline struct nlmsghdr *hdr() { return &m_n; }

  template <typename S> void print(S &s) const {
    s << "ZiNetlinkHdr [[len = " << m_n.nlmsg_len
      << "] [type = " << m_n.nlmsg_type
      << "] [flags = " << m_n.nlmsg_flags
      << "] [seqNo = " << m_n.nlmsg_seq
      << "] [pid = " << m_n.nlmsg_pid
      << "] [hdrSize = " << hdrSize()
      << "] [dataLen = " << dataLen()
      << "] [size = " << hdrSize()
      << "]]";
  }
  friend ZuPrintFn ZuPrintType(ZiNetlinkHdr *);

protected:
  ZiNetlinkHdr() { memset(&m_n, 0, sizeof(struct nlmsghdr)); }

  ZiNetlinkHdr(uint32_t len, uint16_t type,
      uint16_t flags, uint32_t seqNo, uint32_t portID) :
    m_n{(len + NLMSG_HDRLEN), type, flags, seqNo, portID} { }

private:
  struct nlmsghdr		m_n;
  char				m_pad[PADDING];
};

class ZiGenericNetlinkHdr : public ZiNetlinkHdr {
  ZiGenericNetlinkHdr(const ZiGenericNetlinkHdr &);
  ZiGenericNetlinkHdr &operator =(const ZiGenericNetlinkHdr &);

public:
  enum { PADDING = GENL_HDRLEN - sizeof(struct genlmsghdr) };

  ZiGenericNetlinkHdr() { memset(&m_g, 0, sizeof(struct genlmsghdr)); }

  ZiGenericNetlinkHdr(uint32_t len, uint16_t type, uint16_t flags,
		      uint32_t seqNo, uint32_t portID, uint8_t cmd) :
    ZiNetlinkHdr(GENL_HDRLEN + len, type, flags, seqNo, portID) :
      m_g{cmd, ZiGenericNetlinkVersion, 0} { }

  ZiGenericNetlinkHdr(ZiConnection *connection, uint32_t seqNo, uint32_t len);

  ZuInline uint8_t cmd() const { return m_g.cmd; }
  ZuInline uint8_t version() const { return m_g.version; }
  ZuInline uint32_t hdrSize() const { 
    return GENL_HDRLEN + ZiNetlinkHdr::hdrSize(); 
  }

  template <typename S> void print(S &s) const {
    s << "ZiGenericNetlinkHdr [" << static_cast<const ZiNetlinkHdr &>(*this)
      << " [cmd = " << m_g.cmd
      << "] [version = " << m_g.version
      << "] [reserved = " << m_g.reserved
      << "] [size = " << hdrSize()
      << "]]";
  }
  friend ZuPrintFn ZuPrintType(ZiGenericNetlinkHdr *);

private:
  struct genlmsghdr	m_g;
  char			m_pad[PADDING];
};
#define ZiGenericNetlinkHdr2Vec(x) (void *)&(x), x.hdrSize()

// Netlink Attributes

/* from netlink.h:
 *
 *  <------- NLA_HDRLEN ------> <-- NLA_ALIGN(payload)-->
 * +---------------------+- - -+- - - - - - - - - -+- - -+
 * |        Header       | Pad |     Payload       | Pad |
 * |   (struct nlattr)   | ing |                   | ing |
 * +---------------------+- - -+- - - - - - - - - -+- - -+
 *  <-------------- nlattr->nla_len -------------->
 * NLA_HDRLEN				== hdrLen()
 * nlattr->nla_len			== len()
 * NLA_ALIGN(payload) - NLA_HDRLEN	== dataLen()
 * NLA_HDRLEN + NLA_ALIGN(payload)	== size()
 */
class ZiNetlinkAttr {
  ZiNetlinkAttr(const ZiNetlinkAttr &);
  ZiNetlinkAttr &operator =(const ZiNetlinkAttr &);

public:
  enum { PADDING = 0 }; // NLA_HDRLEN - sizeof(struct nlattr) };

  ZiNetlinkAttr() { memset(this, 0, sizeof(ZiNetlinkAttr)); }

  ZuInline uint16_t hdrLen() const { return NLA_HDRLEN; }
  ZuInline uint16_t len() const { return m_na.nla_len; }
  ZuInline uint16_t dataLen() const { return len() - hdrLen(); }
  ZuInline uint16_t size() const { return hdrLen() + NLA_ALIGN(dataLen()); }
  ZuInline uint16_t type() const { return m_na.nla_type; }

  ZuInline char *data_() { return ((char *)&m_na); }
  ZuInline char *data() { return ((char *)&m_na) + hdrLen(); }
  ZuInline const char *data() const { return ((const char *)&m_na) + hdrLen(); }
  ZuInline ZiNetlinkAttr *next() {
    return (ZiNetlinkAttr *)((char *)&m_na + size());
  }

  template <typename S> void print(S &s) const {
    s << "ZiNetlinkAttr [[len = " << m_na.nla_len
      << "] [type = " << m_na.nla_type
      << "] [hdrLen = " << hdrLen()
      << "] [dataLen = " << dataLen()
      << "] [size = " << size()
      << "]]";
  }
  friend ZuPrintFn ZuPrintType(ZiNetlinkHdr *);
		
protected:	   
  ZiNetlinkAttr(uint16_t type, uint16_t len) :
    m_na{NLA_HDRLEN + PADDING + len, type} { }

private:
  struct nlattr		m_na;
  char			m_pad[PADDING];
};

class ZiNetlinkFamilyName : public ZiNetlinkAttr {
  enum { PADDING = NLA_ALIGN(GENL_NAMSIZ) - GENL_NAMSIZ };

public:
  ZiNetlinkFamilyName(ZuCSpan s) :
    ZiNetlinkAttr(CTRL_ATTR_FAMILY_NAME,
	s.length() >= GENL_NAMSIZ ? GENL_NAMSIZ : (s.length() + 1)) {
    unsigned len = s.length() >= GENL_NAMSIZ ? GENL_NAMSIZ : (s.length() + 1);
    memcpy(m_familyName, s.data(), len - 1);
    m_familyName[len] = 0;
  }

  template <typename S> void print(S &s) const {
    s << "ZiNetlinkFamilyName [" << static_cast<const ZiNetlinkAttr &>(*this)
      << " [familyName = " << m_familyName << "]]";
  }
  friend ZuPrintFn ZuPrintType(ZiNetlinkFamilyName *);

private:
  char m_familyName[GENL_NAMSIZ];
  char m_pad[PADDING];
};

class ZiNetlinkDataAttr : public ZiNetlinkAttr {
public:
  ZiNetlinkDataAttr(int len) : ZiNetlinkAttr(ZiGNLAttr_Data, len) { }
};
#define ZiNelinkDataAttr2Vec(x) (void *)x.data_(), x.hdrLen()

template <typename T, uint16_t AttrType>
class ZiNetlinkAttr_ : public ZiNetlinkAttr {
  enum { PADDING = NLA_ALIGN(sizeof(T)) };

public:
  ZiNetlinkAttr_(const T &v) : ZiNetlinkAttr(AttrType, sizeof(T)) {
    m_data = v;
  }

  const T &data() const { return m_data; }

private:
  T	m_data;
  char	m_pad[PADDING];
};

using ZiNetlinkOpCodeAttr = ZiNetlinkAttr_<uint16_t, ZiGNLAttr_PCI>;

#endif /* ZiNetlinkMsg_HH */
