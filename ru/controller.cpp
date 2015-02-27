#include "ru/controller.h"

#include <chrono>

#include <cmath>

#include "common/log.h"
#include "common/utility.h"

namespace lseb {

Controller::Controller(Generator const& generator,
                       MetaDataRange const& metadata_range,
                       SharedQueue<MetaDataRange>& ready_events_queue,
                       SharedQueue<MetaDataRange>& sent_events_queue)
    : m_generator(generator),
      m_metadata_range(metadata_range),
      m_ready_events_queue(ready_events_queue),
      m_sent_events_queue(sent_events_queue) {
}

void Controller::operator()(size_t generator_frequency) {

  auto const start_time = std::chrono::high_resolution_clock::now();
  size_t tot_generated_events = 0;
  MetaDataRange::iterator current_metadata = m_metadata_range.begin();

  while (true) {
    double const elapsed_seconds = std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - start_time).count();

    size_t const tot_events_to_generate = elapsed_seconds * generator_frequency;

    assert(tot_events_to_generate >= tot_generated_events);

    size_t const generated_events = m_generator.generateEvents(
        tot_events_to_generate - tot_generated_events);

    // Send generated events
    if (generated_events) {
      MetaDataRange::iterator previous_metadata = current_metadata;
      current_metadata = advance_in_range(current_metadata, generated_events,
                                          m_metadata_range);
      m_ready_events_queue.push(
          MetaDataRange(previous_metadata, current_metadata));
      tot_generated_events += generated_events;
    }

    // Receive events to release
    while (!m_sent_events_queue.empty()) {
      MetaDataRange metadata_subrange(m_sent_events_queue.pop());
      size_t const events_to_release = distance_in_range(metadata_subrange,
                                                         m_metadata_range);
      m_generator.releaseEvents(events_to_release);
    }
  }

}

}
