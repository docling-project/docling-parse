// SPDX-License-Identifier: MIT

#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <semaphore>

#include <pybind11/embed.h>
#include <pybind/docling_threaded_base.h>

namespace
{
  using namespace std::chrono_literals;

  void require(bool condition, const char* message)
  {
    if(not condition)
      {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
      }
  }

  class gated_parser :
    public docling::docling_threaded_base<gated_parser, docling::page_decode_result>
  {
  public:

    explicit gated_parser(int capacity): docling_threaded_base("fatal", 1, capacity, {}) {}

    void add_schedule(const std::string& key, std::vector<int> pages)
    {
      key2doc.emplace(key, nullptr);
      key2scheduled_pages.emplace(key, std::move(pages));
    }

    std::string first_key() const
    {
      return key2scheduled_pages.begin()->first;
    }

    void worker_loop()
    {
      while(auto task = take_task())
        {
          const auto& [key, page] = *task;
          docling::page_decode_result result;
          result.doc_key = key;
          result.page_number = page;
          result.success = true;
          before_publish(key, page);
          publish_result(std::move(result));
          after_publish(key, page);
        }
      finish_worker();
    }

    std::function<void(const std::string&, int)> before_publish = [](const auto&, int) {};
    std::function<void(const std::string&, int)> after_publish = [](const auto&, int) {};
  };

  void evict_buffered_results_and_end_iteration()
  {
    std::binary_semaphore claimed{0};
    std::binary_semaphore finish{0};
    gated_parser parser(2);
    parser.add_schedule("cancelled", {0, 1, 2, 3, 4});
    parser.before_publish = [&](const std::string&, int page) {
      if(page == 2)
        {
          claimed.release();
          finish.acquire();
        }
    };

    require(parser.has_tasks(), "the document should have scheduled work");
    require(claimed.try_acquire_for(5s), "the worker did not claim the third page");
    require(parser.cancel_document("cancelled") == 4, "expected two pending pages and two buffered results");
    require(parser.cancel_document("cancelled") == 0, "repeat cancellation removed work");

    std::binary_semaphore consuming{0};
    auto consumer = std::async(std::launch::async, [&] {
      consuming.release();
      return parser.get_task();
    });
    require(consuming.try_acquire_for(5s), "the consumer did not start");
    require(consumer.wait_for(0s) == std::future_status::timeout, "the claimed page is still in flight");
    finish.release();

    require(consumer.wait_for(5s) == std::future_status::ready, "the consumer did not wake at completion");
    require(consumer.get().page_number == -1, "a cancelled page reached the consumer");
    require(not parser.has_tasks(), "cancelled work is still counted");
    parser.unload_all_documents();
  }

  void discard_without_waiting_for_result_capacity()
  {
    std::binary_semaphore publishing{0};
    std::binary_semaphore published{0};
    gated_parser parser(1);
    parser.add_schedule("a", {0});
    parser.add_schedule("b", {0});
    const auto keep = parser.first_key();
    const std::string cancel = keep == "a" ? "b" : "a";

    // By the time the second document's page is claimed, the only result
    // slot holds the first document's page. Leave it there during cancellation.
    parser.before_publish = [&](const std::string& key, int) {
      if(key == cancel) { publishing.release(); }
    };
    parser.after_publish = [&](const std::string& key, int) {
      if(key == cancel) { published.release(); }
    };

    require(parser.has_tasks(), "both documents should have scheduled work");
    require(publishing.try_acquire_for(5s), "the worker did not reach publication");
    require(parser.cancel_document(cancel) == 0, "only an in-flight page should belong to the cancelled key");
    require(published.try_acquire_for(5s), "the cancelled page is still waiting for result capacity");
    require(parser.get_task().doc_key == keep, "cancellation evicted another document's result");
    require(not parser.has_tasks(), "the discarded result still counts as outstanding");
    parser.unload_all_documents();
  }
}

int main(int argc, char** argv)
{
  require(argc == 2, "expected the path to the Python executable");
  PyConfig config;
  PyConfig_InitPythonConfig(&config);
  const auto status = PyConfig_SetBytesString(&config, &config.program_name, argv[1]);
  require(not PyStatus_Exception(status), "could not set the Python program name");
  pybind11::scoped_interpreter interpreter(&config);
  evict_buffered_results_and_end_iteration();
  discard_without_waiting_for_result_capacity();
}
