#pragma once

#include "details.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <system_error>
#include <utility>
#include <new>

#include <sys/socket.h>
#include <unistd.h>

namespace sio::event_loop::epoll {
  template <class Protocol, class Receiver>
  class socket_accept_operation
    : public fd_operation_base<socket_accept_operation<Protocol, Receiver>, Receiver> {
   public:
    socket_accept_operation(context& ctx, descriptor_token token, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : fd_operation_base<socket_accept_operation<Protocol, Receiver>, Receiver>{ctx, token, static_cast<Receiver&&>(rcvr)} {
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
        sockaddr_storage addr{};
        socklen_t addrlen = sizeof(addr);
        int fd = ::accept4(this->entry()->native_handle(), reinterpret_cast<sockaddr*>(&addr), &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd >= 0) {
          descriptor_token child{};
          try {
            child = this->loop_context().register_descriptor(fd);
          } catch (const std::bad_alloc&) {
            ::close(fd);
            auto ec = std::make_error_code(std::errc::not_enough_memory);
            this->release_entry();
            this->complete_error(ec);
            return;
          }
          this->reset_stop_callback();
          this->release_entry();
          stdexec::set_value(std::move(*this).receiver(), socket_state<Protocol>{child});
          return;
        }

        if (errno == EINTR) {
          continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          if (!this->wait_for(interest::read)) {
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
  };

  template <class Protocol>
  struct socket_accept_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(socket_state<Protocol>),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    context* context_{nullptr};
    descriptor_token token_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return socket_accept_operation<Protocol, Receiver>{
        *context_, token_, static_cast<Receiver&&>(receiver)};
    }
  };
} // namespace sio::event_loop::epoll
