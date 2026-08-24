#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <thread>

#include "src/context/context.h"

int main() {
  buried::Context::GetGlobalContext().Start();
  auto remaining = std::make_shared<std::atomic<int>>(5);
  auto completion = std::make_shared<std::promise<void>>();
  auto completed = completion->get_future();
  auto finish = [remaining, completion]() {
    if (remaining->fetch_sub(1) == 1) {
      completion->set_value();
    }
  };

  buried::Context::GetGlobalContext().GetMainStrand().Post([finish]() {
    std::cout << "Operation 1 executed in strand1 on thread id "
              << std::this_thread::get_id() << std::endl;
    finish();
  });

  buried::Context::GetGlobalContext().GetReportStrand().Post([finish]() {
    std::cout << "Operation 2 executed in strand2 on thread id "
              << std::this_thread::get_id() << std::endl;
    finish();

    buried::Context::GetGlobalContext().GetReportStrand().Post([finish]() {
      std::cout << "Operation 3 executed in strand2 on thread id "
                << std::this_thread::get_id() << std::endl;
      finish();
    });

    buried::Context::GetGlobalContext().GetMainStrand().Post([finish]() {
      std::cout << "Operation 4 executed in strand1 on thread id "
                << std::this_thread::get_id() << std::endl;
      finish();
    });

    buried::Context::GetGlobalContext().GetReportStrand().Post([finish]() {
      std::cout << "Operation 5 executed in strand2 on thread id "
                << std::this_thread::get_id() << std::endl;
      finish();
    });
  });

  if (completed.wait_for(std::chrono::seconds(5)) !=
      std::future_status::ready) {
    std::cerr << "Timed out waiting for strand operations" << std::endl;
    return 1;
  }
  return 0;
}
