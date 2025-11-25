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
    socket_sendmsg_operation(context& ctx, descriptor_token token, Receiver&& rcvr, ::msghdr msg) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : fd_operation_base<socket_sendmsg_operation<Protocol, Receiver>, Receiver>{ctx, token, static_cast<Receiver&&>(rcvr)}
      , msg_{msg} {
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

      while (true) {
        ::ssize_t rc = ::sendmsg(this->entry()->native_handle(), &msg_, 0);
        if (rc >= 0) {
          this->reset_stop_callback();
          this->release_entry();
          stdexec::set_value(std::move(*this).receiver(), static_cast<std::size_t>(rc));
          return;
        }
        if (errno == EINTR) {
          continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          if (!this->wait_for(interest::write)) {
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
    ::msghdr msg_{};
  };

  template <class Protocol>
  struct socket_sendmsg_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    context* context_{nullptr};
    descriptor_token token_{};
    ::msghdr msg_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return socket_sendmsg_operation<Protocol, Receiver>{
        *context_, token_, static_cast<Receiver&&>(receiver), msg_};
    }
  };
} // namespace sio::event_loop::epoll
