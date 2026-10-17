//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP application profile selection

#ifndef ZhttpProfiles_HH
#define ZhttpProfiles_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZhttpCore.hh>

namespace Zhttp {

struct TCP { };
struct TLS { };
struct QUIC { };


template <typename Protocol_, int Version_>
struct Profile {
  using Protocol = Protocol_;
  enum {
    HTTPVersion = Version_,
    Multiplexed = Version_ != Version::H1
  };
};

using H1TCP = Profile<TCP, Version::H1>;
using H1TLS = Profile<TLS, Version::H1>;
using H2TLS = Profile<TLS, Version::H2>;
using H3QUIC = Profile<QUIC, Version::H3>;

template <typename> struct IsProfile : public ZuFalse { };
template <> struct IsProfile<H1TCP> : public ZuTrue { };
template <> struct IsProfile<H1TLS> : public ZuTrue { };
template <> struct IsProfile<H2TLS> : public ZuTrue { };
template <> struct IsProfile<H3QUIC> : public ZuTrue { };

struct ProfileDesc;
ZhttpAPI const ProfileDesc &h1TCP();
ZhttpAPI const ProfileDesc &h1TLS();
ZhttpAPI const ProfileDesc &h2TLS();
ZhttpAPI const ProfileDesc &h3QUIC();

template <typename> struct ProfileTraits;
template <> struct ProfileTraits<H1TCP> : H1TCP {
  using Profile = H1TCP;
  static const ProfileDesc &descriptor() { return h1TCP(); }
};
template <> struct ProfileTraits<H1TLS> : H1TLS {
  using Profile = H1TLS;
  static const ProfileDesc &descriptor() { return h1TLS(); }
};
template <> struct ProfileTraits<H2TLS> : H2TLS {
  using Profile = H2TLS;
  static const ProfileDesc &descriptor() { return h2TLS(); }
};
template <> struct ProfileTraits<H3QUIC> : H3QUIC {
  using Profile = H3QUIC;
  static const ProfileDesc &descriptor() { return h3QUIC(); }
};

template <typename List, typename = void> struct Profiles_;
template <typename ...P>
struct Profiles_<ZuTypeList<P...>,
    ZuIfT<(sizeof...(P) > 0) && (bool(IsProfile<P>{}) && ...)>> {
  using List = ZuTypeUnique<P...>;
  enum { N = List::N };

  template <typename Profile_>
  using Has = ZuTypeIn<Profile_, List>;

private:
  template <typename, typename, typename, typename> friend class Server;
  template <typename, typename, typename, typename> friend class Pool;

  // Control-thread setup. add() copies the descriptor handle into storage
  // sized for this selection before this synchronous registration returns.
  template <typename Core>
  static void configure(Core &core) {
    core.profiles(N);
    ZuUnroll::all<List>([&core]<typename P_>() {
      core.add(ProfileTraits<P_>::descriptor());
    });
  }
};

template <typename ...P>
using Profiles = Profiles_<ZuTypeList<P...>>;

using AllProfiles = Profiles<H1TCP, H1TLS, H2TLS, H3QUIC>;

} // namespace Zhttp

#endif /* ZhttpProfiles_HH */
