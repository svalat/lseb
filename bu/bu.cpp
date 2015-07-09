#include <memory>
#include <string>
#include <algorithm>

#include "common/frequency_meter.h"
#include "common/timer.h"
#include "common/configuration.h"
#include "common/log.hpp"
#include "common/dataformat.h"
#include "transport/endpoints.h"
#include "transport/transport.h"

#include "bu/receiver.h"
#include "bu/builder.h"

using namespace lseb;

int main(int argc, char* argv[]) {

  assert(argc == 3 && "bu <config_file> <id>");

  std::ifstream f(argv[1]);
  if (!f) {
    std::cerr << argv[1] << ": No such file or directory\n";
    return EXIT_FAILURE;
  }
  Configuration configuration = read_configuration(f);

  Log::init(
    "BuilderUnit",
    Log::FromString(configuration.get<std::string>("BU.LOG_LEVEL")));

  LOG(INFO) << configuration << std::endl;

  int const max_fragment_size = configuration.get<int>(
    "GENERAL.MAX_FRAGMENT_SIZE");
  assert(max_fragment_size > 0);

  int const bulk_size = configuration.get<int>("GENERAL.BULKED_EVENTS");
  assert(bulk_size > 0);

  int const tokens = configuration.get<int>("GENERAL.TOKENS");
  assert(tokens > 0);

  std::vector<Endpoint> const ru_endpoints = get_endpoints(
    configuration.get_child("RU.ENDPOINTS"));
  std::vector<Endpoint> const bu_endpoints = get_endpoints(
    configuration.get_child("BU.ENDPOINTS"));

  std::chrono::milliseconds ms_timeout(configuration.get<int>("BU.MS_TIMEOUT"));

  size_t const data_size = max_fragment_size * bulk_size * tokens;

  int const bu_id = std::stol(argv[2]);
  assert(bu_id >= 0 && "Negative bu id");

  assert(bu_id < bu_endpoints.size() && "Wrong bu id");

  // Allocate memory

  std::unique_ptr<unsigned char[]> const data_ptr(
    new unsigned char[data_size * ru_endpoints.size()]);

  LOG(INFO)
    << "Allocated "
    << data_size * ru_endpoints.size()
    << " bytes of memory";

  // Connections

  BuSocket socket = lseb_listen(
    bu_endpoints[bu_id].hostname(),
    bu_endpoints[bu_id].port(),
    tokens);

  std::vector<BuConnectionId> connection_ids;

  LOG(INFO) << "Waiting for connections...";
  int endpoint_count = 0;
  std::transform(
    std::begin(ru_endpoints),
    std::end(ru_endpoints),
    std::back_inserter(connection_ids),
    [&](Endpoint const& endpoint) {
      BuConnectionId conn = lseb_accept(socket);
      lseb_register(conn, data_ptr.get() + endpoint_count++ * data_size,data_size);
      return conn;
    });
  LOG(INFO) << "Connections established";

  Builder builder;
  Receiver receiver(connection_ids);

  FrequencyMeter bandwith(1.0);
  Timer t_recv;
  Timer t_build;

  while (true) {

    std::map<int, std::vector<void*> > wrs_map;

    for (int conn = 0; conn < connection_ids.size(); ++conn) {
      t_recv.start();
      std::vector<iovec> conn_iov = receiver.receive(conn);
      t_recv.pause();
      if (conn_iov.size()) {
        /*
         t_build.start();
         builder.build(conn, conn_iov);
         t_build.pause();
         */

        auto it = wrs_map.insert(std::make_pair(conn, std::vector<void*>()));
        for (auto const& i : conn_iov) {
          it.first->second.push_back(i.iov_base);
        }

        bandwith.add(iovec_length(conn_iov));
      }
    }

    t_recv.start();
    receiver.release(wrs_map);
    t_recv.pause();

    if (bandwith.check()) {
      LOG(INFO) << "Bandwith: " << bandwith.frequency() / std::giga::num * 8. << " Gb/s";

      LOG(INFO)
        << "Times:\n"
        << "\tt_recv: "
        << t_recv.rate()
        << "%\n"
        << "\tt_build: "
        << t_build.rate()
        << "%";
      t_recv.reset();
      t_build.reset();
    }
  }

}

