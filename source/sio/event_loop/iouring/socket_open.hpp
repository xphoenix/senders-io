#pragma once

#include "context.hpp"
#include "details.hpp"
#include "submission_operation.hpp"
#include "../concepts.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <sys/socket.h>

#include <system_error>
#include <utility>

namespace sio::event_loop::iouring {

  template <class Protocol, class Receiver>
  class socket_open_operation {
   public:
    socket_open_operation(io_context& ctx, Protocol protocol, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : context_{&ctx}
      , protocol_{std::move(protocol)}
      , receiver_{static_cast<Receiver&&>(rcvr)} {
    }

    socket_open_operation(socket_open_operation&&) = delete;
    socket_open_operation(const socket_open_operation&) = delete;
    socket_open_operation& operator=(socket_open_operation&&) = delete;
    socket_open_operation& operator=(const socket_open_operation&) = delete;

    friend void tag_invoke(stdexec::start_t, socket_open_operation& op) noexcept {
      auto stop_token = stdexec::get_stop_token(stdexec::get_env(op.receiver_));
      if (stop_token.stop_requested()) {
        stdexec::set_stopped(std::move(op.receiver_));
        return;
      }

      int fd = ::socket(op.protocol_.family(), op.protocol_.type(), op.protocol_.protocol());
      if (fd == -1) {
        auto ec = std::error_code(errno, std::system_category());
        stdexec::set_error(std::move(op.receiver_), std::move(ec));
      } else {
        stdexec::set_value(std::move(op.receiver_), socket_fd<Protocol>{fd});
      }
    }

   private:
    io_context* context_;
    Protocol protocol_;
    Receiver receiver_;
  };

  template <class Protocol>
  struct socket_open_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(socket_fd<Protocol>),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{nullptr};
    Protocol protocol_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return socket_open_operation<Protocol, Receiver>{
        *context_, protocol_, static_cast<Receiver&&>(receiver)};
    }
  };

} // namespace sio::event_loop::iouring
