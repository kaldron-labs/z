METASOURCES = AUTO
AM_CPPFLAGS = -DZU_EXPORTS @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = \
	ZuLib.hh \
	ZuArrayFn.hh ZuSpan.hh ZuArray.hh ZuAssert.hh ZuBox.hh \
	ZuBitmap.hh ZuBitfield.hh ZuByteSwap.hh ZuCmp.hh ZuICmp.hh \
	ZuFnName.hh ZuFmt.hh ZuFP.hh ZuFP_.hh ZuHash.hh ZuID.hh \
	ZuInt.hh ZuLargest.hh ZuDemangle.hh ZuTime.hh ZuDateTime.hh \
	ZuLambdaTraits.hh ZuReserved.hh \
	ZuObjectTraits.hh ZuObject.hh ZuPolymorph.hh ZuPP.hh \
	ZuPrint.hh ZuRef.hh ZuStdString.hh \
	ZuStringFn.hh ZuTraits.hh ZuTuple.hh ZuUnion.hh ZuPercent.hh \
	Zu_aton.hh Zu_ntoa.hh ZuDecimalFn.hh ZuUTF.hh \
	ZuDecimal.hh ZuSwitch.hh ZuSort.hh ZuPtr.hh \
	ZuNorm.hh ZuEquiv.hh ZuStruct.hh ZuIOResult.hh ZuUnroll.hh \
	ZuBitStream.hh ZuStream.hh ZuMostAligned.hh \
	ZuVStream.hh ZuVArray.hh ZuMArray.hh ZuIter.hh ZuJoin.hh \
	ZuBase32.hh ZuBase64.hh ZuBase64URL.hh ZuHex.hh \
	ZuMatcher.hh ZuDerive.hh ZuAlt.hh ZuTest.hh ZuTestUtil.hh \
	ZuElem.hh \
	\
	zu_lib.h zu_decimal.h zu_time.h zu_bitmap.h
lib_LTLIBRARIES = libZu.la
libZu_la_SOURCES = \
	ZuLib.cc ZuUTF.cc ZuTime.cc ZuDateTime.cc ZuTest.cc \
	ZuDemangle.cc \
	\
	zu_decimal.cc zu_time.cc zu_bitmap.cc
libZu_la_LIBADD = @Z_LIBS@
