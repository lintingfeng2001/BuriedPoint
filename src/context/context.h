#pragma once

#include <memory>
#include <mutex>
#include <thread>

#include "boost/asio/executor_work_guard.hpp"
#include "boost/asio/io_context.hpp"
#include "context/strand.h"

namespace buried {

class Context {
 public:
  static Context& GetGlobalContext() {
    static Context global_context;
    return global_context;
  }

  ~Context();

  using IOContext = boost::asio::io_context;
  using WorkGuard = boost::asio::executor_work_guard<IOContext::executor_type>;

  Strand& GetMainStrand() { return main_strand_; }

  Strand& GetReportStrand() { return report_strand_; }

  IOContext& GetMainContext() { return main_context_; }

  IOContext& GetReportContext() { return report_context_; }

  void Start();

 private:
  Context() : main_strand_(main_context_), report_strand_(report_context_) {}

  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  static void RunContext_(IOContext& context);

 private:
  boost::asio::io_context main_context_;
  boost::asio::io_context report_context_;

  Strand main_strand_;
  Strand report_strand_;

  std::unique_ptr<WorkGuard> main_work_guard_;
  std::unique_ptr<WorkGuard> report_work_guard_;

  std::unique_ptr<std::thread> main_thread_;
  std::unique_ptr<std::thread> report_thread_;

  std::mutex start_mutex_;
  bool is_started_{false};
};

}  // namespace buried
