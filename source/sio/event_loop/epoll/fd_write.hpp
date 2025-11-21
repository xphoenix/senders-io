#pragma once

#include "details.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

#include <sys/uio.h>
#include <unistd.h>

namespace sio::event_loop::epoll {
  template <class Receiver>
  class fd_write_operation_span : public fd_operation_base<fd_write_operation_span<Receiver>, Receiver> {
   public:
    fd_write_operation_span(fd_state& state, sio::const_buffer_span buffers, Receiver&& rcvr, ::off_t offset) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : fd_operation_base<fd_write_operation_span<Receiver>, Receiver>{state.ctx(), state, static_cast<Receiver&&>(rcvr)}
      , offset_{offset} {
      iovecs_.reserve(buffers.size());
      for (auto buffer : buffers) {
        ::iovec iov{};
        iov.iov_base = const_cast<std::byte*>(buffer.data());
        iov.iov_len = buffer.size();
        iovecs_.push_back(iov);
      }
    }

    void run_once() noexcept {
      if (this->stop_requested()) {
        this->complete_stopped();
        return;
      }

      if (iovecs_.empty()) {
        this->reset_stop_callback();
        stdexec::set_value(std::move(*this).receiver(), std::size_t{0});
        return;
      }

      while (true) {
        ::ssize_t rc = -1;
        if (offset_ >= 0) {
          rc = ::pwritev(this->state().native_handle(), iovecs_.data(), static_cast<int>(iovecs_.size()), offset_);
        } else {
          rc = ::writev(this->state().native_handle(), iovecs_.data(), static_cast<int>(iovecs_.size()));
        }

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
    std::vector<::iovec> iovecs_;
    ::off_t offset_{-1};
  };

  template <class Receiver>
  class fd_write_operation_single : public fd_operation_base<fd_write_operation_single<Receiver>, Receiver> {
   public:
    fd_write_operation_single(fd_state& state, sio::const_buffer buffer, Receiver&& rcvr, ::off_t offset) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : fd_operation_base<fd_write_operation_single<Receiver>, Receiver>{state.ctx(), state, static_cast<Receiver&&>(rcvr)}
      , buffer_{buffer}
      , offset_{offset} {
    }

    void run_once() noexcept {
      if (this->stop_requested()) {
        this->complete_stopped();
        return;
      }

      if (buffer_.size() == 0) {
        this->reset_stop_callback();
        std::size_t written = 0;
        stdexec::set_value(std::move(*this).receiver(), written);
        return;
      }

      while (true) {
        ::ssize_t rc = -1;
        if (offset_ >= 0) {
          rc = ::pwrite(this->state().native_handle(), buffer_.data(), buffer_.size(), offset_);
        } else {
          rc = ::write(this->state().native_handle(), buffer_.data(), buffer_.size());
        }

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
    sio::const_buffer buffer_;
    ::off_t offset_{-1};
  };

  struct fd_write_sender_span {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    fd_state* state_{nullptr};
    sio::const_buffer_span buffers_{};
    ::off_t offset_{-1};

    fd_write_sender_span() = default;

    fd_write_sender_span(fd_state& state, sio::const_buffer_span buffers, ::off_t offset = -1) noexcept
      : state_{&state}
      , buffers_{buffers}
      , offset_{offset} {
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return fd_write_operation_span<Receiver>{
        *state_, buffers_, static_cast<Receiver&&>(receiver), offset_};
    }

    env get_env() const noexcept {
      return {state_->ctx().get_scheduler()};
    }
  };

  struct fd_write_sender_single {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    fd_state* state_{nullptr};
    sio::const_buffer buffer_{};
    ::off_t offset_{-1};

    fd_write_sender_single() = default;

    fd_write_sender_single(fd_state& state, sio::const_buffer buffer, ::off_t offset = -1) noexcept
      : state_{&state}
      , buffer_{buffer}
      , offset_{offset} {
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return fd_write_operation_single<Receiver>{
        *state_, buffer_, static_cast<Receiver&&>(receiver), offset_};
    }

    env get_env() const noexcept {
      return {state_->ctx().get_scheduler()};
    }
  };

  struct fd_write_factory {
    std::shared_ptr<fd_state> owned_state_{};
    fd_state* state_{nullptr};

    fd_write_factory() = default;

    explicit fd_write_factory(fd_state* state) noexcept
      : state_{state} {
    }

    fd_write_factory(context* ctx, int fd)
      : owned_state_{std::make_shared<fd_state>(*ctx, fd)}
      , state_{owned_state_.get()} {
    }

    fd_write_sender_span operator()(sio::const_buffer_span buffers, ::off_t offset) const noexcept {
      return fd_write_sender_span{*state_, buffers, offset};
    }

    fd_write_sender_single operator()(sio::const_buffer buffer, ::off_t offset) const noexcept {
      return fd_write_sender_single{*state_, buffer, offset};
    }
  };
} // namespace sio::event_loop::epoll
