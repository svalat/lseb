#include <iostream>

#include <boost/program_options.hpp>

#include "common/memory_pool.h"
#include "common/frequency_meter.h"

#include "transport/transport.h"

using namespace lseb;

int main(int argc, char* argv[]) {

  boost::program_options::options_description desc("Options");

  std::string server;
  std::string port;
  size_t chunk_size;
  int credits;

  desc.add_options()("help,h", "Print help messages.")(
    "server,s",
    boost::program_options::value<std::string>(&server)->required(),
    "Server.")(
    "port,p",
    boost::program_options::value<std::string>(&port)->required(),
    "Port.")(
    "chunk_size,c",
    boost::program_options::value<size_t>(&chunk_size)->required(),
    "Buffer size.")(
    "credits,C",
    boost::program_options::value<int>(&credits)->required(),
    "Credits.");

  try {
    boost::program_options::variables_map vm;
    boost::program_options::store(
      boost::program_options::command_line_parser(argc, argv).options(desc).run(),
      vm);
    if (vm.count("help")) {
      std::cout << desc << std::endl;
      return EXIT_SUCCESS;
    }
    boost::program_options::notify(vm);
  } catch (const boost::program_options::error& e) {
    std::cerr << e.what() << std::endl << desc << std::endl;
    return EXIT_FAILURE;
  }

  size_t const buffer_size = chunk_size * credits;

  std::cout << "Allocating " << buffer_size << " bytes of memory" << std::endl;

  std::unique_ptr<unsigned char[]> const buffer_ptr(
    new unsigned char[buffer_size]);

  MemoryPool pool(buffer_ptr.get(), buffer_size, chunk_size);

  Connector<SendSocket> connector(credits);
  std::unique_ptr<SendSocket> socket(connector.connect(server, port));
  socket->register_memory(buffer_ptr.get(), buffer_size);
  std::cout << "Connected to " << server << " on port " << port << std::endl;

  FrequencyMeter bandwith(5.0);

  while (true) {
    std::vector<iovec> vect = socket->pop_completed();
    for (auto& iov : vect) {
      bandwith.add(iov.iov_len);
      pool.free({iov.iov_base, chunk_size});
    }
    if (socket->pending() != credits) {
      socket->post_send(pool.alloc());
    }
    if (bandwith.check()) {
      std::cout
        << "Bandwith: "
        << bandwith.frequency() / std::giga::num * 8.
        << " Gb/s"
        << std::endl;
    }
  }
}
