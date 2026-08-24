#pragma once

#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "boost/asio/io_context.hpp"

namespace buried {

// A small project-owned serial executor. It intentionally implements only the
// behavior used by Buried rather than the complete Boost.Asio executor model.
class Strand {
 public:
  enum class CloseMode {
    kDrain,
    kCancel,
  };

  explicit Strand(boost::asio::io_context& context);

  template <typename Handler>
  bool Post(Handler&& handler) const {
    using HandlerType = std::decay_t<Handler>;
    auto handler_holder =
        std::make_shared<HandlerType>(std::forward<Handler>(handler));
    return PostTask_([handler_holder]() mutable {
      std::invoke(std::move(*handler_holder));
    });
  }

  template <typename Handler>
  auto Wrap(Handler&& handler) const {
    using HandlerType = std::decay_t<Handler>;
    auto handler_holder =
        std::make_shared<HandlerType>(std::forward<Handler>(handler));
    Strand strand = *this;

    return [strand = std::move(strand),
            handler_holder = std::move(handler_holder)](
               auto&&... arguments) mutable {
      using Arguments =
          std::tuple<std::decay_t<decltype(arguments)>...>;
      auto arguments_holder = std::make_shared<Arguments>(
          std::forward<decltype(arguments)>(arguments)...);

      strand.Post([handler_holder, arguments_holder]() mutable {
        std::apply(
            [&handler_holder](auto&&... stored_arguments) mutable {
              std::invoke(*handler_holder,
                          std::forward<decltype(stored_arguments)>(
                              stored_arguments)...);
            },
            std::move(*arguments_holder));
      });
    };
  }

  void Close(CloseMode mode = CloseMode::kDrain) const;

  // Returns false rather than blocking when called by this Strand's handler.
  bool WaitIdle() const;

  bool RunningInThisThread() const noexcept;

  bool IsOpen() const;

 private:
  using Task = std::function<void()>;

  class State;

  bool PostTask_(Task task) const;

  std::shared_ptr<State> state_;
};

}  // namespace buried
