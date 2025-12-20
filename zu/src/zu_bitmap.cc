//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ZuBitmap - C API

#include <zlib/ZuAssert.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuBitmap.hh>
#include <zlib/ZuDerive.hh>

#include <zlib/zu_bitmap.h>

#define BitShift 6
#define ByteShift 3

#pragma pack(push, 8)
struct Data : public zu_bitmap {
  using zu_bitmap::data;

  enum { Fixed = 0 };

private:
  void copy(const Data &b) {
    unsigned n = b.zu_bitmap::length;
    if (n > zu_bitmap::length) n = zu_bitmap::length;
    memcpy(&data[0], &b.data[0], n<<(BitShift - ByteShift));
  }

public:
  Data(unsigned n) { length(n); }
  Data(const Data &b) { copy(b); }
  Data &operator =(const Data &b) { copy(b); return *this; }
  Data(Data &&b) { copy(b); }
  Data &operator =(Data &&b) { copy(b); return *this; }

  /* return length resulting from combining this with another instance
   * - zu_bitmap is grown by the caller, so return the overlap here */
  unsigned combine(const Data &b) {
    unsigned l = zu_bitmap::length;
    unsigned r = b.zu_bitmap::length;
    return (l < r) ? l : r;
  }

  unsigned length() const {
    return zu_bitmap::length<<BitShift;
  }
  void length(unsigned n) {
    zu_bitmap::length = (n + 63)>>BitShift;
  }
};

ZuDerive(Bitmap, (ZuBitmap_::Bitmap_<Data>));
#pragma pack(pop)

zu_bitmap *zu_bitmap_new_(
  const zu_bitmap_allocator *allocator, unsigned n)
{
  n = (n + 63) & ~63;
  zu_bitmap *v_ = static_cast<zu_bitmap *>(
    allocator->alloc(sizeof(uint64_t) + (n>>ByteShift)));
  if (!v_) return nullptr;
  new (v_) Bitmap{n};
  return v_;
}

zu_bitmap *zu_bitmap_new(const zu_bitmap_allocator *allocator, unsigned n)
{
  zu_bitmap *v_;
  if (!(v_ = zu_bitmap_new_(allocator, n))) return nullptr;
  auto &v = *reinterpret_cast<Bitmap *>(v_);
  memset(&v.data[0], 0, v.zu_bitmap::length<<(BitShift - ByteShift));
  return v_;
}

zu_bitmap *zu_bitmap_new_fill(const zu_bitmap_allocator *allocator, unsigned n)
{
  zu_bitmap *v_;
  if (!(v_ = zu_bitmap_new_(allocator, n))) return nullptr;
  auto &v = *reinterpret_cast<Bitmap *>(v_);
  memset(&v.data[0], 0xff, v.zu_bitmap::length<<(BitShift - ByteShift));
  return v_;
}

void zu_bitmap_delete(const zu_bitmap_allocator *allocator, zu_bitmap *v_)
{
  auto &v = *reinterpret_cast<Bitmap *>(v_);
  v.~Bitmap();
  allocator->free(v_);
}

zu_bitmap *zu_bitmap_copy(
  const zu_bitmap_allocator *allocator, const zu_bitmap *p_)
{
  const auto &p = *reinterpret_cast<const Bitmap *>(p_);
  auto n = p.length();
  zu_bitmap *v_;
  if (!(v_ = zu_bitmap_new_(allocator, n))) return nullptr;
  auto &v = *reinterpret_cast<Bitmap *>(v_);
  memcpy(&v.data[0], &p.data[0], n<<(BitShift - ByteShift));
  return v_;
}

zu_bitmap *zu_bitmap_resize(
  const zu_bitmap_allocator *allocator, zu_bitmap *v_, unsigned n)
{
  auto &v = *reinterpret_cast<Bitmap *>(v_);
  auto l = (n + 63)>>BitShift;
  auto o = v.zu_bitmap::length;
  if (o == l) return v_;
  if (o > l) { v.zu_bitmap::length = l; return v_; }
  auto w_ = zu_bitmap_new_(allocator, n);
  if (!w_) { zu_bitmap_delete(allocator, v_); return nullptr; }
  auto &w = *reinterpret_cast<Bitmap *>(w_);
  memcpy(&w.data[0], &v.data[0], o<<(BitShift - ByteShift));
  memset(&w.data[o], 0, (l - o)<<(BitShift - ByteShift));
  zu_bitmap_delete(allocator, v_);
  return w_;
}

unsigned zu_bitmap_length(const zu_bitmap *v_)
{
  const auto &v = *reinterpret_cast<const Bitmap *>(v_);
  return v.length();
}

unsigned zu_bitmap_in(
  const zu_bitmap_allocator *allocator, zu_bitmap **v_, const char *s_)
{
  ZuCSpan s(s_);
  unsigned n = 1U + Bitmap::scanLast(s);
  if (!(*v_ = zu_bitmap_new(allocator, n))) return 0;
  auto &v = *reinterpret_cast<Bitmap *>(*v_);
  return v.scan(s);
}

unsigned zu_bitmap_out_len(const zu_bitmap *v_)
{
  const auto &v = *reinterpret_cast<const Bitmap *>(v_);
  return v.printLen() + 1;
}

char *zu_bitmap_out(char *s_, unsigned n, const zu_bitmap *v_)
{
  const auto &v = *reinterpret_cast<const Bitmap *>(v_);
  if (ZuUnlikely(!n)) return nullptr; // should never happen
  ZuStream s{s_, n - 1};
  s << v;
  *s.data() = 0;
  return s.data();
}

unsigned zu_bitmap_get_wlength(const zu_bitmap *v_)
{
  const auto &v = *reinterpret_cast<const Bitmap *>(v_);
  return v.zu_bitmap::length;
}
uint64_t zu_bitmap_get_word(const zu_bitmap *v_, unsigned i)
{
  const auto &v = *reinterpret_cast<const Bitmap *>(v_);
  return v.data[i];
}

void zu_bitmap_set_word(zu_bitmap *v_, unsigned i, uint64_t w)
{
  auto &v = *reinterpret_cast<Bitmap *>(v_);
  v.data[i] = w;
}

int zu_bitmap_cmp(const zu_bitmap *l_, const zu_bitmap *r_)
{
  const auto &l = *reinterpret_cast<const Bitmap *>(l_);
  const auto &r = *reinterpret_cast<const Bitmap *>(r_);
  return l.cmp(r);
}

uint32_t zu_bitmap_hash(const zu_bitmap *v_)
{
  const auto &v = *reinterpret_cast<const Bitmap *>(v_);
  return v.hash();
}

bool zu_bitmap_get(const zu_bitmap *v_, unsigned i)
{
  const auto &v = *reinterpret_cast<const Bitmap *>(v_);
  return v.get(i);
}
zu_bitmap *zu_bitmap_set(
  const zu_bitmap_allocator *allocator, zu_bitmap *v_, unsigned i)
{
  auto v = reinterpret_cast<Bitmap *>(v_);
  if (i >= v->length()) {
    v_ = zu_bitmap_resize(allocator, v_, i + 1);
    v = reinterpret_cast<Bitmap *>(v_);
  }
  v->set(i);
  return v_;
}
zu_bitmap *zu_bitmap_clr(zu_bitmap *v_, unsigned i)
{
  auto &v = *reinterpret_cast<Bitmap *>(v_);
  v.clr(i);
  return v_;
}

zu_bitmap *zu_bitmap_set_range(
  const zu_bitmap_allocator *allocator,
  zu_bitmap *v_, unsigned begin, unsigned end)
{
  auto v = reinterpret_cast<Bitmap *>(v_);
  if (end > v->length()) {
    v_ = zu_bitmap_resize(allocator, v_, end);
    v = reinterpret_cast<Bitmap *>(v_);
  }
  v->set(begin, end);
  return v_;
}

zu_bitmap *zu_bitmap_clr_range(zu_bitmap *v_, unsigned begin, unsigned end)
{
  auto &v = *reinterpret_cast<Bitmap *>(v_);
  v.clr(begin, end);
  return v_;
}

int zu_bitmap_first(const zu_bitmap *v_)
{
  const auto &v = *reinterpret_cast<const Bitmap *>(v_);
  return v.first();
}
int zu_bitmap_last(const zu_bitmap *v_)
{
  const auto &v = *reinterpret_cast<const Bitmap *>(v_);
  return v.last();
}
int zu_bitmap_next(const zu_bitmap *v_, int i)
{
  const auto &v = *reinterpret_cast<const Bitmap *>(v_);
  return v.next(i);
}
int zu_bitmap_prev(const zu_bitmap *v_, int i)
{
  const auto &v = *reinterpret_cast<const Bitmap *>(v_);
  return v.prev(i);
}

zu_bitmap *zu_bitmap_zero(zu_bitmap *v_)
{
  auto &v = *reinterpret_cast<Bitmap *>(v_);
  v.zero();
  return v_;
}

zu_bitmap *zu_bitmap_fill(zu_bitmap *v_)
{
  auto &v = *reinterpret_cast<Bitmap *>(v_);
  v.fill();
  return v_;
}

zu_bitmap *zu_bitmap_flip(zu_bitmap *v_)
{
  auto &v = *reinterpret_cast<Bitmap *>(v_);
  v.flip();
  return v_;
}

zu_bitmap *zu_bitmap_or(
  const zu_bitmap_allocator *allocator, zu_bitmap *v_, const zu_bitmap *p_)
{
  auto v = reinterpret_cast<Bitmap *>(v_);
  const auto &p = *reinterpret_cast<const Bitmap *>(p_);
  if (v->length() < p.length())
    v = reinterpret_cast<Bitmap *>(
      v_ = zu_bitmap_resize(allocator, v_, p.length()));
  if (!v) return nullptr;
  *v |= p;
  return v_;
}

zu_bitmap *zu_bitmap_and(
  const zu_bitmap_allocator *allocator, zu_bitmap *v_, const zu_bitmap *p_)
{
  auto v = reinterpret_cast<Bitmap *>(v_);
  const auto &p = *reinterpret_cast<const Bitmap *>(p_);
  if (v->length() < p.length())
    v = reinterpret_cast<Bitmap *>(
      v_ = zu_bitmap_resize(allocator, v_, p.length()));
  if (!v) return nullptr;
  *v &= p;
  return v_;
}

zu_bitmap *zu_bitmap_xor(
  const zu_bitmap_allocator *allocator, zu_bitmap *v_, const zu_bitmap *p_)
{
  auto v = reinterpret_cast<Bitmap *>(v_);
  const auto &p = *reinterpret_cast<const Bitmap *>(p_);
  if (v->length() < p.length())
    v = reinterpret_cast<Bitmap *>(
      v_ = zu_bitmap_resize(allocator, v_, p.length()));
  if (!v) return nullptr;
  *v ^= p;
  return v_;
}
