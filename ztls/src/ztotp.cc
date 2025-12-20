#include <iostream>

#include <zlib/ZuBox.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZuBase32.hh>
#include <zlib/ZtlsTOTP.hh>

static void usage() {
  std::cout
    << "Usage: ztotp BASE32\n\n"
    << "app URI is otpauth://totp/ID@DOMAIN?secret=BASE32&issuer=ISSUER\n"
    << std::flush;
  ::exit(1);
}

int main(int argc, char **argv)
{
  ZtArray<uint8_t> secret;
  if (argc != 2) usage();
  unsigned n = strlen(argv[1]);
  secret.length(ZuBase32::declen(n));
  secret.length(ZuBase32::decode(secret, ZuBSpan{argv[1], n}));
  if (!secret) {
    std::cerr << "decode error\n" << std::flush;
    return 1;
  }
  auto code = Ztls::TOTP::calc(secret);
  std::cout << ZuBoxed(code).fmt<ZuFmt::Right<6>>() << '\n';
  return 0;
}
