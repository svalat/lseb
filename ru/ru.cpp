#include <memory>
#include <string>
#include <cstdlib>
#include <cassert>
#include <algorithm>

#include "common/dataformat.h"
#include "common/log.h"
#include "common/utility.h"
#include "common/iniparser.hpp"
#include "common/frequency_meter.h"

#include "generator/generator.h"
#include "generator/length_generator.h"

#include "ru/controller.h"
#include "ru/accumulator.h"
#include "ru/sender.h"

#include "transport/endpoints.h"
#include "transport/transport.h"

using namespace lseb;

int main(int argc, char* argv[]) {

  assert(argc == 3 && "ru <config_file> <id>");

  Parser parser(argv[1]);
  size_t const ru_id = std::stol(argv[2]);

  Log::init("ReadoutUnit", Log::FromString(parser.top()("RU")["LOG_LEVEL"]));

  size_t const generator_frequency = std::stol(
    parser.top()("GENERATOR")["FREQUENCY"]);
  size_t const mean = std::stol(parser.top()("GENERATOR")["MEAN"]);
  size_t const stddev = std::stol(parser.top()("GENERATOR")["STD_DEV"]);
  size_t const data_size = std::stol(parser.top()("GENERAL")["DATA_BUFFER"]);
  size_t const bulk_size = std::stol(parser.top()("GENERAL")["BULKED_EVENTS"]);
  Endpoints const bu_endpoints = get_endpoints(parser.top()("BU")["ENDPOINTS"]);
  Endpoints const ru_endpoints = get_endpoints(parser.top()("RU")["ENDPOINTS"]);

  LOG(INFO) << parser << std::endl;

  assert(ru_id < ru_endpoints.size() && "Wrong ru id");

  std::vector<int> connection_ids;
  std::transform(
    std::begin(bu_endpoints),
    std::end(bu_endpoints),
    std::back_inserter(connection_ids),
    [](Endpoint const& endpoint) {
      return lseb_connect(endpoint.hostname(), endpoint.port());
    });

  size_t const max_buffered_events = data_size / (sizeof(EventHeader) + mean);
  size_t const metadata_size = max_buffered_events * sizeof(EventMetaData);

  LOG(INFO)
    << "Metadata buffer can contain "
    << max_buffered_events
    << " events";

  // Allocate memory

  std::unique_ptr<unsigned char[]> const metadata_ptr(
    new unsigned char[metadata_size]);
  std::unique_ptr<unsigned char[]> const data_ptr(new unsigned char[data_size]);

  MetaDataRange metadata_range(
    pointer_cast<EventMetaData>(metadata_ptr.get()),
    pointer_cast<EventMetaData>(metadata_ptr.get() + metadata_size));
  DataRange data_range(data_ptr.get(), data_ptr.get() + data_size);

  MetaDataBuffer metadata_buffer(
    std::begin(metadata_range),
    std::end(metadata_range));
  DataBuffer data_buffer(std::begin(data_range), std::end(data_range));

  LengthGenerator payload_size_generator(mean, stddev);
  Generator generator(
    payload_size_generator,
    metadata_buffer,
    data_buffer,
    ru_id);

  Controller controller(generator, metadata_range, generator_frequency);
  Accumulator accumulator(metadata_range, data_range, bulk_size);
  Sender sender(metadata_range, data_range, connection_ids);

  FrequencyMeter frequency(1.0);

  FrequencyMeter read(1.0);
  FrequencyMeter add(1.0);
  FrequencyMeter send(1.0);
  FrequencyMeter release(1.0);

  while (true) {
    auto t0 = std::chrono::high_resolution_clock::now();

    MetaDataRange ready_events = controller.read();
    auto t1 = std::chrono::high_resolution_clock::now();
    read.add(std::chrono::duration<double>(t1 - t0).count());

    MultiEvents multievents = accumulator.add(ready_events);
    t0 = std::chrono::high_resolution_clock::now();
    add.add(std::chrono::duration<double>(t0 - t1).count());

    if (multievents.size()) {
      size_t sent_bytes = sender.send(multievents);
      t1 = std::chrono::high_resolution_clock::now();
      send.add(std::chrono::duration<double>(t1 - t0).count());

      controller.release(
        MetaDataRange(
          std::begin(multievents.front().first),
          std::end(multievents.back().first)));
      t0 = std::chrono::high_resolution_clock::now();
      release.add(std::chrono::duration<double>(t0 - t1).count());

      frequency.add(multievents.size() * bulk_size);
    }

    if (frequency.check()) {
      LOG(INFO)
        << "Frequency: "
        << frequency.frequency() / std::mega::num
        << " MHz";
    }

    if (read.check()) {
      LOG(INFO) << "read: " << read.frequency() << " s";
    }
    if (add.check()) {
      LOG(INFO) << "add: " << add.frequency() << " s";
    }
    if (send.check()) {
      LOG(INFO) << "send: " << send.frequency() << " s";
    }
    if (release.check()) {
      LOG(INFO) << "release: " << release.frequency() << " s";
    }
  }
}
