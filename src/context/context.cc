#include "context/context.h"

#include <cstdio>
#include <exception>
#include <utility>

namespace buried {

void Context::Start() {
  std::lock_guard<std::mutex> lock(start_mutex_);
  if (is_started_) {
    return;
  }

  main_context_.restart();
  report_context_.restart();
  main_work_guard_ =
      std::make_unique<WorkGuard>(main_context_.get_executor());
  report_work_guard_ =
      std::make_unique<WorkGuard>(report_context_.get_executor());

  std::unique_ptr<std::thread> main_thread;
  try {
    main_thread = std::make_unique<std::thread>(
        [this]() { RunContext_(main_context_); });
    auto report_thread = std::make_unique<std::thread>(
        [this]() { RunContext_(report_context_); });
    main_thread_ = std::move(main_thread);
    report_thread_ = std::move(report_thread);
    is_started_ = true;
  } catch (...) {
    main_work_guard_.reset();
    report_work_guard_.reset();
    main_context_.stop();
    report_context_.stop();
    if (main_thread) {
      main_thread->join();
    }
    throw;
  }
}

Context::~Context() {
  {
    std::lock_guard<std::mutex> lock(start_mutex_);
    if (!is_started_) {
      return;
    }

    main_strand_.Close(Strand::CloseMode::kCancel);
    report_strand_.Close(Strand::CloseMode::kCancel);
    main_work_guard_.reset();
    report_work_guard_.reset();
    main_context_.stop();
    report_context_.stop();
    is_started_ = false;
  }

  if (main_thread_) {
    main_thread_->join();
  }
  if (report_thread_) {
    report_thread_->join();
  }
}

void Context::RunContext_(IOContext& context) {
  for (;;) {
    try {
      context.run();
      return;
    } catch (const std::exception& error) {
      std::fprintf(stderr, "Buried context handler exception: %s\n",
                   error.what());
    } catch (...) {
      std::fprintf(stderr,
                   "Buried context handler threw an unknown exception\n");
    }
  }
}

}  // namespace buried
