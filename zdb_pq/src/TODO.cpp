// Not a real .cpp file, notes for implementation

// note that back-end must maintain a "_mrd" table: (most recent deletes)
//   ZuID table	// PK
//   UN un	// update number of most recent delete
//   SN sn	// sequence number of most recent delete
//
// _mrd is updated together with the delete in a batch, to ensure EC:
//
// - deal with superseded UN recovery from data store - replica can skip
//
//    - async / pipelined
//    - prepared statements
//    - binary
//    - non-blocking FD
//    - figure out (like Gtk) how to integrate with event loop
//      - use the example async event loop code in the postgres source
//      - will use wfmo on Windows with two FDs, one for wakeup
//	- consider epoll for Linux
//
// use pipeline mode (https://www.postgresql.org/docs/current/libpq-pipeline-mode.html)
//
// testlibpq3.c tests binary I/O
// libpq_pipeline.c tests pipeline mode
//
// use libcommon pg as a reference
//
// prepare query - 
// send query - PQsendQueryPrepared
// flush queries (prior to close) - PQflush
// 
// https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaeventselect
// https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjectsex
// https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaenumnetworkevents

auto socket = PQsocket(PGconn *);
auto event = WSACreateEvent();
WSAEventSelect(socket, event, FD_READ | FD_WRITE | FD_OOB | FD_CLOSE);
// WSAEventSelect(socket, NULL, 0) disassociates event from socket
// WSACloseEvent(event) closes the event

HANDLE handles[2] = { event, sem };
DWORD event = WaitForMultipleObjectsEx(2, handles, FALSE, INFINITE, FALSE);
if (event == WAIT_FAILED) {
  // WFMO error
}
// WAIT_OBJECT_0 to (WAIT_OBJECT_0 + nCount– 1)
// the return value minus WAIT_OBJECT_0 indicates the lpHandles array index of
// the object that satisfied the wait. If more than one object became signaled
// during the call, this is the array index of the signaled object with the
// smallest index value of all the signaled objects.
if (event == WAIT_OBJECT_0) {
  // PQsocket active
  WSANETWORKEVENTS events;
  auto i = WSAEnumNetworkEvents(socket, event, &events);
  if (i != 0) { /* error - WSAGetLastError() */ }
  if (events.lNetworkEvents & (FD_READ|FD_OOB|FD_CLOSE)) { // consume data
  }
  if ((events.lNetworkEvents & (FD_WRITE|FD_CLOSE)) == FD_WRITE) { // write data
    // dequeue, send request
  }
}


// see ZiMultiplex.cc:1916 for epoll open and wake pipe setup
// see ZiMultiplex.cc:2043 for epoll close
{ // add PG socket - wake pipe is just EPOLLIN
  struct epoll_event ev;
  memset(&ev, 0, sizeof(struct epoll_event));
  ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
  ev.data.u64 = id; // ID - either PG socket or wake pipe
  epoll_ctl(m_epollFD, EPOLL_CTL_ADD, s, &ev);
}
epoll_ctl(m_epollFD, EPOLL_CTL_DEL, s, 0); // remove socket

epoll_event ev[8];
r = epoll_wait(m_epollFD, ev, 8, -1); // 8 is max events
for (unsigned i = 0; i < (unsigned)r; i++) {
uint32_t events = ev[i].events;
uintptr_t v = ev[i].data.u64; // ID
if (events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR))
  read();
if (events & EPOLLOUT)
  write();

