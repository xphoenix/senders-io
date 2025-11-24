#pragma once

#include "details.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <system_error>
#include <utility>
#include <vector>

#include <sys/uio.h>
#include <unistd.h>

namespace sio::event_loop::epoll {
  template <class Receiver>
  class fd_write_operation_span : public fd_operation_base<fd_write_operation_span<Receiver>, Receiver> {
   public:
    fd_write_operation_span(
      context& ctx,
      descriptor_token token,
      sio::const_buffer_span buffers,
      Receiver&& rcvr,
      ::off_t offset) noexcept(std::is_nothrow_move_constructible_v<Receiver>)
      : fd_operation_base<fd_write_operation_span<Receiver>, Receiver>{ctx, token, static_cast<Receiver&&>(rcvr)}
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
        this->release_entry();
        this->complete_stopped();
        return;
      }

      if (!this->ensure_entry()) {
        this->release_entry();
        this->complete_error(std::make_error_code(std::errc::bad_file_descriptor));
        return;
      }

      if (iovecs_.empty()) {
        this->reset_stop_callback();
        this->release_entry();
        stdexec::set_value(std::move(*this).receiver(), std::size_t{0});
        return;
      }

      while (true) {
        ::ssize_t rc = -1;
        if (offset_ >= 0) {
          rc = ::pwritev(this->entry()->native_handle(), iovecs_.data(), static_cast<int>(iovecs_.size()), offset_);
        } else {
          rc = ::writev(this->entry()->native_handle(), iovecs_.data(), static_cast<int>(iovecs_.size()));
        }

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
    std::vector<::iovec> iovecs_;
    ::off_t offset_{-1};
  };

  template <class Receiver>
  class fd_write_operation_single : public fd_operation_base<fd_write_operation_single<Receiver>, Receiver> {
   public:
    fd_write_operation_single(
      context& ctx,
      descriptor_token token,
      sio::const_buffer buffer,
      Receiver&& rcvr,
      ::off_t offset) noexcept(std::is_nothrow_move_constructible_v<Receiver>)
      : fd_operation_base<fd_write_operation_single<Receiver>, Receiver>{ctx, token, static_cast<Receiver&&>(rcvr)}
      , buffer_{buffer}
      , offset_{offset} {
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

      if (buffer_.size() == 0) {
        this->reset_stop_callback();
        this->release_entry();
        std::size_t written = 0;
        stdexec::set_value(std::move(*this).receiver(), written);
        return;
      }

      while (true) {
        ::ssize_t rc = -1;
        if (offset_ >= 0) {
          rc = ::pwrite(this->entry()->native_handle(), buffer_.data(), buffer_.size(), offset_);
        } else {
          rc = ::write(this->entry()->native_handle(), buffer_.data(), buffer_.size());
        }

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
    sio::const_buffer buffer_;
    ::off_t offset_{-1};
  };

  struct fd_write_sender_span {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    context* context_{nullptr};
    descriptor_token token_{};
    sio::const_buffer_span buffers_{};
    ::off_t offset_{-1};

    fd_write_sender_span() = default;

    fd_write_sender_span(
      context& ctx,
      descriptor_token token,
      sio::const_buffer_span buffers,
      ::off_t offset = -1) noexcept
      : context_{&ctx}
      , token_{token}
      , buffers_{buffers}
      , offset_{offset} {
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return fd_write_operation_span<Receiver>{
        *context_, token_, buffers_, static_cast<Receiver&&>(receiver), offset_};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct fd_write_sender_single {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    context* context_{nullptr};
    descriptor_token token_{};
    sio::const_buffer buffer_{};
    ::off_t offset_{-1};

    fd_write_sender_single() = default;

    fd_write_sender_single(
      context& ctx,
      descriptor_token token,
      sio::const_buffer buffer,
      ::off_t offset = -1) noexcept
      : context_{&ctx}
      , token_{token}
      , buffer_{buffer}
      , offset_{offset} {
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return fd_write_operation_single<Receiver>{
        *context_, token_, buffer_, static_cast<Receiver&&>(receiver), offset_};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct fd_write_factory {
    context* context_{nullptr};
    descriptor_token token_{};

    fd_write_sender_span operator()(sio::const_buffer_span buffers, ::off_t offset) const noexcept {
      return fd_write_sender_span{*context_, token_, buffers, offset};
    }

    fd_write_sender_single operator()(sio::const_buffer buffer, ::off_t offset) const noexcept {
      return fd_write_sender_single{*context_, token_, buffer, offset};
    }
  };
} // namespace sio::event_loop::epoll
