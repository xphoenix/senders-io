#pragma once

#include "details.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <system_error>
#include <utility>

#include <sys/socket.h>

namespace sio::event_loop::epoll {
  template <class Protocol, class Receiver>
  class socket_connect_operation
    : public fd_operation_base<socket_connect_operation<Protocol, Receiver>, Receiver> {
   public:
    socket_connect_operation(socket_state<Protocol>& state, typename Protocol::endpoint endpoint, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : fd_operation_base<socket_connect_operation<Protocol, Receiver>, Receiver>{state.ctx(), state, static_cast<Receiver&&>(rcvr)}
      , state_{&state}
      , endpoint_{std::move(endpoint)} {
    }

    void run_once() noexcept {
      if (this->stop_requested()) {
        this->complete_stopped();
        return;
      }

      if (awaiting_completion_) {
        complete_connect();
        return;
      }

      while (true) {
        int rc = ::connect(
          state_->native_handle(),
          reinterpret_cast<const sockaddr*>(endpoint_.data()),
          endpoint_.size());
        if (rc == 0) {
          this->reset_stop_callback();
          stdexec::set_value(std::move(*this).receiver());
          return;
        }

        if (errno == EINTR) {
          continue;
        }

        if (errno == EINPROGRESS || errno == EALREADY) {
          awaiting_completion_ = true;
          this->wait_for(interest::write);
          return;
        }

        auto ec = std::error_code(errno, std::system_category());
        this->complete_error(ec);
        return;
      }
    }

   private:
    void complete_connect() noexcept {
      int err = 0;
      socklen_t len = sizeof(err);
      if (::getsockopt(state_->native_handle(), SOL_SOCKET, SO_ERROR, &err, &len) == -1) {
        auto ec = std::error_code(errno, std::system_category());
        this->complete_error(ec);
        return;
      }
      awaiting_completion_ = false;
      if (err == 0) {
        this->reset_stop_callback();
        stdexec::set_value(std::move(*this).receiver());
      } else {
        auto ec = std::error_code(err, std::system_category());
        this->complete_error(ec);
      }
    }

    socket_state<Protocol>* state_{nullptr};
    typename Protocol::endpoint endpoint_{};
    bool awaiting_completion_{false};
  };

  template <class Protocol>
  struct socket_connect_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    socket_state<Protocol>* state_{nullptr};
    typename Protocol::endpoint endpoint_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return socket_connect_operation<Protocol, Receiver>{
        *state_, endpoint_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {state_->ctx().get_scheduler()};
    }
  };
} // namespace sio::event_loop::epoll

