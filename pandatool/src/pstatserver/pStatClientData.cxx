// Filename: pStatClientData.cxx
// Created by:  drose (11Jul00)
// 
////////////////////////////////////////////////////////////////////

#include "pStatClientData.h"
#include "pStatReader.h"

#include <pStatCollectorDef.h>

PStatCollectorDef PStatClientData::_null_collector(-1, "Unknown");



////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::Constructor
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
PStatClientData::
PStatClientData(PStatReader *reader) :
  _reader(reader)
{
  _is_alive = true;
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::Destructor
//       Access: Public
//  Description: 
////////////////////////////////////////////////////////////////////
PStatClientData::
~PStatClientData() {
  Collectors::const_iterator ci;
  for (ci = _collectors.begin(); ci != _collectors.end(); ++ci) {
    delete (*ci);
  }
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::is_alive
//       Access: Public
//  Description: Returns true if the data is actively getting filled
//               by a connected client, or false if the client has
//               terminated.
////////////////////////////////////////////////////////////////////
bool PStatClientData::
is_alive() const {
  return _is_alive;
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::close
//       Access: Public
//  Description: Closes the client connection if it is open.
////////////////////////////////////////////////////////////////////
void PStatClientData::
close() {
  if (_is_alive && _reader != (PStatReader *)NULL) {
    _reader->close();
    _reader = (PStatReader *)NULL;
    _is_alive = false;
  }
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::get_num_collectors
//       Access: Public
//  Description: Returns the total number of collectors the Data
//               knows about.
////////////////////////////////////////////////////////////////////
int PStatClientData::
get_num_collectors() const {
  return _collectors.size();
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::has_collector
//       Access: Public
//  Description: Returns true if the indicated collector has been
//               defined by the client already, false otherwise.  It
//               is possible for the client to start streaming data
//               before all of the collectors have been defined.
////////////////////////////////////////////////////////////////////
bool PStatClientData::
has_collector(int index) const {
  return (index >= 0 && index < (int)_collectors.size() &&
	  _collectors[index] != (PStatCollectorDef *)NULL);
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::get_collector_def
//       Access: Public
//  Description: Returns the nth collector definition.
////////////////////////////////////////////////////////////////////
const PStatCollectorDef &PStatClientData::
get_collector_def(int index) const {
  if (!has_collector(index)) {
    return _null_collector;
  }
  return *_collectors[index];
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::get_collector_name
//       Access: Public
//  Description: Returns the name of the indicated collector.
////////////////////////////////////////////////////////////////////
string PStatClientData::
get_collector_name(int index) const {
  if (!has_collector(index)) {
    return "Unknown";
  }
  const PStatCollectorDef *def = _collectors[index];
  return def->_name;
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::get_collector_fullname
//       Access: Public
//  Description: Returns the "full name" of the indicated collector.
//               This will be the concatenation of all of the
//               collector's parents' names (except Frame) and the
//               collector's own name.
////////////////////////////////////////////////////////////////////
string PStatClientData::
get_collector_fullname(int index) const {
  if (!has_collector(index)) {
    return "Unknown";
  }

  const PStatCollectorDef *def = _collectors[index];
  if (def->_parent_index == 0) {
    return def->_name;
  } else {
    return get_collector_fullname(def->_parent_index) + ":" + def->_name;
  }
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::get_num_threads
//       Access: Public
//  Description: Returns the total number of threads the Data
//               knows about.
////////////////////////////////////////////////////////////////////
int PStatClientData::
get_num_threads() const {
  return _threads.size();
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::has_thread
//       Access: Public
//  Description: Returns true if the indicated thread has been
//               defined by the client already, false otherwise.  It
//               is possible for the client to start streaming data
//               before all of the threads have been defined.
////////////////////////////////////////////////////////////////////
bool PStatClientData::
has_thread(int index) const {
  return (index >= 0 && index < (int)_threads.size() &&
	  !_threads[index]._name.empty());
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::get_thread_name
//       Access: Public
//  Description: Returns the name of the indicated thread.
////////////////////////////////////////////////////////////////////
string PStatClientData::
get_thread_name(int index) const {
  if (!has_thread(index)) {
    return "Unknown";
  }
  return _threads[index]._name;
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::get_thread_data
//       Access: Public
//  Description: Returns the data associated with the indicated
//               thread.  This will create a thread definition if it
//               does not already exist.
////////////////////////////////////////////////////////////////////
const PStatThreadData *PStatClientData::
get_thread_data(int index) const {
  ((PStatClientData *)this)->define_thread(index);
  nassertr(index >= 0 && index < (int)_threads.size(), NULL);
  return _threads[index]._data;
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::get_child_distance
//       Access: Public
//  Description: Returns the number of Collectors between the
//               indicated parent and the child Collector in the
//               relationship graph.  If child is the same as parent,
//               returns zero.  If child is an immediate child of
//               parent, returns 1.  If child is a grandchild of
//               parent, returns 2, and so on.  If child is not a
//               descendant of parent at all, returns -1.
////////////////////////////////////////////////////////////////////
int PStatClientData::
get_child_distance(int parent, int child) const {
  if (parent == child) {
    return 0;
  }
  if (!has_collector(child) || child == 0) {
    return -1;
  }
  int dist = get_child_distance(parent, get_collector_def(child)._parent_index);
  if (dist == -1) {
    return -1;
  } else {
    return dist + 1;
  }
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::add_collector
//       Access: Public
//  Description: Adds a new collector definition to the dataset.
//               Presumably this is information just arrived from the
//               client.
//
//               The pointer will become owned by the PStatClientData
//               object and will be freed on destruction.
////////////////////////////////////////////////////////////////////
void PStatClientData::
add_collector(PStatCollectorDef *def) {
  // A sanity check on the index number.
  nassertv(def->_index < 1000);

  // Make sure we have enough slots allocated.
  while (_collectors.size() <= def->_index) {
    _collectors.push_back(NULL);
  }

  if (_collectors[def->_index] != (PStatCollectorDef *)NULL) {
    // Free any old definition.
    delete _collectors[def->_index];
  }

  _collectors[def->_index] = def;
}

////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::define_thread
//       Access: Public
//  Description: Adds a new thread definition to the dataset.
//               Presumably this is information just arrived from the
//               client.
////////////////////////////////////////////////////////////////////
void PStatClientData::
define_thread(int thread_index, const string &name) {
  // A sanity check on the index number.
  nassertv(thread_index < 1000);

  // Make sure we have enough slots allocated.
  while (_threads.size() <= thread_index) {
    _threads.push_back(Thread());
  }

  if (!name.empty()) {
    _threads[thread_index]._name = name;
  }

  if (_threads[thread_index]._data.is_null()) {
    _threads[thread_index]._data = new PStatThreadData(this);
  }
}


////////////////////////////////////////////////////////////////////
//     Function: PStatClientData::record_new_frame
//       Access: Public
//  Description: Makes room for and stores a new frame's worth of
//               data associated with some particular thread (which
//               may or may not have already been defined).
//
//               The pointer will become owned by the PStatThreadData
//               object and will be freed on destruction.
////////////////////////////////////////////////////////////////////
void PStatClientData::
record_new_frame(int thread_index, int frame_number,
		 PStatFrameData *frame_data) {
  define_thread(thread_index);
  nassertv(thread_index >= 0 && thread_index < (int)_threads.size());
  _threads[thread_index]._data->record_new_frame(frame_number, frame_data);
}
