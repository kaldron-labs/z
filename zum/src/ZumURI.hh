//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Shared application-scoped OAuth URI mappings

#ifndef ZumURI_HH
#define ZumURI_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumTypes.hh>

#include <zlib/ZfURI.hh>

namespace Zum {

struct AppIDPath {
  AppID appID = 0;
};
ZfStruct(, (AppIDPath, URI),
  (((appID), (URI::PathIndex<0>, Required)),	UInt64));

struct AppIssuerPath {
  ZuCSpan oauth2;
  AppID appID = 0;
};
ZfStruct(, (AppIssuerPath, URI),
  (((oauth2),	(URI::PathIndex<0>, Required)),	String),
  (((appID),	(URI::PathIndex<1>, Required)),	UInt64));

struct AppEndpointPath {
  ZuCSpan oauth2;
  AppID appID = 0;
  ZuCSpan group;
  ZuCSpan endpoint;
};
ZfStruct(, (AppEndpointPath, URI),
  (((oauth2),	(URI::PathIndex<0>, Required)),	String),
  (((appID),	(URI::PathIndex<1>, Required)),	UInt64),
  (((group),	(URI::PathIndex<2>, Required)),	String),
  (((endpoint),	(URI::PathIndex<3>, Required)),	String));

struct AppNestedEndpointPath {
  ZuCSpan oauth2;
  AppID appID = 0;
  ZuCSpan group;
  ZuCSpan section;
  ZuCSpan endpoint;
};
ZfStruct(, (AppNestedEndpointPath, URI),
  (((oauth2),	(URI::PathIndex<0>, Required)),	String),
  (((appID),	(URI::PathIndex<1>, Required)),	UInt64),
  (((group),	(URI::PathIndex<2>, Required)),	String),
  (((section),	(URI::PathIndex<3>, Required)),	String),
  (((endpoint),	(URI::PathIndex<4>, Required)),	String));

struct AppOIDCMetadataPath {
  ZuCSpan oauth2;
  AppID appID = 0;
  ZuCSpan wellKnown;
  ZuCSpan endpoint;
};
ZfStruct(, (AppOIDCMetadataPath, URI),
  (((oauth2),	(URI::PathIndex<0>, Required)),		String),
  (((appID),	(URI::PathIndex<1>, Required)),		UInt64),
  (((wellKnown),	(URI::PathIndex<2>, Required)),	String),
  (((endpoint),	(URI::PathIndex<3>, Required)),		String));

struct AppOAuthMetadataPath {
  ZuCSpan wellKnown;
  ZuCSpan endpoint;
  ZuCSpan oauth2;
  AppID appID = 0;
};
ZfStruct(, (AppOAuthMetadataPath, URI),
  (((wellKnown),	(URI::PathIndex<0>, Required)),	String),
  (((endpoint),	(URI::PathIndex<1>, Required)),		String),
  (((oauth2),	(URI::PathIndex<2>, Required)),		String),
  (((appID),	(URI::PathIndex<3>, Required)),		UInt64));

} // namespace Zum

#endif /* ZumURI_HH */
