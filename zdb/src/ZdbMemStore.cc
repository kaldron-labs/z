//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Zdb in-memory data store

#include <zlib/ZmScratch.hh>

#include <zlib/ZdbMemStore.hh>

#include <zlib/ZuJoin.hh>

Zdb_::Store *ZdbStore()
{
  return new ZdbMem::Store{};
}

namespace ZdbMem {

void StoreTbl::count(KeyID keyID, ZmRef<IOBuf> buf, CountFn countFn)
{
  m_store->run([
    this, keyID, buf = ZuMv(buf), countFn = ZuMv(countFn)
  ]() mutable {
    ZmAssert(keyID >= 0 && keyID < m_indices.length());

    const auto &keyFields = m_keyFields[keyID];
    const auto &xKeyFields = m_xKeyFields[keyID];

    unsigned nParams = m_keyGroup[keyID];

    auto key = loadTuple_(
      nParams, keyFields, xKeyFields, Zfb::GetAnyRoot(buf->data()));

    const auto &index = m_indices[keyID];
    auto row = index.find<ZmRBTreeGreater>(key);
    uint64_t i = 0;
    while (row && equals_(row->key(), key, nParams)) {
      ++i;
      row = index.next(row);
    }
    countFn(CountData{i});
  });
}

void StoreTbl::select(
  bool selectRow, bool selectNext, bool inclusive,
  KeyID keyID, ZmRef<IOBuf> buf,
  unsigned limit, TupleFn tupleFn)
{
  m_store->run([
    this, selectRow, selectNext, inclusive,
    keyID, buf = ZuMv(buf), limit, tupleFn = ZuMv(tupleFn)
  ]() mutable {
    ZmAssert(keyID >= 0 && keyID < m_indices.length());

    const auto &keyFields = m_keyFields[keyID];
    const auto &xKeyFields = m_xKeyFields[keyID];

    unsigned keyGroup = m_keyGroup[keyID];
    unsigned nParams = selectNext ? keyFields.length() : keyGroup;

    auto key = loadTuple_(
      nParams, keyFields, xKeyFields, Zfb::GetAnyRoot(buf->data()));

    const auto &index = m_indices[keyID];
    auto row = inclusive ?
      index.find<ZmRBTreeGreaterEqual>(key) :
      index.find<ZmRBTreeGreater>(key);
    unsigned i = 0;
    while (i++ < limit && row && equals_(row->key(), key, keyGroup)) {
      Zfb::IOBuilder fbb{m_bufAllocFn()};
      if (!selectRow) {
	auto key = extractKey(m_fields, m_keyFields, keyID, row->val()->data);
	fbb.Finish(saveTuple(fbb, xKeyFields, key));
      } else {
	fbb.Finish(saveTuple(fbb, m_xFields, row->val()->data));
      }
      TupleData tupleData{
	.keyID = selectRow ? KeyID(ZuStructKeyID::All) : keyID,
	.buf = fbb.buf(),
	.count = i
      };
      tupleFn(TupleResult{ZuMv(tupleData)});
      row = index.next(row);
    }
    tupleFn(TupleResult{});
  });
}

void StoreTbl::find(KeyID keyID, ZmRef<IOBuf> buf, RowFn rowFn)
{
  m_store->run([this, keyID, buf = ZuMv(buf), rowFn = ZuMv(rowFn)]() mutable {
    ZmAssert(keyID >= 0 && keyID < m_indices.length());

    auto key = loadTuple(
      m_keyFields[keyID], m_xKeyFields[keyID], Zfb::GetAnyRoot(buf->data()));
    ZmRef<const MemRow> row = m_indices[keyID].findVal(key);
    if (row) {
      RowData data{.buf = saveRow<false>(row)};
      rowFn(RowResult{ZuMv(data)});
    } else {
      rowFn(RowResult{});
    }
  });
}

void StoreTbl::recover(Shard shard, UN un, RowFn rowFn)
{
  m_store->run([this, shard, un, rowFn = ZuMv(rowFn)]() mutable {
    // build Recover buf and return it
    ZmRef<const MemRow> row = m_indexUN.find(shard, un);
    if (row) {
      RowData data{.buf = saveRow<true>(row)};
      rowFn(RowResult{ZuMv(data)});
    } else {
      // missing is not an error, skip over updated/deleted records
      rowFn(RowResult{});
    }
  });
}

void StoreTbl::write(ZmRef<IOBuf> buf, CommitFn commitFn)
{
  m_store->run([this, buf = ZuMv(buf), commitFn = ZuMv(commitFn)]() mutable {
    // idempotence check
    auto record = record_(msg_(buf->hdr()));
    auto shard = record->shard();
    auto un = record->un();
    auto &maxUN = m_maxUN[shard];
    if (maxUN != ZdbNullUN() && un <= maxUN) {
      commitFn(ZuMv(buf), CommitResult{});
      return;
    }
    // load row, perform insert/update/delete
    ZmRef<MemRow> row = loadRow(buf).mutableRef();
    if (!row->vn)
      insert(ZuMv(row), ZuMv(buf), ZuMv(commitFn));
    else if (row->vn > 0)
      update(ZuMv(row), ZuMv(buf), ZuMv(commitFn));
    else
      del(ZuMv(row), ZuMv(buf), ZuMv(commitFn));
  });
}

void StoreTbl::insert(
  ZmRef<MemRow> row, ZmRef<IOBuf> buf, CommitFn commitFn)
{
  m_maxUN[row->shard] = row->un, m_maxSN = row->sn;
  unsigned n = m_keyFields.length();
  for (unsigned i = 0; i < n; i++) {
    auto key = extractKey(m_fields, m_keyFields, i, row->data);
    ZmAssert(key.length() == m_keyFields[i].length());
    if (!i && m_indices[i].findVal(key)) {
      commitFn(ZuMv(buf), CommitResult{ZeEXCEPT(Error, "ZdbMem",
	  ([id = this->id(), key = ZuMv(key)](auto &s, const auto &) {
	    s << id << " insert(" << ZuJoin(key, ", ")
	      << ") failed - record exists";
	  }))});
      return;
    }
    m_indices[i].add(key, row.constRef());
  }
  m_indexUN.addNode(ZuMv(row));
  commitFn(ZuMv(buf), CommitResult{});
}

ZuDerive(Tuples,
  (ZtArray<Tuple, ZtArrayHeapID<"ZdbMem.Tuples">>));

void StoreTbl::update(
  ZmRef<MemRow> updRow, ZmRef<IOBuf> buf, CommitFn commitFn)
{
  auto key = extractKey(m_fields, m_keyFields, 0, updRow->data);
  ZmRef<MemRow> row = m_indices[0].findVal(key).mutableRef();
  if (row) {
    m_maxUN[updRow->shard] = updRow->un, m_maxSN = updRow->sn;

    // remember original secondary index key values
    unsigned n = m_keyFields.length();
    auto origKeys = ZmScratch(Tuple, n - 1, Tuples::VHeap);
    for (unsigned i = 1; i < n; i++) {
      auto key = extractKey(m_fields, m_keyFields, i, row->data);
      ZmAssert(key.length() == m_keyFields[i].length());
      new (origKeys.push()) Tuple(ZuMv(key)); // not Tuple{}
    }
    // remove from UN index
    m_indexUN.delNode(row);

    row->un = updRow->un;
    row->sn = updRow->sn;
    row->vn = updRow->vn;
    updTuple(m_fields, row->data, ZuMv(updRow->data));

    // add back to UN index
    m_indexUN.addNode(row);
    // update secondary indices if corresponding key changed
    for (unsigned i = 1; i < n; i++) {
      auto j = i - 1;
      auto key = extractKey(m_fields, m_keyFields, i, row->data);
      if (key != origKeys[j]) {
	m_indices[i].del(origKeys[j]);
	m_indices[i].add(key, row.constRef());
      }
    }

    commitFn(ZuMv(buf), CommitResult{});
  } else {
    commitFn(ZuMv(buf), CommitResult{
	ZeEXCEPT(Error, "ZdbMem", ([id = this->id(), key](auto &s, const auto &) {
	  s << id << " update(" << ZuJoin(key, ", ")
	    << ") failed - record missing";
	}))});
  }
}

void StoreTbl::del(
  ZmRef<MemRow> delRow, ZmRef<IOBuf> buf, CommitFn commitFn)
{
  auto key = extractKey(m_fields, m_keyFields, 0, delRow->data);
  ZmRef<MemRow> row = m_indices[0].delVal(key).mutableRef();
  if (row) {
    m_maxUN[delRow->shard] = delRow->un, m_maxSN = delRow->sn;
    m_indexUN.delNode(row);
    unsigned n = m_keyFields.length();
    for (unsigned i = 1; i < n; i++) {
      auto key = extractKey(m_fields, m_keyFields, i, row->data);
      ZmAssert(key.length() == m_keyFields[i].length());
      m_indices[i].del(key);
    }
    commitFn(ZuMv(buf), CommitResult{});
  } else {
    commitFn(ZuMv(buf), CommitResult{
	ZeEXCEPT(Error, "ZdbMem", ([id = this->id(), key](auto &s, const auto &) {
	  s << id << " del(" << ZuJoin(key, ", ")
	    << ") failed - record missing";
	}))});
  }
}

} // ZdbMem
