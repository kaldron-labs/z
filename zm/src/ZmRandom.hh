//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Mersenne Twister RNG
// - not suitable for cryptographic applications (use ZtlsRandom)

// modified version of
// http://www-personal.engin.umich.edu/~wagnerr/MersenneTwister.h

// MersenneTwister.h
// Mersenne Twister random number generator -- a C++ class ZmRandom
// Based on code by Makoto Matsumoto, Takuji Nishimura, and Shawn Cokus
// Richard J. Wagner  v1.0  15 May 2003  rjwagner@writeme.com

// The Mersenne Twister is an algorithm for generating random numbers.	It
// was designed with consideration of the flaws in various other generators.
// The period, 2^19937-1, and the order of equidistribution, 623 dimensions,
// are far greater.  The generator is also fast; it avoids multiplication and
// division, and it benefits from caches and pipelines.  For more information
// see the inventors' web page at http://www.math.keio.ac.jp/~matumoto/emt.html

// Reference
// M. Matsumoto and T. Nishimura, "Mersenne Twister: A 623-Dimensionally
// Equidistributed Uniform Pseudo-Random Number Generator", ACM Transactions on
// Modeling and Computer Simulation, Vol. 8, No. 1, January 1998, pp 3-30.

// Copyright (C) 1997 - 2002, Makoto Matsumoto and Takuji Nishimura,
// Copyright (C) 2000 - 2003, Richard J. Wagner
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//   1. Redistributions of source code must retain the above copyright
//	notice, this list of conditions and the following disclaimer.
//
//   2. Redistributions in binary form must reproduce the above copyright
//	notice, this list of conditions and the following disclaimer in the
//	documentation and/or other materials provided with the distribution.
//
//   3. The names of its contributors may not be used to endorse or promote
//	products derived from this software without specific prior written
//	permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER
// OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original code included the following notice:
//
//     When you use this, send an email to: matumoto@math.keio.ac.jp
//     with an appropriate reference to your work.
//
// It would be nice to CC: rjwagner@writeme.com and Cokus@math.washington.edu
// when you write.

#ifndef ZmRandom_HH
#define ZmRandom_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <limits.h>
#include <math.h>
#ifndef _WIN32
#include <stdio.h>
#endif

#include <zlib/ZuInt.hh>

#include <zlib/ZmObject.hh>
#include <zlib/ZmTime.hh>

class ZmAPI ZmRandom {
public:
  enum { N = 624 };	// length of state vector

protected:
  enum { M = 397 };	// period parameter

  uint32_t	m_state[N];	// internal state
  uint32_t	*m_next;	// next value to get from state
  int		m_left;		// number of values left before reload needed

public:
  ZmRandom() { seed(); } // auto-initialize with /dev/urandom or time
  ZmRandom(uint32_t oneSeed ) // initialize with a simple uint32
    { seed(oneSeed); }
  ZmRandom(uint32_t *const bigSeed, int seedLength = N ) // or array
    { seed(bigSeed, seedLength); }

  ZmRandom(const ZmRandom &) = delete;
  ZmRandom &operator =(const ZmRandom &) = delete;

  // do NOT use for CRYPTOGRAPHY without securely hashing several returned
  // values together, otherwise the generator state can be learned after
  // reading 624 consecutive values.

  // access to 32-bit random numbers
  ZuInline double rand()			// real number in [0,1]
    { return double(randInt()) * (1.0/4294967295.0); }
  ZuInline double rand(double n)		// real number in [0,n]
    { return double(randInt()) * (n/4294967295.0); }
  ZuInline double randExc()			// real number in [0,1)
    { return double(randInt()) * (1.0/4294967296.0); }
  ZuInline double randExc(double n)		// real number in [0,n)
    { return double(randInt()) * (n/4294967296.0); }
  ZuInline double randDblExc()			// real number in (0,1)
    { return (double(randInt()) + 0.5) * (1.0/4294967296.0); }
  ZuInline double randDblExc(double n)		// real number in (0,n)
    { return (double(randInt()) + 0.5) * (n/4294967296.0); }
  uint32_t randInt() {				// integer in [0,2^32-1]
    // pull a 32-bit integer from the generator state
    // every other access function simply transforms the numbers extracted here

    if (m_left == 0) reload();
    --m_left;

    uint32_t s1;

    s1 = *m_next++;
    s1 ^= (s1 >> 11);
    s1 ^= (s1 << 7) & 0x9d2c5680;
    s1 ^= (s1 << 15) & 0xefc60000;
    return s1 ^ (s1 >> 18);
  }
  uint32_t randInt(uint32_t n) {		// integer in [0,n] n < 2^32
    // find which bits are used in n
    // optimized by Magnus Jonsson (magnus@smartelectronix.com)
    uint32_t used = n;

    used |= used >> 1;
    used |= used >> 2;
    used |= used >> 4;
    used |= used >> 8;
    used |= used >> 16;

    // draw numbers until one is found in [0,n]
    uint32_t i;

    do
      i = randInt() & used;	// discard unused bits to shorten search
    while(i > n);
    return i;
  }

  // access to 53-bit random numbers (capacity of IEEE double precision)
  double rand53() {			// real number in [0,1)
    uint32_t a = randInt() >> 5, b = randInt() >> 6;

    return (a * 67108864.0 + b) * (1.0/9007199254740992.0);  // by Isaku Wada
  }

  // access to non-uniform random number distributions
  double randNorm(double mean = 0.0, double variance = 0.0) {
    // return a real number from a normal (Gaussian) distribution with given
    // mean and variance by Box-Muller method
    double r = sqrt(-2.0 * log(1.0 - randDblExc())) * variance;
    double phi = 2.0 * 3.14159265358979323846264338328 * randExc();

    return mean + r * cos(phi);
  }

  // re-seeding functions with same behavior as initializers
  void seed() {
    // seed the generator with an array from /dev/urandom if available
    // otherwise use a hash of time() and clock() values
    // first try getting an array from /dev/urandom
#ifndef _WIN32
    FILE *urandom = fopen("/dev/urandom", "r");

    if (urandom)
    {
      uint32_t bigSeed[N];
      uint32_t *s = bigSeed;
      int i = N;
      bool success = true;
      while (success && i--)
	success = fread(s++, sizeof(uint32_t), 1, urandom);
      fclose(urandom);
      if (success) { seed(bigSeed, N); return; }
    }
    // was not successful, so use time now instead
#endif
    seed(Zm::now().hash());
  }
  void seed(uint32_t oneSeed) {
    // seed the generator with a simple uint32_t
    initialize(oneSeed);
    reload();
  }
  void seed(uint32_t *const bigSeed, int seedLength = N) {
    // seed the generator with an array of uint32_t's
    //
    // there are 2^19937-1 possible initial states; this function allows
    // all of those to be accessed by providing at least 19937 bits (with a
    // default seed length of N = 624 uint32_t's); any bits above the lower 32
    // in each element are discarded
    //
    // just call seed() if you want to get array from /dev/urandom

    initialize(19650218);

    int i = 1, j = 0, k = (N > seedLength ? N : seedLength);

    for(; k; --k)
    {
      m_state[i] =
	m_state[i] ^ ((m_state[i-1] ^ (m_state[i-1] >> 30)) * 1664525);
      m_state[i] += (bigSeed[j] & 0xffffffff) + j;
      m_state[i] &= 0xffffffff;
      ++i; ++j;
      if(i >= N) { m_state[0] = m_state[N-1];  i = 1; }
      if(j >= seedLength) j = 0;
    }
    for(k = N - 1; k; --k)
    {
      m_state[i] =
	m_state[i] ^ ((m_state[i-1] ^ (m_state[i-1] >> 30)) * 1566083941);
      m_state[i] -= i;
      m_state[i] &= 0xffffffff;
      ++i;
      if(i >= N) { m_state[0] = m_state[N-1];  i = 1; }
    }
    m_state[0] = 0x80000000;  // MSB is 1, assuring non-zero initial array
    reload();
  }

protected:
  void initialize(uint32_t oneSeed) {
    // initialize generator state with seed
    //
    // see Knuth TAOCP Vol 2, 3rd Ed, p.106 for multiplier
    //
    // in previous versions, most significant bits (MSBs) of the seed affect
    // only MSBs of the state array
    // 
    // modified 9 Jan 2002 by Makoto Matsumoto.

    uint32_t *s = m_state;
    uint32_t *r = m_state;
    int i = 1;

    *s++ = oneSeed & 0xffffffff;
    for(; i < N; ++i) {
      *s++ = (1812433253 * (*r ^ (*r >> 30)) + i) & 0xffffffff;
      r++;
    }
  }
  void reload() {
    // generate N new values in state
    // made clearer and faster by Matthew Bellew (matthew.bellew@home.com)

    uint32_t *p = m_state;
    int i;

    for(i = N - M; i--; ++p) *p = twist(p[M], p[0], p[1]);
    for(i = M; --i; ++p) *p = twist(p[M-N], p[0], p[1]);
    *p = twist(p[M-N], p[0], m_state[0]);

    m_left = N, m_next = m_state;
  }
  uint32_t hiBit(const uint32_t &u) const { return u & 0x80000000; }
  uint32_t loBit(const uint32_t &u) const { return u & 0x00000001; }
  uint32_t loBits(const uint32_t &u) const { return u & 0x7fffffff; }
  uint32_t mixBits(const uint32_t &u, const uint32_t &v) const
    { return hiBit(u) | loBits(v); }
  uint32_t twist(
    const uint32_t &m, const uint32_t &s0, const uint32_t &s1) const
      { return m ^ (mixBits(s0,s1)>>1) ^ (-(int32_t)loBit(s1) & 0x9908b0df); }
};

#include <zlib/ZmCleanup.hh>

class ZmAPI ZmRand : public ZmObject, public ZmRandom {
  ZmRand(const ZmRand &) = delete;
  ZmRand &operator =(const ZmRand &) = delete;

  ZmRand() : ZmRandom{} { }

  static ZmRandom *instance();

public:
  ZuInline static double rand()			// real number in [0,1]
    { return instance()->rand(); }
  ZuInline static double rand(double n)		// real number in [0,n]
    { return instance()->rand(n); }
  ZuInline static double randExc()		// real number in [0,1)
    { return instance()->randExc(); }
  ZuInline static double randExc(double n)	// real number in [0,n)
    { return instance()->randExc(n); }
  ZuInline static double randDblExc()		// real number in (0,1)
    { return instance()->randDblExc(); }
  ZuInline static double randDblExc(double n)	// real number in (0,n)
    { return instance()->randDblExc(n); }
  ZuInline static uint32_t randInt()		// integer in [0,2^32-1]
    { return instance()->randInt(); }
  ZuInline static uint32_t randInt(uint32_t n)	// integer in [0,n] n < 2^32
    { return instance()->randInt(n); }

  ZuInline static double rand53()		// real number in [0,1)
    { return instance()->rand53(); }

  ZuInline static double randNorm(double mean = 0.0, double variance = 0.0)
    { return instance()->randNorm(mean, variance); }

  static void seed()
    { instance()->seed(); }
  static void seed(uint32_t oneSeed)
    { instance()->seed(oneSeed); }
  static void seed(uint32_t *const bigSeed, int seedLength = ZmRandom::N)
    { instance()->seed(bigSeed, seedLength); }
};

#endif /* ZmRandom_HH */

// Change log:
//
// v0.1 - First release on 15 May 2000
//	- Based on code by Makoto Matsumoto, Takuji Nishimura, and Shawn Cokus
//	- Translated from C to C++
//	- Made completely ANSI compliant
//	- Designed convenient interface for initialization, seeding, and
//	  obtaining numbers in default or user-defined ranges
//	- Added automatic seeding from /dev/urandom or time() and clock()
//	- Provided functions for saving and loading generator state
//
// v0.2 - Fixed bug which reloaded generator one step too late
//
// v0.3 - Switched to clearer, faster reload() code from Matthew Bellew
//
// v0.4 - Removed trailing newline in saved generator format to be consistent
//	  with output format of built-in types
//
// v0.5 - Improved portability by replacing static const int's with enum's and
//	  clarifying return values in seed(); suggested by Eric Heimburg
//	- Removed MAXINT constant; use 0xffffffff instead
//
// v0.6 - Eliminated seed overflow when uint32_t is larger than 32 bits
//	- Changed integer [0,n] generator to give better uniformity
//
// v0.7 - Fixed operator precedence ambiguity in reload()
//	- Added access for real numbers in (0,1) and (0,n)
//
// v0.8 - Included time.h header to properly support time_t and clock_t
//
// v1.0 - Revised seeding to match 26 Jan 2002 update of Nishimura and Matsumoto
//	- Allowed for seeding with arrays of any length
//	- Added access for real numbers in [0,1) with 53-bit resolution
//	- Added access for real numbers from normal (Gaussian) distributions
//	- Increased overall speed by optimizing twist()
//	- Doubled speed of integer [0,n] generation
//	- Fixed out-of-range number generation on 64-bit machines
//	- Improved portability by substituting literal constants for long enum's
//	- Changed license from GNU LGPL to BSD

// ZmRandom
//	- Reformatting/renaming
//	- Use ZuTime for seeding
//	- Removed save/load/streaming methods
