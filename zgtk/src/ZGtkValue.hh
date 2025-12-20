//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// GValue wrapper

#ifndef ZGtkValue_HH
#define ZGtkValue_HH

#ifndef ZGtkLib_HH
#include <zlib/ZGtkLib.hh>
#endif

#include <glib.h>

namespace ZGtk {

// RAII C++ wrapper for GValue
class Value : public GValue {
public:
  ZuInline Value() : GValue G_VALUE_INIT { }
  ZuInline Value(GType type) : GValue G_VALUE_INIT {
    g_value_init(this, type);
  }
  ZuInline ~Value() {
    if (G_VALUE_TYPE(this)) g_value_unset(this);
  }

  ZuInline void init(GType type) { g_value_init(this, type); }
  
  ZuInline void unset() {
    if (G_VALUE_TYPE(this)) g_value_unset(this);
    // new (this) Value{};
  }

  ZuInline void set_schar(gint8 v_char) {
    g_value_set_schar(this, v_char);
  }
  ZuInline gint8 get_schar() const {
    return g_value_get_schar(this);
  }
  ZuInline void set_uchar(guchar v_uchar) {
    g_value_set_uchar(this, v_uchar);
  }
  ZuInline guchar get_uchar() const {
    return g_value_get_uchar(this);
  }
  ZuInline void set_boolean(gboolean v_boolean) {
    g_value_set_boolean(this, v_boolean);
  }
  ZuInline gboolean get_boolean() const {
    return g_value_get_boolean(this);
  }
  ZuInline void set_int(gint v_int) {
    g_value_set_int(this, v_int);
  }
  ZuInline gint get_int() const {
    return g_value_get_int(this);
  }
  ZuInline void set_uint(guint v_uint) {
    g_value_set_uint(this, v_uint);
  }
  ZuInline guint get_uint() const {
    return g_value_get_uint(this);
  }
  ZuInline void set_long(glong v_long) {
    g_value_set_long(this, v_long);
  }
  ZuInline glong get_long() const {
    return g_value_get_long(this);
  }
  ZuInline void set_ulong(gulong v_ulong) {
    g_value_set_ulong(this, v_ulong);
  }
  ZuInline gulong get_ulong() const {
    return g_value_get_ulong(this);
  }
  ZuInline void set_int64(gint64 v_int64) {
    g_value_set_int64(this, v_int64);
  }
  ZuInline gint64 get_int64() const {
    return g_value_get_int64(this);
  }
  ZuInline void set_uint64(guint64 v_uint64) {
    g_value_set_uint64(this, v_uint64);
  }
  ZuInline guint64 get_uint64() const {
    return g_value_get_uint64(this);
  }
  ZuInline void set_float(gfloat v_float) {
    g_value_set_float(this, v_float);
  }
  ZuInline gfloat get_float() const {
    return g_value_get_float(this);
  }
  ZuInline void set_double(gdouble v_double) {
    g_value_set_double(this, v_double);
  }
  ZuInline gdouble get_double() const {
    return g_value_get_double(this);
  }
  ZuInline void set_string(const gchar *v_string) {
    g_value_set_string(this, v_string);
  }
  ZuInline void set_static_string(const gchar *v_string) {
    g_value_set_static_string(this, v_string);
  }
  ZuInline const gchar *get_string() const {
    return g_value_get_string(this);
  }
  ZuInline gchar* dup_string() const {
    return g_value_dup_string(this);
  }
  ZuInline void set_pointer(gpointer v_pointer) {
    g_value_set_pointer(this, v_pointer);
  }
  ZuInline gpointer get_pointer() const {
    return g_value_get_pointer(this);
  }
  ZuInline void set_gtype(GType v_gtype) {
    g_value_set_gtype(this, v_gtype);
  }
  ZuInline GType get_gtype() const {
    return g_value_get_gtype(this);
  }
  ZuInline void set_variant(GVariant *variant) {
    g_value_set_variant(this, variant);
  }
  ZuInline void take_variant(GVariant *variant) {
    g_value_take_variant(this, variant);
  }
  ZuInline GVariant* get_variant() const {
    return g_value_get_variant(this);
  }
  ZuInline GVariant* dup_variant() const {
    return g_value_dup_variant(this);
  }

  void set_boxed(gconstpointer v_boxed) {
    g_value_set_boxed(this, v_boxed);
  }
  void set_static_boxed(gconstpointer v_boxed) {
    g_value_set_static_boxed(this, v_boxed);
  }
  void take_boxed(gconstpointer v_boxed) {
    g_value_take_boxed(this, v_boxed);
  }
  gpointer get_boxed() const {
    return g_value_get_boxed(this);
  }
  gpointer dup_boxed() const {
    return g_value_dup_boxed(this);
  }
};

} // ZGtk

#endif /* ZGtkValue_HH */
