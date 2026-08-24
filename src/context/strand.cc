#include "context/strand.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>

#include "boost/asio/post.hpp"

namespace buried {
namespace {

thread_local const void* current_strand_state = nullptr;

class CurrentStrandScope {
 public:
  explicit CurrentStrandScope(const void* state)
      : previous_state_(current_strand_state) {
    current_strand_state = state;
  }

  ~CurrentStrandScope() { current_strand_state = previous_state_; }

 private:
  const void* previous_state_;
};

}  // namespace

class Strand::State : public std::enable_shared_from_this<Strand::State> {
 public:
  explicit State(boost::asio::io_context& context) : context_(context) {}

  bool Post(Task task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_) {
      return false;
    }

    tasks_.push_back(std::move(task));
    if (active_) {
      return true;
    }

    // boost::asio::post never invokes the handler inline, so scheduling while
    // holding this mutex closes the empty-queue/lost-wakeup race safely.
    active_ = true;
    try {
      boost::asio::post(context_,
                        [self = shared_from_this()]() { self->Drain_(); });
    } catch (...) {
      active_ = false;
      tasks_.pop_back();
      if (tasks_.empty()) {
        idle_condition_.notify_all();
      }
      throw;
    }
    return true;
  }

  void Close(CloseMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = false;
    if (mode == CloseMode::kCancel) {
      tasks_.clear();
    }
    if (!active_ && tasks_.empty()) {
      idle_condition_.notify_all();
    }
  }

  bool WaitIdle() {
    if (RunningInThisThread()) {
      return false;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    idle_condition_.wait(lock,
                         [this]() { return !active_ && tasks_.empty(); });
    return true;
  }

  bool RunningInThisThread() const noexcept {
    return current_strand_state == this;
  }

  bool IsOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return accepting_;
  }

 private:
  void Drain_() {
    std::exception_ptr first_exception;

    for (;;) {
      Task task;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tasks_.empty()) {
          active_ = false;
          idle_condition_.notify_all();
          break;
        }
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }

      CurrentStrandScope scope(this);
      try {
        task();
      } catch (...) {
        if (!first_exception) {
          first_exception = std::current_exception();
        }
      }
    }

    // Queue state is restored before propagating. Context catches and records
    // the exception, while standalone users may apply their own run() policy.
    if (first_exception) {
      std::rethrow_exception(first_exception);
    }
  }

  boost::asio::io_context& context_;
  mutable std::mutex mutex_;
  std::condition_variable idle_condition_;
  std::deque<Task> tasks_;
  bool active_{false};
  bool accepting_{true};
};

Strand::Strand(boost::asio::io_context& context)
    : state_(std::make_shared<State>(context)) {}

bool Strand::PostTask_(Task task) const {
  return state_->Post(std::move(task));
}

void Strand::Close(CloseMode mode) const { state_->Close(mode); }

bool Strand::WaitIdle() const { return state_->WaitIdle(); }

bool Strand::RunningInThisThread() const noexcept {
  return state_->RunningInThisThread();
}

bool Strand::IsOpen() const { return state_->IsOpen(); }

}  // namespace buried
