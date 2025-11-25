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
    socket_connect_operation(context& ctx, descriptor_token token, typename Protocol::endpoint endpoint, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : fd_operation_base<socket_connect_operation<Protocol, Receiver>, Receiver>{ctx, token, static_cast<Receiver&&>(rcvr)}
      , endpoint_{std::move(endpoint)} {
    }

    void run_once() noexcept {
      if (this->stop_requested()) {
        this->release_entry();
        this->complete_stopped();
        return;
      }

      if (!this->ensure_entry()) {
        this->release_entry();
        this->complete_error(std::make_error_code(std::errc::bad_file_descriptor));
        return;
      }

      if (awaiting_completion_) {
        complete_connect();
        return;
      }

      while (true) {
        int rc = ::connect(
          this->entry()->native_handle(),
          reinterpret_cast<const sockaddr*>(endpoint_.data()),
          endpoint_.size());
        if (rc == 0) {
          this->reset_stop_callback();
          this->release_entry();
          stdexec::set_value(std::move(*this).receiver());
          return;
        }

        if (errno == EINTR) {
          continue;
        }

        if (errno == EINPROGRESS || errno == EALREADY) {
          awaiting_completion_ = true;
          if (!this->wait_for(interest::write)) {
            awaiting_completion_ = false;
            this->release_entry();
            this->complete_error(std::make_error_code(std::errc::bad_file_descriptor));
          }
          return;
        }

        auto ec = std::error_code(errno, std::system_category());
        this->release_entry();
        this->complete_error(ec);
        return;
      }
    }

   private:
    void complete_connect() noexcept {
      int err = 0;
      socklen_t len = sizeof(err);
      if (::getsockopt(this->entry()->native_handle(), SOL_SOCKET, SO_ERROR, &err, &len) == -1) {
        auto ec = std::error_code(errno, std::system_category());
        awaiting_completion_ = false;
        this->release_entry();
        this->complete_error(ec);
        return;
      }
      awaiting_completion_ = false;
      if (err == 0) {
        this->reset_stop_callback();
        this->release_entry();
        stdexec::set_value(std::move(*this).receiver());
      } else {
        auto ec = std::error_code(err, std::system_category());
        this->release_entry();
        this->complete_error(ec);
      }
    }

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

    context* context_{nullptr};
    descriptor_token token_{};
    typename Protocol::endpoint endpoint_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return socket_connect_operation<Protocol, Receiver>{
        *context_, token_, endpoint_, static_cast<Receiver&&>(receiver)};
    }
  };
} // namespace sio::event_loop::epoll
