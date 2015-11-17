#ifndef RU_ACCUMULATOR_H
#define RU_ACCUMULATOR_H

#include <deque>

#include "common/dataformat.h"

#include "ru/controller.h"

namespace lseb {

class Accumulator {
  Controller m_controller;
  MetaDataRange m_metadata_range;
  DataRange m_data_range;
  MetaDataRange::iterator m_current_metadata;
  int m_events_in_multievent;
  int m_required_multievents;
  int m_generated_events;
  std::deque<std::pair<void*, bool> > m_iov_multievents;
  MetaDataRange::iterator m_release_metadata;

 public:
  Accumulator(
    Controller const& controller,
    MetaDataRange const& metadata_range,
    DataRange const& data_range,
    int events_in_multievent,
    int required_multievents);
  std::vector<iovec> get_multievents();
  void release_multievents(std::vector<void*> const& vect);
  DataRange data_range() {
    return m_data_range;
  }
};

}

#endif
