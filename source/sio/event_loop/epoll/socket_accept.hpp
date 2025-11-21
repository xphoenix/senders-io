#pragma once

#include "details.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <system_error>
#include <utility>

#include <sys/socket.h>

namespace sio::event_loop::epoll {
  template <class Protocol, class Receiver>
  class socket_accept_operation
    : public fd_operation_base<socket_accept_operation<Protocol, Receiver>, Receiver> {
   public:
    socket_accept_operation(acceptor_state<Protocol>& state, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : fd_operation_base<socket_accept_operation<Protocol, Receiver>, Receiver>{state.ctx(), state, static_cast<Receiver&&>(rcvr)}
      , state_{&state} {
    }

    void run_once() noexcept {
      if (this->stop_requested()) {
        this->complete_stopped();
        return;
      }

      while (true) {
        sockaddr_storage addr{};
        socklen_t addrlen = sizeof(addr);
        int fd = ::accept4(state_->native_handle(), reinterpret_cast<sockaddr*>(&addr), &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd >= 0) {
          socket_state<Protocol> child{state_->ctx(), fd, state_->protocol()};
          this->reset_stop_callback();
          stdexec::set_value(std::move(*this).receiver(), std::move(child));
          return;
        }

        if (errno == EINTR) {
          continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          this->wait_for(interest::read);
          return;
        }

        auto ec = std::error_code(errno, std::system_category());
        this->complete_error(ec);
        return;
      }
    }

   private:
    acceptor_state<Protocol>* state_{nullptr};
  };

  template <class Protocol>
  struct socket_accept_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(socket_state<Protocol>),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    acceptor_state<Protocol>* state_{nullptr};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return socket_accept_operation<Protocol, Receiver>{*state_, static_cast<Receiver&&>(receiver)};
    }
  };
} // namespace sio::event_loop::epoll

