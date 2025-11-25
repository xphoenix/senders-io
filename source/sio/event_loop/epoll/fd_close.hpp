#pragma once

#include "details.hpp"

#include <stdexec/execution.hpp>

#include <system_error>

namespace sio::event_loop::epoll {
  template <class Receiver>
  class fd_close_operation {
   public:
    fd_close_operation(context& ctx, descriptor_token token, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : context_{ctx}
      , token_{token}
      , receiver_(static_cast<Receiver&&>(rcvr)) {
    }

    fd_close_operation(fd_close_operation&&) = delete;
    fd_close_operation(const fd_close_operation&) = delete;
    fd_close_operation& operator=(fd_close_operation&&) = delete;
    fd_close_operation& operator=(const fd_close_operation&) = delete;

    void start() noexcept {
      auto stop_token = stdexec::get_stop_token(stdexec::get_env(receiver_));
      if (stop_token.stop_requested()) {
        stdexec::set_stopped(std::move(receiver_));
        return;
      }
      auto ec = context_.release_entry(token_);
      if (ec) {
        stdexec::set_error(std::move(receiver_), std::move(ec));
      } else {
        stdexec::set_value(std::move(receiver_));
      }
    }

   private:
    context& context_;
    descriptor_token token_{};
    Receiver receiver_;
  };

  struct fd_close_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    context* context_{nullptr};
    descriptor_token token_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return fd_close_operation<Receiver>{*context_, token_, static_cast<Receiver&&>(receiver)};
    }
  };
} // namespace sio::event_loop::epoll
