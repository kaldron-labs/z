//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuHex.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtlsHPKE.hh>

using namespace ZuTestUtil;
using namespace Ztls::PK;

static bool decode(ZuSpan<uint8_t> out, ZuCSpan hex)
{
  return hex.length() == ZuHex::enclen(out.length()) &&
    ZuHex::decode(out, hex) == out.length();
}

// First base-mode hybrid HPKE vector:
// https://github.com/FiloSottile/hpke/blob/main/testdata/hpke-pq.json
static constexpr char SeedHex[] =
  "B3F98B03126A431CCECC62AE0F68E102C2D8E1CC7B21BA85D821D8E31761E0F8";
static constexpr char EncHex[] =
  "B440CB006466E8EE9D161B371B6FA1EC419D6A7589492378DC678FEDBCF9E7DEBFB47F7E0B5368B0"
  "E77EF5B5866686B65231DBD1C1A42E0AF9B0ABB06C795A1AF0734B450DBB60FE0486B1497D7B09D0"
  "C46617A40C5F8C8AB51C2E8E1F48023F73B7C4716BBA2E905D5FB42C3DEDFF166553ECF033305A57"
  "BF436317E6513DEEA2F65537065BB5D82DC4B8A965C3E939B910DC6B027E01673A6E1399B9397629"
  "2EF9FD81120EF2F6C47D94A1C77D9FE16BA7107A8A6A4CE9CE0D302847D602167DE077E17DBB7E01"
  "54202F76C381C4B6D8BCA51680DAB4DBF373DA8F09AA23D2174FB36681CE42108F7BAADCB35626BA"
  "F30A416BD79B3E249585079C277B79B7B31108EF061F25B5D4E548F6F5CC3D4C24FA0F1716843BB6"
  "3AD00A78F37D2E2B81517810ABE9853829BED7B3BA309AD697D8A5F66AF4DD237C25725E9C626374"
  "4BF8641D475D4792AB0535D2B4FDFCF0C5D95118F5779521023016D49751794A1CE66F2A65243684"
  "3978937562A4A5E8628D2B720890D7F3B21C151399BA7DB03CD15516C6A94B84F6D01A37BA92CC7A"
  "C6C480DC9F67C3A066378180BCD2922D3F5C65D69FD0B96AADC055D6B05EBB1105ACC609F200E0C9"
  "45A10E4E11371E23369DE2069CCD7175A652C3CD09EB7F17C9B65B4AA79B26468F9B21F8C0AA8F74"
  "71D5CFBF3697D3EEDEA9351597CE981E7CF745C2950070C1F82F132B48584D03BA1262CB856FF6B5"
  "AE25992DF8612D24F068B4325D3360673ED3EF6E2A57DE297D5482C5CC355BC07F1D975FC6D60CD7"
  "109BF5A77A0FF7B2C5D9F4A276D30CB49DA48B8B90B644B15A5B68FCC67C25F09A8E567CBE4FA2E2"
  "BA11C02993E9E9B4116A7C60DA64A71932800AEC2FB4D2ECEEF57C6FC2308F3ADCD9B46A28748516"
  "284BDB4B3A36851512C5E0E6ED37EF5F00B07DC3C42667CF95CAD764E47F48A994D17C103F822575"
  "5C76008013897C03C31043DF0EB39A603E09CAEAA41AE24488FE96E4D83B4AE5481045F4A7CFD7C8"
  "0B31CE9EEB8FDECD34BE1245F368AB5A3215CBCDFBE0529E1FBC4BA0041CFABA09836C25DD6219E7"
  "5FBC6F143E74D686ECD9E1A416881BC21A9129FB865E82332985798F701F7952C4E69E7B4E6BD03B"
  "FFDC0C65E2A2FDE89F73B8659FD2CC7DFB070D3E95581D1BC587A2D9C4BF142FDC1F20856D3CFB64"
  "D35744EE279B829184723221E9FB19F012AB99C4BB1A904A116727B667C5A11A0E11F3E31682B0C1"
  "14345ECC3EE153BCCD884654BD5A8A023AA3DB878148736F6A090F92785423A9BA2B037B3B90EE91"
  "657BA48A125360DAE75A6FDDFEA406CA823A5E4FBB54AA8909FBD85D95D2ED256ED5D6A9194FAD0D"
  "81A44D3172ABF6B90CECD1ED2080762D670DB4D3437EF8E9E7D39DB4B4215C33F8D19240ED4BF2DE"
  "8B1076B345707043A735BF9E96E16C8B670CF2DF0CE8DB638C7D84A13EE7B35266C7F0E60D2CB2E5"
  "734E9D646A871D0DFD8B4EE5F825BF799A1251ED21E54510E9C605BC83A0BD9673AEE80E8D064A95"
  "C3C3151FFD27608173637FB9DE30B3C02D96EECAC05DBF7C2FBC98B4A1F6972CE928322A22E2B75C";
static constexpr char InfoHex[] =
  "34663634363532303666366532303631323034373732363536333639363136653230353537323665";
static constexpr char AADHex[] =
  "436F756E742D30";
static constexpr char CiphertextHex[] =
  "AC355D192158CD54250E1702BE51E9D2EAFE5F9292A9F153E02A2323E1FF071A30947836C38C63C9"
  "86C28CCF05E00D4E5FE066A48AB8D5B39C69D32DA80C93DC868DAA0F853A6CBDD640";
static constexpr char PlaintextHex[] =
  "34323635363137353734373932303639373332303734373237353734363832633230373437323735"
  "373436383230363236353631373537343739";

static void vector()
{
  ZuTestScope(vector);
  uint8_t seed[HybridSeedSize], enc[HybridCiphertextSize];
  uint8_t info[(sizeof(InfoHex) - 1) / 2];
  uint8_t aad[(sizeof(AADHex) - 1) / 2];
  uint8_t ct[(sizeof(CiphertextHex) - 1) / 2];
  uint8_t expected[(sizeof(PlaintextHex) - 1) / 2];
  uint8_t plain[sizeof(expected)];
  ZuCheck(decode(seed, SeedHex));
  ZuCheck(decode(enc, EncHex));
  ZuCheck(decode(info, InfoHex));
  ZuCheck(decode(aad, AADHex));
  ZuCheck(decode(ct, CiphertextHex));
  ZuCheck(decode(expected, PlaintextHex));
  SK_MLKEM768_X25519 sk{seed};
  auto r = Ztls::HPKE::openBase(sk,
    info, aad, enc, ct, plain);
  ZuCheck(!r.template is<ZeException>());
  if (!r.template is<ZeException>()) {
    ZuCheck(r.template p<0>() == sizeof(plain));
    ZuCheck(!memcmp(plain, expected, sizeof(plain)));
  }
  ZuClear(seed, sizeof(seed));
  ZuClear(plain, sizeof(plain));
}

static void roundtrip()
{
  ZuTestScope(roundtrip);
  Ztls::Random rng;
  ZuCheck(rng.init());
  uint8_t seed[HybridSeedSize], pub[HybridPublicSize];
  ZuCheck(rng.random(seed));
  SK_MLKEM768_X25519 sk{seed};
  ZuCheck(!sk.exportPK(pub).template is<ZeException>());
  PK_MLKEM768_X25519 pk{pub};
  constexpr uint8_t info[] = {1, 0, 2};
  constexpr uint8_t aad[] = {3, 0, 4};
  constexpr uint8_t input[] = {5, 0, 6, 7};
  uint8_t enc[HybridCiphertextSize];
  uint8_t ct[sizeof(input) + Ztls::HPKE::TagSize];
  uint8_t plain[sizeof(input)];
  ZuCheck(!Ztls::HPKE::sealBase(rng, pk, info, aad, input, enc, ct)
    .template is<ZeException>());
  auto r = Ztls::HPKE::openBase(sk, info, aad, enc, ct, plain);
  ZuCheck(!r.template is<ZeException>());
  if (!r.template is<ZeException>()) {
    ZuCheck(r.template p<0>() == sizeof(input));
    ZuCheck(!memcmp(plain, input, sizeof(input)));
  }
  ZuCheck(Ztls::HPKE::openBase(sk, info, {info, sizeof(info)}, enc, ct, plain)
    .template is<ZeException>());
  ZuCheck(Ztls::HPKE::openBase(sk, aad, aad, enc, ct, plain)
    .template is<ZeException>());
  ZuCheck(Ztls::HPKE::openBase(sk, info, aad,
    {enc, sizeof(enc) - 1}, ct, plain).template is<ZeException>());
  ZuCheck(Ztls::HPKE::openBase(sk, info, aad, enc,
    {ct, sizeof(ct) - 1}, plain).template is<ZeException>());
  ZuCheck(Ztls::HPKE::openBase(sk, info, aad, enc,
    {ct, Ztls::HPKE::TagSize - 1}, plain).template is<ZeException>());
  ZuCheck(Ztls::HPKE::openBase(sk, info, aad, enc, ct,
    {plain, sizeof(plain) - 1}).template is<ZeException>());
  ct[sizeof(ct) - 1] ^= 1;
  memset(plain, 0xA5, sizeof(plain));
  ZuCheck(Ztls::HPKE::openBase(sk, info, aad, enc, ct, plain)
    .template is<ZeException>());
  uint8_t zero[sizeof(plain)] = {};
  ZuCheck(!memcmp(plain, zero, sizeof(plain)));
  ct[sizeof(ct) - 1] ^= 1;
  ZuCheck(Ztls::HPKE::sealBase(rng, pk, info, aad, input,
    {enc, sizeof(enc) - 1}, ct).template is<ZeException>());
  ZuCheck(Ztls::HPKE::sealBase(rng, pk, info, aad, input,
    enc, {ct, sizeof(ct) - 1}).template is<ZeException>());
  uint8_t emptyCT[Ztls::HPKE::TagSize], emptyPlain[1];
  ZuCheck(!Ztls::HPKE::sealBase(rng, pk, info, aad, {},
    enc, emptyCT).template is<ZeException>());
  auto empty = Ztls::HPKE::openBase(sk, info, aad, enc, emptyCT,
    {emptyPlain, 0});
  ZuCheck(!empty.template is<ZeException>());
  if (!empty.template is<ZeException>())
    ZuCheck(empty.template p<0>() == 0);
  memcpy(ct, input, sizeof(input));
  ZuCheck(!Ztls::HPKE::sealBase(rng, pk, info, aad,
    {ct, sizeof(input)}, enc, ct).template is<ZeException>());
  auto inPlace = Ztls::HPKE::openBase(sk, info, aad, enc, ct,
    {ct, sizeof(input)});
  ZuCheck(!inPlace.template is<ZeException>());
  if (!inPlace.template is<ZeException>()) {
    ZuCheck(inPlace.template p<0>() == sizeof(input));
    ZuCheck(!memcmp(ct, input, sizeof(input)));
  }
  ZuClear(seed, sizeof(seed));
  ZuClear(plain, sizeof(plain));
}

int main()
{
  ZuTestMain();
  ZuTestCall_("vector", vector);
  ZuTestCall_("roundtrip", roundtrip);
}
