//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// libdbus-1 reference service, caller, and signal subscriber.

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <dbus/dbus.h>

namespace Peer {

constexpr const char *Path = "/org/example/ZdbusPeer";
constexpr const char *Interface = "org.example.ZdbusPeer";
constexpr const char *PeerName = "org.example.ZdbusPeer";
constexpr const char *ServiceName = "org.example.ZdbusService";
constexpr const char *ErrorName = "org.example.ZdbusPeer.Rejected";
enum { ReplyTimeoutMs = 5000, MessageBudget = 64 };

static bool writeAll(const char *text, unsigned length)
{
  for (unsigned offset = 0; offset < length;) {
    int n = int(::write(STDOUT_FILENO, text + offset, length - offset));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    offset += unsigned(n);
  }
  return true;
}

static bool ready() { return writeAll("READY\n", 6); }

static DBusConnection *connect(const char *address)
{
  DBusError error;
  dbus_error_init(&error);
  DBusConnection *cxn = dbus_connection_open_private(address, &error);
  if (!cxn) {
    dbus_error_free(&error);
    return nullptr;
  }
  dbus_connection_set_exit_on_disconnect(cxn, FALSE);
  bool registered = dbus_bus_register(cxn, &error);
  dbus_error_free(&error);
  if (registered) return cxn;
  dbus_connection_close(cxn);
  dbus_connection_unref(cxn);
  return nullptr;
}

static void close(DBusConnection *cxn)
{
  dbus_connection_close(cxn);
  dbus_connection_unref(cxn);
}

static bool appendText(DBusMessage *msg, const char *text)
{
  return dbus_message_append_args(msg, DBUS_TYPE_STRING, &text,
    DBUS_TYPE_INVALID);
}

static bool send(DBusConnection *cxn, DBusMessage *msg)
{
  if (!msg) return false;
  bool ok = dbus_connection_send(cxn, msg, nullptr);
  dbus_message_unref(msg);
  if (ok) dbus_connection_flush(cxn);
  return ok;
}

static bool signal(DBusConnection *cxn)
{
  DBusMessage *msg = dbus_message_new_signal(Path, Interface, "Changed");
  if (!msg) return false;
  if (!appendText(msg, "changed")) {
    dbus_message_unref(msg);
    return false;
  }
  return send(cxn, msg);
}

static bool complexValue(DBusMessage *msg)
{
  if (strcmp(dbus_message_get_signature(msg), "(qu)aua{sv}v"))
    return false;
  DBusMessageIter top;
  if (!dbus_message_iter_init(msg, &top) ||
      dbus_message_iter_get_arg_type(&top) != DBUS_TYPE_STRUCT)
    return false;
  DBusMessageIter inner;
  dbus_message_iter_recurse(&top, &inner);
  dbus_uint16_t small = 0;
  dbus_uint32_t count = 0;
  if (dbus_message_iter_get_arg_type(&inner) != DBUS_TYPE_UINT16)
    return false;
  dbus_message_iter_get_basic(&inner, &small);
  if (!dbus_message_iter_next(&inner) ||
      dbus_message_iter_get_arg_type(&inner) != DBUS_TYPE_UINT32)
    return false;
  dbus_message_iter_get_basic(&inner, &count);
  if (small != 9 || count != 42 || dbus_message_iter_next(&inner) ||
      !dbus_message_iter_next(&top) ||
      dbus_message_iter_get_arg_type(&top) != DBUS_TYPE_ARRAY)
    return false;
  DBusMessageIter array;
  dbus_message_iter_recurse(&top, &array);
  dbus_uint32_t first = 0, second = 0;
  if (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_UINT32)
    return false;
  dbus_message_iter_get_basic(&array, &first);
  if (!dbus_message_iter_next(&array) ||
      dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_UINT32)
    return false;
  dbus_message_iter_get_basic(&array, &second);
  if (first != 1 || second != 2 || dbus_message_iter_next(&array) ||
      !dbus_message_iter_next(&top) ||
      dbus_message_iter_get_arg_type(&top) != DBUS_TYPE_ARRAY)
    return false;
  DBusMessageIter map;
  dbus_message_iter_recurse(&top, &map);
  if (dbus_message_iter_get_arg_type(&map) != DBUS_TYPE_DICT_ENTRY)
    return false;
  DBusMessageIter entry;
  dbus_message_iter_recurse(&map, &entry);
  if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
    return false;
  const char *key = nullptr;
  dbus_message_iter_get_basic(&entry, &key);
  if (strcmp(key, "answer") || !dbus_message_iter_next(&entry) ||
      dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT)
    return false;
  DBusMessageIter mapped;
  dbus_message_iter_recurse(&entry, &mapped);
  dbus_uint32_t answer = 0;
  if (dbus_message_iter_get_arg_type(&mapped) != DBUS_TYPE_UINT32)
    return false;
  dbus_message_iter_get_basic(&mapped, &answer);
  if (answer != 42 || dbus_message_iter_next(&mapped) ||
      dbus_message_iter_next(&entry) || dbus_message_iter_next(&map) ||
      !dbus_message_iter_next(&top) ||
      dbus_message_iter_get_arg_type(&top) != DBUS_TYPE_VARIANT)
    return false;
  DBusMessageIter choice;
  dbus_message_iter_recurse(&top, &choice);
  if (dbus_message_iter_get_arg_type(&choice) != DBUS_TYPE_STRING)
    return false;
  const char *tag = nullptr;
  dbus_message_iter_get_basic(&choice, &tag);
  return !strcmp(tag, "tag") && !dbus_message_iter_next(&choice) &&
    !dbus_message_iter_next(&top);
}

static bool appendComplex(DBusMessage *msg)
{
  DBusMessageIter top, inner, array, map, entry, mapped, choice;
  dbus_message_iter_init_append(msg, &top);
  if (!dbus_message_iter_open_container(&top, DBUS_TYPE_STRUCT,
      nullptr, &inner)) return false;
  dbus_uint16_t small = 9;
  dbus_uint32_t count = 42;
  if (!dbus_message_iter_append_basic(&inner, DBUS_TYPE_UINT16, &small) ||
      !dbus_message_iter_append_basic(&inner, DBUS_TYPE_UINT32, &count) ||
      !dbus_message_iter_close_container(&top, &inner)) return false;
  if (!dbus_message_iter_open_container(&top, DBUS_TYPE_ARRAY, "u",
      &array)) return false;
  dbus_uint32_t first = 1, second = 2;
  if (!dbus_message_iter_append_basic(&array, DBUS_TYPE_UINT32, &first) ||
      !dbus_message_iter_append_basic(&array, DBUS_TYPE_UINT32, &second) ||
      !dbus_message_iter_close_container(&top, &array)) return false;
  if (!dbus_message_iter_open_container(&top, DBUS_TYPE_ARRAY, "{sv}",
      &map) ||
      !dbus_message_iter_open_container(&map, DBUS_TYPE_DICT_ENTRY,
        nullptr, &entry)) return false;
  const char *key = "answer";
  dbus_uint32_t answer = 42;
  if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key) ||
      !dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
        "u", &mapped) ||
      !dbus_message_iter_append_basic(&mapped, DBUS_TYPE_UINT32,
        &answer) ||
      !dbus_message_iter_close_container(&entry, &mapped) ||
      !dbus_message_iter_close_container(&map, &entry) ||
      !dbus_message_iter_close_container(&top, &map)) return false;
  if (!dbus_message_iter_open_container(&top, DBUS_TYPE_VARIANT,
      "s", &choice)) return false;
  const char *tag = "tag";
  return dbus_message_iter_append_basic(&choice, DBUS_TYPE_STRING, &tag) &&
    dbus_message_iter_close_container(&top, &choice);
}

static bool serviceCall(DBusConnection *cxn, DBusMessage *msg,
  bool &done)
{
  if (!dbus_message_has_path(msg, Path) ||
      !dbus_message_has_interface(msg, Interface)) return true;
  if (dbus_message_is_method_call(msg, Interface, "Quit")) {
    done = true;
    return send(cxn, dbus_message_new_method_return(msg));
  }
  if (dbus_message_is_method_call(msg, Interface, "Emit")) {
    return send(cxn, dbus_message_new_method_return(msg)) &&
      signal(cxn);
  }
  if (dbus_message_is_method_call(msg, Interface, "Echo")) {
    DBusError error;
    dbus_error_init(&error);
    const char *text = nullptr;
    bool valid = dbus_message_get_args(msg, &error, DBUS_TYPE_STRING,
      &text, DBUS_TYPE_INVALID);
    dbus_error_free(&error);
    if (!valid) return false;
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return false;
    if (!appendText(reply, text)) {
      dbus_message_unref(reply);
      return false;
    }
    return send(cxn, reply);
  }
  if (dbus_message_is_method_call(msg, Interface, "Complex")) {
    if (!complexValue(msg)) return false;
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return false;
    if (!appendComplex(reply)) {
      dbus_message_unref(reply);
      return false;
    }
    return send(cxn, reply);
  }
  if (dbus_message_is_method_call(msg, Interface, "OddReply")) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (!reply) return false;
    dbus_uint32_t value = 7;
    if (!dbus_message_append_args(reply, DBUS_TYPE_UINT32, &value,
        DBUS_TYPE_INVALID)) {
      dbus_message_unref(reply);
      return false;
    }
    return send(cxn, reply);
  }
  if (dbus_message_is_method_call(msg, Interface, "OtherError")) {
    DBusMessage *reply = dbus_message_new_error(msg,
      "org.example.ZdbusPeer.Other", nullptr);
    if (!reply) return false;
    dbus_uint32_t value = 99;
    if (!dbus_message_append_args(reply, DBUS_TYPE_UINT32, &value,
        DBUS_TYPE_INVALID)) {
      dbus_message_unref(reply);
      return false;
    }
    return send(cxn, reply);
  }
  if (dbus_message_is_method_call(msg, Interface, "Fail")) {
    DBusMessage *reply = dbus_message_new_error(msg, ErrorName, nullptr);
    if (!reply) return false;
    dbus_uint32_t code = 23;
    const char *text = "rejected";
    if (!dbus_message_append_args(reply, DBUS_TYPE_UINT32, &code,
        DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID)) {
      dbus_message_unref(reply);
      return false;
    }
    return send(cxn, reply);
  }
  return send(cxn, dbus_message_new_error(msg,
    DBUS_ERROR_UNKNOWN_METHOD, "unknown method"));
}

static bool service(DBusConnection *cxn)
{
  DBusError error;
  dbus_error_init(&error);
  int owner = dbus_bus_request_name(cxn, PeerName,
    DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
  dbus_error_free(&error);
  if (owner != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER || !ready())
    return false;
  bool done = false;
  while (!done && dbus_connection_read_write(cxn, -1)) {
    DBusMessage *msg;
    while ((msg = dbus_connection_pop_message(cxn))) {
      bool ok = serviceCall(cxn, msg, done);
      dbus_message_unref(msg);
      if (!ok) return false;
      if (done) return true;
    }
  }
  return false;
}

static DBusMessage *awaitReply(DBusConnection *cxn, DBusMessage *msg)
{
  if (!msg) return nullptr;
  DBusPendingCall *pending = nullptr;
  bool queued = dbus_connection_send_with_reply(cxn, msg, &pending,
    int(ReplyTimeoutMs));
  dbus_uint32_t serial = dbus_message_get_serial(msg);
  dbus_message_unref(msg);
  if (!queued || !pending) return nullptr;
  dbus_connection_flush(cxn);
  dbus_pending_call_block(pending);
  DBusMessage *reply = dbus_pending_call_steal_reply(pending);
  dbus_pending_call_unref(pending);
  if (reply && dbus_message_get_reply_serial(reply) == serial) return reply;
  if (reply) dbus_message_unref(reply);
  return nullptr;
}

static DBusMessage *request(DBusConnection *cxn, const char *name,
  const char *member, const char *text = nullptr)
{
  DBusMessage *msg = dbus_message_new_method_call(name, Path,
    Interface, member);
  if (!msg) return nullptr;
  if (text && !appendText(msg, text)) {
    dbus_message_unref(msg);
    return nullptr;
  }
  return awaitReply(cxn, msg);
}

static DBusMessage *requestComplex(DBusConnection *cxn)
{
  DBusMessage *msg = dbus_message_new_method_call(ServiceName, Path,
    Interface, "Complex");
  if (!msg) return nullptr;
  if (!appendComplex(msg)) {
    dbus_message_unref(msg);
    return nullptr;
  }
  return awaitReply(cxn, msg);
}

static bool isText(DBusMessage *msg, const char *expected)
{
  DBusError error;
  dbus_error_init(&error);
  const char *text = nullptr;
  bool valid = dbus_message_get_args(msg, &error, DBUS_TYPE_STRING,
    &text, DBUS_TYPE_INVALID);
  dbus_error_free(&error);
  return valid && !strcmp(text, expected);
}

static bool isError(DBusMessage *msg)
{
  if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_ERROR ||
      !dbus_message_is_error(msg, ErrorName)) return false;
  DBusError error;
  dbus_error_init(&error);
  dbus_uint32_t code = 0;
  const char *text = nullptr;
  bool valid = dbus_message_get_args(msg, &error, DBUS_TYPE_UINT32,
    &code, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
  dbus_error_free(&error);
  return valid && code == 23 && !strcmp(text, "rejected");
}

static bool matchSignals(DBusConnection *cxn)
{
  DBusError error;
  dbus_error_init(&error);
  dbus_bus_add_match(cxn,
    "type='signal',interface='org.example.ZdbusPeer',"
    "member='Changed',path='/org/example/ZdbusPeer'", &error);
  bool ok = !dbus_error_is_set(&error);
  dbus_error_free(&error);
  if (ok) dbus_connection_flush(cxn);
  return ok;
}

static bool waitSignal(DBusConnection *cxn)
{
  for (unsigned budget = MessageBudget; budget--;) {
    DBusMessage *msg = dbus_connection_pop_message(cxn);
    if (!msg) {
      if (!dbus_connection_read_write(cxn, int(ReplyTimeoutMs)))
        return false;
      msg = dbus_connection_pop_message(cxn);
      if (!msg) return false;
    }
    const char *sender = dbus_message_get_sender(msg);
    bool found = dbus_message_is_signal(msg, Interface, "Changed") &&
      dbus_message_has_path(msg, Path) &&
      !strcmp(dbus_message_get_signature(msg), "s") &&
      dbus_message_get_serial(msg) && sender && *sender &&
      isText(msg, "changed");
    dbus_message_unref(msg);
    if (found) return true;
  }
  return false;
}

static bool subscriber(DBusConnection *cxn)
{
  return matchSignals(cxn) && ready() && waitSignal(cxn);
}

static bool caller(DBusConnection *cxn)
{
  if (!matchSignals(cxn) || !ready()) return false;
  DBusMessage *reply = request(cxn, ServiceName, "Echo", "hello");
  bool ok = reply && dbus_message_get_type(reply) ==
    DBUS_MESSAGE_TYPE_METHOD_RETURN && isText(reply, "hello");
  if (reply) dbus_message_unref(reply);
  if (!ok) return false;
  reply = requestComplex(cxn);
  ok = reply && dbus_message_get_type(reply) ==
    DBUS_MESSAGE_TYPE_METHOD_RETURN && complexValue(reply);
  if (reply) dbus_message_unref(reply);
  if (!ok) return false;
  reply = request(cxn, ServiceName, "Fail", "ignored");
  ok = reply && isError(reply);
  if (reply) dbus_message_unref(reply);
  if (!ok) return false;
  reply = request(cxn, ServiceName, "Emit");
  ok = reply && dbus_message_get_type(reply) ==
    DBUS_MESSAGE_TYPE_METHOD_RETURN;
  if (reply) dbus_message_unref(reply);
  return ok && waitSignal(cxn);
}

} // Peer

int main(int argc, char **argv)
{
  if (argc != 3) return 2;
  DBusConnection *cxn = Peer::connect(argv[1]);
  if (!cxn) return 1;
  bool ok = false;
  if (!strcmp(argv[2], "service")) ok = Peer::service(cxn);
  else if (!strcmp(argv[2], "caller")) ok = Peer::caller(cxn);
  else if (!strcmp(argv[2], "subscriber"))
    ok = Peer::subscriber(cxn);
  Peer::close(cxn);
  return ok ? 0 : 1;
}
