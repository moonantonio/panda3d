// Filename: pStatThreadData.h
// Created by:  drose (08Jul00)
// 
////////////////////////////////////////////////////////////////////

#ifndef PSTATTHREADDATA_H
#define PSTATTHREADDATA_H

#include <pandatoolbase.h>

#include <referenceCount.h>

#include <deque>

class PStatCollectorDef;
class PStatFrameData;
class PStatClientData;

////////////////////////////////////////////////////////////////////
// 	 Class : PStatThreadData
// Description : A collection of FrameData structures for
//               recently-received frames within a particular thread.
//               This holds the raw data as reported by the client,
//               and it automatically handles frames received
//               out-of-order or skipped.  You can ask for a
//               particular frame by frame number or time and receive
//               the data for the nearest frame.
////////////////////////////////////////////////////////////////////
class PStatThreadData : public ReferenceCount {
public:
  PStatThreadData(const PStatClientData *client_data);
  ~PStatThreadData();

  INLINE const PStatClientData *get_client_data() const;

  bool is_empty() const;

  int get_latest_frame_number() const;
  int get_oldest_frame_number() const;
  bool has_frame(int frame_number) const;
  const PStatFrameData &get_frame(int frame_number) const;

  double get_latest_time() const;
  double get_oldest_time() const;
  const PStatFrameData &get_frame_at_time(double time) const;
  int get_frame_number_at_time(double time, int hint = -1) const;

  const PStatFrameData &get_latest_frame() const;

  double get_frame_rate(double time = 3.0) const;


  void set_history(double time);
  double get_history() const;

  void record_new_frame(int frame_number, PStatFrameData *frame_data);

private:
  const PStatClientData *_client_data;

  typedef deque<PStatFrameData *> Frames;
  Frames _frames;
  int _first_frame_number;
  double _history;

  static PStatFrameData _null_frame;
};

#include "pStatThreadData.I"

#endif

