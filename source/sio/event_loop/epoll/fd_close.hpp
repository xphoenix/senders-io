#pragma once

#include "details.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <system_error>
#include <unistd.h>

namespace sio::event_loop::epoll {
  template <class Receiver>
  class fd_close_operation {
   public:
    fd_close_operation(context& ctx, int fd, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : context_{ctx}
      , fd_{fd}
      , receiver_(static_cast<Receiver&&>(rcvr)) {
    }

    fd_close_operation(fd_close_operation&&) = delete;
    fd_close_operation(const fd_close_operation&) = delete;
    fd_close_operation& operator=(fd_close_operation&&) = delete;
    fd_close_operation& operator=(const fd_close_operation&) = delete;

    friend void tag_invoke(stdexec::start_t, fd_close_operation& op) noexcept {
      auto stop_token = stdexec::get_stop_token(stdexec::get_env(op.receiver_));
      if (stop_token.stop_requested()) {
        stdexec::set_stopped(std::move(op.receiver_));
        return;
      }
      if (::close(op.fd_) == -1) {
        auto ec = std::error_code(errno, std::system_category());
        stdexec::set_error(std::move(op.receiver_), std::move(ec));
      } else {
        stdexec::set_value(std::move(op.receiver_));
      }
    }

   private:
    context& context_;
    int fd_;
    Receiver receiver_;
  };

  struct fd_close_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    context* context_{nullptr};
    int fd_{-1};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return fd_close_operation<Receiver>{*context_, fd_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };
} // namespace sio::event_loop::epoll
