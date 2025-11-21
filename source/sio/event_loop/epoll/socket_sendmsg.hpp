#pragma once

#include "details.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <system_error>
#include <utility>

#include <sys/socket.h>

namespace sio::event_loop::epoll {
  template <class Protocol, class Receiver>
  class socket_sendmsg_operation
    : public fd_operation_base<socket_sendmsg_operation<Protocol, Receiver>, Receiver> {
   public:
    socket_sendmsg_operation(socket_state<Protocol>& state, Receiver&& rcvr, ::msghdr msg) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : fd_operation_base<socket_sendmsg_operation<Protocol, Receiver>, Receiver>{state.ctx(), state, static_cast<Receiver&&>(rcvr)}
      , state_{&state}
      , msg_{msg} {
    }

    void run_once() noexcept {
      if (this->stop_requested()) {
        this->complete_stopped();
        return;
      }

      while (true) {
        ::ssize_t rc = ::sendmsg(state_->native_handle(), &msg_, 0);
        if (rc >= 0) {
          this->reset_stop_callback();
          stdexec::set_value(std::move(*this).receiver(), static_cast<std::size_t>(rc));
          return;
        }
        if (errno == EINTR) {
          continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          this->wait_for(interest::write);
          return;
        }
        auto ec = std::error_code(errno, std::system_category());
        this->complete_error(ec);
        return;
      }
    }

   private:
    socket_state<Protocol>* state_{nullptr};
    ::msghdr msg_{};
  };

  template <class Protocol>
  struct socket_sendmsg_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    socket_state<Protocol>* state_{nullptr};
    ::msghdr msg_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return socket_sendmsg_operation<Protocol, Receiver>{
        *state_, static_cast<Receiver&&>(receiver), msg_};
    }

    env get_env() const noexcept {
      return {state_->ctx().get_scheduler()};
    }
  };
} // namespace sio::event_loop::epoll

