// Filename: pStatListener.cxx
// Created by:  drose (09Jul00)
// 
////////////////////////////////////////////////////////////////////

#include "pStatListener.h"
#include "pStatServer.h"
#include "pStatReader.h"

////////////////////////////////////////////////////////////////////
//     Function: PStatListener::Constructor
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
PStatListener::
PStatListener(PStatServer *manager) :
  ConnectionListener(manager, manager->is_thread_safe() ? 1 : 0),
  _manager(manager)
{
}

////////////////////////////////////////////////////////////////////
//     Function: PStatListener::connection_opened
//       Access: Protected, Virtual
//  Description: An internal function called by ConnectionListener()
//               when a new TCP connection has been established.
////////////////////////////////////////////////////////////////////
void PStatListener::
connection_opened(const PT(Connection) &, 
		  const NetAddress &address,
		  const PT(Connection) &new_connection) {
  PStatMonitor *monitor = _manager->make_monitor();
  if (monitor == (PStatMonitor *)NULL) {
    nout << "Couldn't create monitor!\n";
    return;
  }

  nout << "Got new connection from " << address.get_ip() << "\n";

  PStatReader *reader = new PStatReader(_manager, monitor);
  _manager->add_reader(new_connection, reader);
  reader->set_tcp_connection(new_connection);
}
