// -*- C++ -*-

// Copyright (C) 2005-2020 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software
// Foundation; either version 3, or (at your option) any later
// version.

// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.

// Under Section 7 of GPL version 3, you are granted additional
// permissions described in the GCC Runtime Library Exception, version
// 3.1, as published by the Free Software Foundation.

// You should have received a copy of the GNU General Public License and
// a copy of the GCC Runtime Library Exception along with this program;
// see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
// <http://www.gnu.org/licenses/>.

// Copyright (C) 2004 Ami Tavory and Vladimir Dreizin, IBM-HRL.

// Permission to use, copy, modify, sell, and distribute this software
// is hereby granted without fee, provided that the above copyright
// notice appears in all copies, and that both that copyright notice
// and this permission notice appear in supporting documentation. None
// of the above authors, nor IBM Haifa Research Laboratories, make any
// representation about the suitability of this software for any
// purpose. It is provided "as is" without express or implied
// warranty.

/**
 * @file stats_tree.hh
 * Contains order statistics tree supporting duplicate (repeated) values
 */

#ifndef PB_DS_STATS_TREE_HH
#define PB_DS_STATS_TREE_HH

#include <utility>
#include <iterator>
#include <ext/pb_ds/assoc_container.hpp>	// included with gcc
#include <ext/pb_ds/tree_policy.hpp>		// ''

// "multiset" order statistics tree supporting repeated values

namespace {
using namespace __gnu_pbds;

// adapted from canonical GNU pb_ds tree_order_statistics_node_update
template <
  typename Node_CItr, typename Node_Itr, typename Cmp_Fn, typename _Alloc>
class stats_tree_node_update :
  private detail::branch_policy<Node_CItr, Node_Itr, _Alloc> {
private:
  typedef detail::branch_policy<Node_CItr, Node_Itr, _Alloc> base_type;

public:
  using cmp_fn = Cmp_Fn;
  using allocator_type = _Alloc;
  using size_type = typename allocator_type::size_type;
  using key_type = typename base_type::key_type;
  using key_const_reference = typename base_type::key_const_reference;

  using metadata_type = size_type;
  using node_const_iterator = Node_CItr;
  using node_iterator = Node_Itr;
  using const_iterator = typename node_const_iterator::value_type;
  using iterator = typename node_iterator::value_type;

  virtual ~stats_tree_node_update() { }

private:
  virtual node_const_iterator node_begin() const = 0;
  virtual node_iterator node_begin() = 0;
  virtual node_const_iterator node_end() const = 0;
  virtual node_iterator node_end() = 0;
  virtual cmp_fn &get_cmp_fn() = 0;

public:
  const_iterator find_by_order(size_type order) const {
    return const_cast<stats_tree_node_update *>(this)->find_by_order(order);
  }
  iterator find_by_order(size_type order) {
    node_iterator it = node_begin();
    node_iterator end_it = node_end();

    while (it != end_it) {
      node_iterator l_it = it.get_l_child();
      const size_type o = l_it == end_it ? 0 : l_it.get_metadata();
      const size_type n = (*it)->second; // n = repeat count

      if (order < o)
	it = l_it;
      else if (order < o + n)
	return *it;
      else {
	order -= o + n;
	it = it.get_r_child();
      }
    }
    return base_type::end_iterator();
  }

  size_type order_of_key(key_const_reference r_key) const {
    node_const_iterator it = node_begin();
    node_const_iterator end_it = node_end();

    const auto &r_cmp_fn =
      const_cast<stats_tree_node_update *>(this)->get_cmp_fn();
    size_type ord = 0;
    while (it != end_it) {
      node_const_iterator l_it = it.get_l_child();

      const auto &key = (*it)->first;
      if (r_cmp_fn(r_key, key))
	it = l_it;
      else if (r_cmp_fn(key, r_key)) {
	const size_t n = (*it)->second; // n = repeat count
	ord += (l_it == end_it) ? n : n + l_it.get_metadata();
	it = it.get_r_child();
      } else {
	ord += (l_it == end_it) ? 0 : l_it.get_metadata();
	it = end_it;
      }
    }
    return ord;
  }

  // increment repeat count
  void inc(iterator it_) {
    auto node = it_.m_p_nd;
    auto begin = node_begin().m_p_nd;
    do {
      ++const_cast<metadata_reference>(node->get_metadata());
    } while (node != begin && (node = node->m_p_parent));
  }

  // decrement repeat count
  void dec(iterator it_) {
    auto node = it_.m_p_nd;
    auto begin = node_begin().m_p_nd;
    do {
      --const_cast<metadata_reference>(node->get_metadata());
    } while (node != begin && (node = node->m_p_parent));
  }

private:
  typedef typename detail::rebind_traits<_Alloc, metadata_type>::reference
    metadata_reference;

protected:
  void operator()(node_iterator node_it, node_const_iterator end_nd_it) const {
    node_iterator l_it = node_it.get_l_child();
    const size_type l_rank = (l_it == end_nd_it) ? 0 : l_it.get_metadata();

    node_iterator r_it = node_it.get_r_child();
    const size_type r_rank = (r_it == end_nd_it) ? 0 : r_it.get_metadata();

    const size_t n = (*node_it)->second;
    const_cast<metadata_reference>(node_it.get_metadata()) =
      n + l_rank + r_rank;
  }
};
} // namespace

namespace __gnu_pbds {
template <typename Key, typename Count, typename Allocator>
using stats_tree_ = __gnu_pbds::tree<
  Key, Count, std::less<Key>,
  __gnu_pbds::rb_tree_tag, stats_tree_node_update, Allocator>;
template <
  typename Key, typename Count = unsigned int,
  typename Allocator = std::allocator<char>>
class stats_tree : public stats_tree_<Key, Count, Allocator> {
private:
  using base_type = stats_tree_<Key, Count, Allocator>;
  using key_type = Key;
  using iterator = decltype(ZuDeclVal<base_type &>().begin());

public:
  void insert(key_type key) {
    auto p = base_type::insert(std::make_pair(key, 1));
    if (!p.second) {
      ++p.first->second;
      this->inc(p.first);
    }
  }
  void erase(iterator iter) {
    if (iter->second == 1)
      base_type::erase(iter);
    else {
      this->dec(iter);
      --iter->second;
    }
  }
};
} // namespace __gnu_pbds

#endif 
