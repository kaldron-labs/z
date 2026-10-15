//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtArray.hh>
#include <zlib/ZiFile.hh>
#include <zlib/Zpb.hh>

#include <note.hh>

ZuDerive(Deltas, (ZtArray<int32_t, ZtArrayHeapID<"zpbexample.Deltas">>));
ZuDerive(Output, (ZtArray<uint8_t, ZtArrayHeapID<"zpbexample.Output">>));

struct HandNote {
  uint32_t	id = 0;
  ZuCSpan	text;
  Deltas	deltas;
};
ZfStruct(, (HandNote, PB),
  (id,,				UInt32),
  (text,,			String),
  (deltas,	(PB::ZigZag),	Int32Vec));
Zpb::AsMessage<"example.Note"> Zpb_Fmt(HandNote *);

int main()
{
  HandNote handwritten;
  handwritten.id = 17;
  handwritten.text = "hello protobuf";
  handwritten.deltas.push(-2);
  handwritten.deltas.push(5);

  Output bytes;
  Zpb::ArrayStorage storage{bytes};
  Zpb::Builder builder{storage};
  auto wire = Zpb::save(builder, handwritten);
  if (builder.failed() || !Zpb::verify<example::Note>(wire)) return 1;

  auto generated = Zpb::load<example::Note>(wire);
  if (generated.id != handwritten.id || generated.text != handwritten.text ||
    generated.deltas.length() != 2 || generated.deltas[0] != -2 ||
    generated.deltas[1] != 5) return 1;

  Output reverse;
  Zpb::ArrayStorage reverseStorage{reverse};
  Zpb::Builder reverseBuilder{reverseStorage};
  auto reverseWire = Zpb::save(reverseBuilder, generated);
  if (reverseBuilder.failed() || !Zpb::verify<HandNote>(reverseWire)) return 1;
  auto loaded = Zpb::load<HandNote>(reverseWire);
  if (loaded.id != handwritten.id || loaded.text != handwritten.text ||
    loaded.deltas.length() != 2 || loaded.deltas[0] != -2 ||
    loaded.deltas[1] != 5) return 1;

  ZiFile out;
  out.openStdOut();
  if (!out) return 1;
  constexpr char result[] = "Zpb example OK\n";
  return out.write(result, sizeof(result) - 1) == Zi::OK ? 0 : 1;
}
