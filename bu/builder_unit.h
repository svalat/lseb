#ifndef BU_BUILDER_UNIT_H
#define BU_BUILDER_UNIT_H

#include <sys/uio.h>

#include <boost/lockfree/spsc_queue.hpp>

#include "transport/transport.h"
#include "transport/endpoints.h"

namespace lseb {

class BuilderUnit {
  boost::lockfree::spsc_queue<iovec>& m_free_local_queue;
  boost::lockfree::spsc_queue<iovec>& m_ready_local_queue;
  std::vector<Endpoint> m_endpoints;
  std::vector<RecvSocket> m_connection_ids;
  int m_bulk_size;
  int m_credits;
  int m_max_fragment_size;
  int m_id;

 public:
  BuilderUnit(
    boost::lockfree::spsc_queue<iovec>& free_local_data,
    boost::lockfree::spsc_queue<iovec>& ready_local_data,
    std::vector<Endpoint> const& endpoints,
    int bulk_size,
    int credits,
    int max_fragment_size,
    int id);
  void operator()();
};

}

#endif
