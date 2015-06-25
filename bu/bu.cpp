#include <memory>
#include <string>
#include <algorithm>

#include "common/frequency_meter.h"
#include "common/timer.h"
#include "common/iniparser.hpp"
#include "common/log.hpp"
#include "common/dataformat.h"
#include "transport/endpoints.h"
#include "transport/transport.h"

#include "bu/receiver.h"
#include "bu/builder.h"

using namespace lseb;

int main(int argc, char* argv[]) {

  assert(argc == 3 && "bu <config_file> <id>");

  int const bu_id = std::stol(argv[2]);
  assert(bu_id >= 0 && "Negative bu id");

  Parser parser(argv[1]);

  Log::init("BuilderUnit", Log::FromString(parser.top()("BU")["LOG_LEVEL"]));

  Endpoints const ru_endpoints = get_endpoints(parser.top()("RU")["ENDPOINTS"]);
  Endpoints const bu_endpoints = get_endpoints(parser.top()("BU")["ENDPOINTS"]);
  size_t const data_size = std::stol(parser.top()("BU")["RECV_BUFFER"]);
  int const n_threads = std::stol(parser.top()("GENERAL")["LEVEL_PARALLELISM"]);
  std::chrono::milliseconds ms_timeout(
    std::stol(parser.top()("BU")["MS_TIMEOUT"]));

  LOG(INFO) << parser << std::endl;

  assert(bu_id < bu_endpoints.size() && "Wrong bu id");

  // Allocate memory

  std::unique_ptr<unsigned char[]> const data_ptr(
    new unsigned char[data_size * ru_endpoints.size()]);

  // Connections

  BuSocket socket = lseb_listen(
    bu_endpoints[bu_id].hostname(),
    bu_endpoints[bu_id].port());

  std::vector<BuConnectionId> connection_ids;

  LOG(INFO) << "Waiting for connections...";
  int endpoint_count = 0;
  std::transform(
    std::begin(ru_endpoints),
    std::end(ru_endpoints),
    std::back_inserter(connection_ids),
    [&](Endpoint const& endpoint) {
      return lseb_accept(
          socket,
          data_ptr.get() + endpoint_count++ * data_size,
          data_size
      );
    });
  LOG(INFO) << "Connections established";

  Receiver receiver(connection_ids);
  Builder builder(n_threads);

  FrequencyMeter bandwith(1.0);
  Timer t_recv;
  Timer t_build;

  while (true) {

    t_recv.start();
    std::vector<iovec> total_iov = receiver.receive(ms_timeout);
    t_recv.pause();

    bandwith.add(iovec_length(total_iov));

    t_build.start();
    builder.build(total_iov);
    t_build.pause();

    receiver.release();

    if (bandwith.check()) {
      LOG(INFO)
        << "Bandwith: "
        << bandwith.frequency() / std::giga::num * 8.
        << " Gb/s";

      LOG(INFO) << "Times:\n"
        << "\tt_recv: " << t_recv.rate() << "%\n"
        << "\tt_build: " << t_build.rate() << "%";
      t_recv.reset();
      t_build.reset();
    }
  }

}

