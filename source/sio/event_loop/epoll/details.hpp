#pragma once

#include "context.hpp"
#include "scheduler.hpp"

#include "../../async_resource.hpp"
#include "../../const_buffer.hpp"
#include "../../const_buffer_span.hpp"
#include "../../mutable_buffer.hpp"
#include "../../mutable_buffer_span.hpp"
#include "../../intrusive/list.hpp"

#include <stdexec/execution.hpp>

#include <atomic>
#include <cerrno>
#include <mutex>
#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>

#include <sys/socket.h>

namespace sio::event_loop::epoll {
  struct env {
    scheduler loop_scheduler_;

    auto query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept -> scheduler {
      return loop_scheduler_;
    }
  };

  enum class interest {
    none,
    read,
    write
  };

  class fd_state;

  struct fd_operation_base_common : runnable_base {
    fd_operation_base_common(class context& ctx, fd_state& state, void (*fn)(runnable_base&) noexcept) noexcept;
    fd_operation_base_common(const fd_operation_base_common&) = delete;
    fd_operation_base_common& operator=(const fd_operation_base_common&) = delete;

    void wait_on(interest what) noexcept;
    void clear_waiting() noexcept;
    bool try_cancel() noexcept;
    class context& context() const noexcept;
    fd_state& state() const noexcept;

    fd_operation_base_common* wait_next_{nullptr};
    fd_operation_base_common* wait_prev_{nullptr};
    interest waiting_interest_{interest::none};
    bool waiting_{false};
    std::atomic<bool> cancelled_{false};

   private:
    friend class fd_state;
    class context& context_;
    fd_state& state_;
  };

  class fd_state {
   public:
    fd_state() = default;

    fd_state(context& ctx, int fd) noexcept
      : context_{&ctx}
      , fd_{fd} {
    }

    context& ctx() const noexcept {
      return *context_;
    }

    int native_handle() const noexcept {
      return fd_;
    }

    void reset(int fd = -1) noexcept {
      fd_ = fd;
    }

   private:
    friend void handle_descriptor_event(fd_state&, std::uint32_t) noexcept;
    friend struct fd_operation_base_common;

    using wait_list = sio::intrusive::list<
      &fd_operation_base_common::wait_next_,
      &fd_operation_base_common::wait_prev_>;

    void add_waiter(fd_operation_base_common& op, interest what) noexcept;

    void remove_waiter(fd_operation_base_common& op) noexcept;

    void handle_events(std::uint32_t events) noexcept;

    std::uint32_t compute_mask_locked() const noexcept;

    void update_interest_locked(std::uint32_t mask) noexcept;

    context* context_{nullptr};
    int fd_{-1};
    mutable std::mutex mutex_{};
    wait_list read_waiters_{};
    wait_list write_waiters_{};
    bool registered_{false};
    std::uint32_t interest_mask_{0};
  };

  template <class Derived, class Receiver>
  class fd_operation_base : public fd_operation_base_common {
   protected:
     using env_type = stdexec::env_of_t<Receiver&>;
     using stop_token_type = stdexec::stop_token_of_t<env_type>;
    struct on_stop {
      fd_operation_base* self;

      void operator()() noexcept {
        self->request_stop();
      }
    };
    using callback_type = stdexec::stop_callback_for_t<stop_token_type, on_stop>;

    fd_operation_base(class context& ctx, fd_state& state, Receiver&& rcvr) noexcept(std::is_nothrow_move_constructible_v<Receiver>)
      : fd_operation_base_common{ctx, state, &fd_operation_base::dispatch}
      , receiver_(static_cast<Receiver&&>(rcvr)) {
    }

    fd_operation_base(fd_operation_base&&) = delete;
    fd_operation_base(const fd_operation_base&) = delete;
    fd_operation_base& operator=(fd_operation_base&&) = delete;
    fd_operation_base& operator=(const fd_operation_base&) = delete;

    friend void tag_invoke(stdexec::start_t, fd_operation_base& op) noexcept {
      auto stop_token = stdexec::get_stop_token(stdexec::get_env(op.receiver_));
      if (stop_token.stop_requested()) {
        stdexec::set_stopped(std::move(op.receiver_));
        return;
      }
      if constexpr (stdexec::stoppable_token<stop_token_type>) {
        if (stop_token.stop_possible()) {
          op.stop_callback_.emplace(stop_token, on_stop{&op});
        }
      }
      op.context().enqueue_task(op);
    }

    void complete_stopped() noexcept {
      stop_callback_.reset();
      stdexec::set_stopped(std::move(receiver_));
    }

    void complete_error(std::error_code ec) noexcept {
      stop_callback_.reset();
      stdexec::set_error(std::move(receiver_), std::move(ec));
    }

    Receiver& receiver() & noexcept {
      return receiver_;
    }

    Receiver&& receiver() && noexcept {
      return static_cast<Receiver&&>(receiver_);
    }

    void reset_stop_callback() noexcept {
      stop_callback_.reset();
    }

    bool stop_requested() const noexcept {
      return this->cancelled_.load(std::memory_order_acquire);
    }

    void request_stop() noexcept {
      if (this->try_cancel()) {
        this->context().enqueue_task(*this);
      }
    }

    void wait_for(interest what) noexcept {
      this->wait_on(what);
    }

   private:
    static void dispatch(runnable_base& base) noexcept {
      auto& self = static_cast<Derived&>(base);
      self.run_once();
    }

    Receiver receiver_;
    std::optional<callback_type> stop_callback_{};
  };

  inline std::uint32_t fd_state::compute_mask_locked() const noexcept {
    std::uint32_t mask = 0;
    if (!read_waiters_.empty()) {
      mask |= static_cast<std::uint32_t>(EPOLLIN | EPOLLRDHUP | EPOLLERR);
    }
    if (!write_waiters_.empty()) {
      mask |= static_cast<std::uint32_t>(EPOLLOUT | EPOLLERR);
    }
    return mask;
  }

  inline void fd_state::update_interest_locked(std::uint32_t mask) noexcept {
    if (mask == interest_mask_) {
      return;
    }
    ::epoll_event ev;
    ev.events = mask;
    ev.data.ptr = this;
    if (!registered_) {
      if (::epoll_ctl(context_->epoll_fd_, EPOLL_CTL_ADD, fd_, &ev) == 0) {
        registered_ = true;
        interest_mask_ = mask;
      }
      return;
    }
    if (mask == 0) {
      if (::epoll_ctl(context_->epoll_fd_, EPOLL_CTL_DEL, fd_, nullptr) == 0) {
        registered_ = false;
        interest_mask_ = 0;
      }
      return;
    }
    if (::epoll_ctl(context_->epoll_fd_, EPOLL_CTL_MOD, fd_, &ev) == 0) {
      interest_mask_ = mask;
    }
  }

  inline void fd_state::add_waiter(fd_operation_base_common& op, interest what) noexcept {
    std::lock_guard lk(mutex_);
    switch (what) {
    case interest::read:
      read_waiters_.push_back(&op);
      break;
    case interest::write:
      write_waiters_.push_back(&op);
      break;
    default:
      break;
    }
    update_interest_locked(compute_mask_locked());
  }

  inline void fd_state::remove_waiter(fd_operation_base_common& op) noexcept {
    std::lock_guard lk(mutex_);
    if (!op.waiting_) {
      return;
    }
    switch (op.waiting_interest_) {
    case interest::read:
      read_waiters_.erase(&op);
      break;
    case interest::write:
      write_waiters_.erase(&op);
      break;
    default:
      break;
    }
    op.waiting_interest_ = interest::none;
    op.waiting_ = false;
    update_interest_locked(compute_mask_locked());
  }

  inline void fd_state::handle_events(std::uint32_t events) noexcept {
    wait_list ready_readers{};
    wait_list ready_writers{};

    const bool wake_read = (events & (EPOLLIN | EPOLLERR | EPOLLRDHUP | EPOLLHUP)) != 0;
    const bool wake_write = (events & (EPOLLOUT | EPOLLERR | EPOLLHUP)) != 0;

    {
      std::lock_guard lk(mutex_);
      if (wake_read) {
        ready_readers = std::move(read_waiters_);
      }
      if (wake_write) {
        ready_writers = std::move(write_waiters_);
      }
      update_interest_locked(compute_mask_locked());
    }

    auto resume = [&](wait_list& list) {
      while (auto* op = list.pop_front()) {
        op->clear_waiting();
        context_->enqueue_task(*op);
      }
    };

    resume(ready_readers);
    resume(ready_writers);
  }

  inline void handle_descriptor_event(fd_state& state, std::uint32_t events) noexcept {
    state.handle_events(events);
  }

  inline fd_operation_base_common::fd_operation_base_common(
    class context& ctx,
    fd_state& state,
    void (*fn)(runnable_base&) noexcept) noexcept
    : runnable_base{fn}
    , context_{ctx}
    , state_{state} {
  }

  inline void fd_operation_base_common::wait_on(interest what) noexcept {
    waiting_interest_ = what;
    waiting_ = true;
    state_.add_waiter(*this, what);
  }

  inline void fd_operation_base_common::clear_waiting() noexcept {
    waiting_interest_ = interest::none;
    waiting_ = false;
  }

  inline bool fd_operation_base_common::try_cancel() noexcept {
    bool already = cancelled_.exchange(true, std::memory_order_acq_rel);
    if (!already) {
      state_.remove_waiter(*this);
    }
    return !already;
  }

  inline class context& fd_operation_base_common::context() const noexcept {
    return context_;
  }

  inline fd_state& fd_operation_base_common::state() const noexcept {
    return state_;
  }

  struct file_state_base : fd_state {
    async::mode mode_{async::mode::read};
    async::creation creation_{async::creation::open_existing};
    async::caching caching_{async::caching::unchanged};

    file_state_base() = default;

    file_state_base(
      context& ctx,
      int fd,
      async::mode mode,
      async::creation creation,
      async::caching caching) noexcept
      : fd_state{ctx, fd}
      , mode_{mode}
      , creation_{creation}
      , caching_{caching} {
    }

    async::mode mode() const noexcept {
      return mode_;
    }

    async::creation creation() const noexcept {
      return creation_;
    }

    async::caching caching() const noexcept {
      return caching_;
    }
  };

  struct file_state : file_state_base {
    using file_state_base::file_state_base;
  };

  struct seekable_file_state : file_state_base {
    using file_state_base::file_state_base;
  };

  template <class Protocol>
  struct socket_state : fd_state {
    using endpoint = typename Protocol::endpoint;
    using buffer_type = sio::mutable_buffer;
    using buffers_type = sio::mutable_buffer_span;
    using const_buffer_type = sio::const_buffer;
    using const_buffers_type = sio::const_buffer_span;

    socket_state() = default;

    socket_state(context& ctx, int fd, Protocol protocol) noexcept
      : fd_state{ctx, fd}
      , protocol_{std::move(protocol)} {
    }

    const Protocol& protocol() const noexcept {
      return *protocol_;
    }

    Protocol consume_protocol() noexcept {
      Protocol value = std::move(*protocol_);
      protocol_.reset();
      return value;
    }

    void bind(endpoint local_endpoint) {
      auto addr = reinterpret_cast<const sockaddr*>(local_endpoint.data());
      if (::bind(native_handle(), addr, local_endpoint.size()) == -1) {
        throw std::system_error(errno, std::system_category());
      }
    }

    endpoint local_endpoint() const;
    endpoint remote_endpoint() const;

   private:
    [[no_unique_address]] std::optional<Protocol> protocol_{};
  };

  template <class Protocol>
  struct acceptor_state : fd_state {
    using endpoint = typename Protocol::endpoint;

    acceptor_state() = default;

    acceptor_state(context& ctx, int fd, Protocol protocol, endpoint endpoint_value) noexcept
      : fd_state{ctx, fd}
      , protocol_{std::move(protocol)}
      , local_endpoint_{std::move(endpoint_value)} {
    }

    const Protocol& protocol() const noexcept {
      return *protocol_;
    }

    Protocol consume_protocol() noexcept {
      Protocol value = std::move(*protocol_);
      protocol_.reset();
      return value;
    }

    const endpoint& local_endpoint() const noexcept {
      return local_endpoint_;
    }

   private:
    [[no_unique_address]] std::optional<Protocol> protocol_{};
    endpoint local_endpoint_{};
  };
} // namespace sio::event_loop::epoll
