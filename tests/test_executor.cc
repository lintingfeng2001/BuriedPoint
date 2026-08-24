#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "boost/asio/error.hpp"
#include "boost/asio/executor_work_guard.hpp"
#include "boost/asio/io_context.hpp"
#include "boost/asio/steady_timer.hpp"
#include "gtest/gtest.h"
#include "src/context/context.h"
#include "src/context/strand.h"

namespace {

using namespace std::chrono_literals;

class IoContextThreads {
 public:
  IoContextThreads(boost::asio::io_context& context, int thread_count)
      : context_(context), guard_(context.get_executor()) {
    for (int i = 0; i < thread_count; ++i) {
      threads_.emplace_back([this]() { Run_(); });
    }
  }

  ~IoContextThreads() {
    guard_.reset();
    context_.stop();
    for (auto& thread : threads_) {
      thread.join();
    }
  }

  bool WaitForExceptions(int expected,
                         std::chrono::milliseconds timeout = 5s) {
    std::unique_lock<std::mutex> lock(exception_mutex_);
    return exception_condition_.wait_for(
        lock, timeout, [this, expected]() { return exceptions_ >= expected; });
  }

 private:
  void Run_() {
    for (;;) {
      try {
        context_.run();
        return;
      } catch (...) {
        {
          std::lock_guard<std::mutex> lock(exception_mutex_);
          ++exceptions_;
        }
        exception_condition_.notify_all();
      }
    }
  }

  boost::asio::io_context& context_;
  boost::asio::executor_work_guard<
      boost::asio::io_context::executor_type>
      guard_;
  std::vector<std::thread> threads_;
  std::mutex exception_mutex_;
  std::condition_variable exception_condition_;
  int exceptions_{0};
};

void UpdateMaximum(std::atomic<int>& maximum, int value) {
  int previous = maximum.load();
  while (previous < value &&
         !maximum.compare_exchange_weak(previous, value)) {
  }
}

}  // namespace

TEST(StrandTest, PostIsAsynchronous) {
  boost::asio::io_context context;
  buried::Strand strand(context);
  std::atomic<bool> ran{false};
  std::promise<void> completed;
  auto completed_future = completed.get_future();

  ASSERT_TRUE(strand.Post([&]() {
    ran.store(true);
    completed.set_value();
  }));
  EXPECT_FALSE(ran.load());

  IoContextThreads threads(context, 1);
  ASSERT_EQ(completed_future.wait_for(5s), std::future_status::ready);
  EXPECT_TRUE(ran.load());
}

TEST(StrandTest, ConcurrentPostRunsExactlyOnce) {
  constexpr int kProducerCount = 8;
  constexpr int kTasksPerProducer = 50;
  constexpr int kTaskCount = kProducerCount * kTasksPerProducer;

  boost::asio::io_context context;
  buried::Strand strand(context);
  std::vector<int> executions(kTaskCount, 0);
  std::mutex executions_mutex;
  std::atomic<int> completed_count{0};
  std::atomic<int> rejected_count{0};
  std::promise<void> completed;
  auto completed_future = completed.get_future();
  std::vector<std::thread> producers;
  IoContextThreads context_threads(context, 4);

  for (int producer = 0; producer < kProducerCount; ++producer) {
    producers.emplace_back([&, producer]() {
      for (int sequence = 0; sequence < kTasksPerProducer; ++sequence) {
        int task_id = producer * kTasksPerProducer + sequence;
        if (!strand.Post([&, task_id]() {
              {
                std::lock_guard<std::mutex> lock(executions_mutex);
                ++executions[task_id];
              }
              if (completed_count.fetch_add(1) + 1 == kTaskCount) {
                completed.set_value();
              }
            })) {
          rejected_count.fetch_add(1);
        }
      }
    });
  }

  for (auto& producer : producers) {
    producer.join();
  }
  ASSERT_EQ(completed_future.wait_for(5s), std::future_status::ready);
  ASSERT_TRUE(strand.WaitIdle());
  EXPECT_EQ(rejected_count.load(), 0);
  EXPECT_TRUE(std::all_of(executions.begin(), executions.end(),
                          [](int count) { return count == 1; }));
}

TEST(StrandTest, SameStrandNeverOverlaps) {
  constexpr int kProducerCount = 8;
  constexpr int kTasksPerProducer = 40;
  constexpr int kTaskCount = kProducerCount * kTasksPerProducer;

  boost::asio::io_context context;
  buried::Strand strand(context);
  std::atomic<int> in_flight{0};
  std::atomic<int> maximum_in_flight{0};
  std::atomic<int> completed_count{0};
  std::promise<void> completed;
  auto completed_future = completed.get_future();
  std::vector<std::thread> producers;
  IoContextThreads context_threads(context, 4);

  for (int producer = 0; producer < kProducerCount; ++producer) {
    producers.emplace_back([&]() {
      for (int sequence = 0; sequence < kTasksPerProducer; ++sequence) {
        strand.Post([&]() {
          int current = in_flight.fetch_add(1) + 1;
          UpdateMaximum(maximum_in_flight, current);
          for (int i = 0; i < 8; ++i) {
            std::this_thread::yield();
          }
          in_flight.fetch_sub(1);
          if (completed_count.fetch_add(1) + 1 == kTaskCount) {
            completed.set_value();
          }
        });
      }
    });
  }

  for (auto& producer : producers) {
    producer.join();
  }
  ASSERT_EQ(completed_future.wait_for(5s), std::future_status::ready);
  EXPECT_EQ(maximum_in_flight.load(), 1);
}

TEST(StrandTest, PreservesHappensBeforeOrder) {
  constexpr int kProducerCount = 4;
  constexpr int kTasksPerProducer = 50;
  constexpr int kTaskCount = kProducerCount * kTasksPerProducer;

  boost::asio::io_context context;
  buried::Strand strand(context);
  std::vector<int> last_sequence(kProducerCount, -1);
  std::atomic<int> order_violations{0};
  std::atomic<int> completed_count{0};
  std::promise<void> completed;
  auto completed_future = completed.get_future();
  std::vector<std::thread> producers;
  IoContextThreads context_threads(context, 4);

  for (int producer = 0; producer < kProducerCount; ++producer) {
    producers.emplace_back([&, producer]() {
      for (int sequence = 0; sequence < kTasksPerProducer; ++sequence) {
        strand.Post([&, producer, sequence]() {
          if (last_sequence[producer] + 1 != sequence) {
            order_violations.fetch_add(1);
          }
          last_sequence[producer] = sequence;
          if (completed_count.fetch_add(1) + 1 == kTaskCount) {
            completed.set_value();
          }
        });
      }
    });
  }

  for (auto& producer : producers) {
    producer.join();
  }
  ASSERT_EQ(completed_future.wait_for(5s), std::future_status::ready);
  EXPECT_EQ(order_violations.load(), 0);
  for (int sequence : last_sequence) {
    EXPECT_EQ(sequence, kTasksPerProducer - 1);
  }
}

TEST(StrandTest, ReentrantPostDoesNotDeadlock) {
  boost::asio::io_context context;
  buried::Strand strand(context);
  std::vector<int> order;
  std::atomic<bool> nested_accepted{false};
  std::atomic<bool> self_wait_result{true};
  std::promise<void> completed;
  auto completed_future = completed.get_future();

  ASSERT_TRUE(strand.Post([&]() {
    order.push_back(1);
    self_wait_result.store(strand.WaitIdle());
    nested_accepted.store(strand.Post([&]() {
      order.push_back(3);
      completed.set_value();
    }));
  }));
  ASSERT_TRUE(strand.Post([&]() { order.push_back(2); }));

  IoContextThreads context_threads(context, 2);
  ASSERT_EQ(completed_future.wait_for(5s), std::future_status::ready);
  EXPECT_TRUE(nested_accepted.load());
  EXPECT_FALSE(self_wait_result.load());
  EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(StrandTest, TimerCallbackEntersReportStrand) {
  boost::asio::io_context context;
  buried::Strand strand(context);
  boost::asio::steady_timer successful_timer(context, 0ms);
  boost::asio::steady_timer cancelled_timer(context, 1h);
  std::atomic<int> in_flight{0};
  std::atomic<int> maximum_in_flight{0};
  std::atomic<int> completed_count{0};
  std::atomic<int> success_count{0};
  std::atomic<int> cancelled_count{0};
  std::atomic<bool> all_in_strand{true};
  std::promise<void> completed;
  auto completed_future = completed.get_future();
  IoContextThreads context_threads(context, 4);

  auto handler = [&](const boost::system::error_code& error) {
    all_in_strand.store(all_in_strand.load() &&
                        strand.RunningInThisThread());
    int current = in_flight.fetch_add(1) + 1;
    UpdateMaximum(maximum_in_flight, current);
    std::this_thread::yield();
    if (error == boost::asio::error::operation_aborted) {
      cancelled_count.fetch_add(1);
    } else if (!error) {
      success_count.fetch_add(1);
    }
    in_flight.fetch_sub(1);
    if (completed_count.fetch_add(1) + 1 == 2) {
      completed.set_value();
    }
  };

  successful_timer.async_wait(strand.Wrap(handler));
  cancelled_timer.async_wait(strand.Wrap(handler));
  cancelled_timer.cancel();

  ASSERT_EQ(completed_future.wait_for(5s), std::future_status::ready);
  EXPECT_EQ(success_count.load(), 1);
  EXPECT_EQ(cancelled_count.load(), 1);
  EXPECT_EQ(maximum_in_flight.load(), 1);
  EXPECT_TRUE(all_in_strand.load());
}

TEST(StrandTest, ExceptionDoesNotWedgeQueue) {
  boost::asio::io_context context;
  buried::Strand strand(context);
  std::promise<void> completed;
  auto completed_future = completed.get_future();
  IoContextThreads context_threads(context, 2);

  ASSERT_TRUE(strand.Post([]() { throw std::runtime_error("expected"); }));
  ASSERT_TRUE(strand.Post([&]() { completed.set_value(); }));

  ASSERT_EQ(completed_future.wait_for(5s), std::future_status::ready);
  EXPECT_TRUE(context_threads.WaitForExceptions(1));
  EXPECT_TRUE(strand.WaitIdle());
}

TEST(StrandTest, CloseRejectsNewWorkAndDrainsAcceptedWork) {
  boost::asio::io_context context;
  buried::Strand strand(context);
  std::promise<void> entered;
  auto entered_future = entered.get_future();
  std::promise<void> release;
  auto release_future = release.get_future().share();
  std::atomic<int> completed_count{0};
  IoContextThreads context_threads(context, 2);

  ASSERT_TRUE(strand.Post([&]() {
    entered.set_value();
    release_future.wait_for(5s);
    completed_count.fetch_add(1);
  }));
  ASSERT_EQ(entered_future.wait_for(5s), std::future_status::ready);
  bool queued = strand.Post([&]() { completed_count.fetch_add(1); });

  strand.Close(buried::Strand::CloseMode::kDrain);
  EXPECT_FALSE(strand.Post([]() {}));
  release.set_value();
  EXPECT_TRUE(queued);
  EXPECT_TRUE(strand.WaitIdle());
  EXPECT_EQ(completed_count.load(), 2);
}

TEST(StrandTest, CloseCanCancelQueuedWork) {
  boost::asio::io_context context;
  buried::Strand strand(context);
  std::promise<void> entered;
  auto entered_future = entered.get_future();
  std::promise<void> release;
  auto release_future = release.get_future().share();
  std::atomic<int> completed_count{0};
  IoContextThreads context_threads(context, 2);

  ASSERT_TRUE(strand.Post([&]() {
    entered.set_value();
    release_future.wait_for(5s);
    completed_count.fetch_add(1);
  }));
  ASSERT_EQ(entered_future.wait_for(5s), std::future_status::ready);
  bool queued = strand.Post([&]() { completed_count.fetch_add(1); });

  strand.Close(buried::Strand::CloseMode::kCancel);
  EXPECT_FALSE(strand.IsOpen());
  release.set_value();
  EXPECT_TRUE(queued);
  EXPECT_TRUE(strand.WaitIdle());
  EXPECT_EQ(completed_count.load(), 1);
}

TEST(StrandTest, SupportsMoveOnlyHandler) {
  boost::asio::io_context context;
  buried::Strand strand(context);
  std::promise<int> completed;
  auto completed_future = completed.get_future();
  auto value = std::make_unique<int>(42);
  IoContextThreads context_threads(context, 1);

  ASSERT_TRUE(strand.Post(
      [value = std::move(value), &completed]() mutable {
        completed.set_value(*value);
      }));
  ASSERT_EQ(completed_future.wait_for(5s), std::future_status::ready);
  EXPECT_EQ(completed_future.get(), 42);
}

TEST(ContextTest, PostAfterContextIdle) {
  auto& context = buried::Context::GetGlobalContext();
  context.Start();
  auto& strand = context.GetReportStrand();
  std::promise<void> first_completed;
  auto first_future = first_completed.get_future();

  ASSERT_TRUE(strand.Post([&]() { first_completed.set_value(); }));
  ASSERT_EQ(first_future.wait_for(5s), std::future_status::ready);
  ASSERT_TRUE(strand.WaitIdle());

  std::promise<void> second_completed;
  auto second_future = second_completed.get_future();
  ASSERT_TRUE(strand.Post([&]() { second_completed.set_value(); }));
  EXPECT_EQ(second_future.wait_for(5s), std::future_status::ready);
}
