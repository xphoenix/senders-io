#pragma once

#include "../../intrusive/queue.hpp"

#include <stdexec/execution.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace sio::event_loop::epoll {
  class scheduler;
  struct run_sender;
  class fd_state;
  void handle_descriptor_event(fd_state&, std::uint32_t) noexcept;
  namespace detail {
    template <class Receiver>
    class schedule_operation;
  }

  struct runnable_base {
    runnable_base* next_{nullptr};
    void (*execute_)(runnable_base&) noexcept;

    explicit runnable_base(void (*fn)(runnable_base&) noexcept) noexcept
      : execute_{fn} {
    }

    void run() noexcept {
      execute_(*this);
    }
  };

  enum class run_mode {
    stopped,
    drained
  };

  class context {
   public:
    context();
    context(const context&) = delete;
    context& operator=(const context&) = delete;
    context(context&&) = delete;
    context& operator=(context&&) = delete;
    ~context();

    scheduler get_scheduler() noexcept;

    [[nodiscard]] bool stop_requested() const noexcept {
      return stop_requested_.load(std::memory_order_acquire);
    }

    void request_stop();

    void run_until_empty();

    std::size_t run_some();

    std::size_t run_one();

    run_sender run(run_mode mode = run_mode::stopped) noexcept;

    void enqueue_task(runnable_base& task) noexcept;

   private:
    friend class scheduler;
    friend class fd_state;
    friend struct run_sender;
    template <class Receiver>
    friend class detail::schedule_operation;

    struct wake_token {
    };

    void wake() noexcept;

    void drain_wake_fd() noexcept;

    std::size_t drain_ready_tasks() noexcept;

    std::size_t drive(bool block);

    void dispatch_event(const ::epoll_event& event) noexcept;

    int epoll_fd_{-1};
    int wake_fd_{-1};
    wake_token wake_token_{};
    std::vector<::epoll_event> events_{};
    sio::intrusive::queue<&runnable_base::next_> ready_queue_{};
    std::mutex ready_mutex_{};
    std::atomic<bool> stop_requested_{false};
  };

  inline context::context() {
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) {
      throw std::system_error(errno, std::system_category());
    }

    wake_fd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wake_fd_ == -1) {
      int err = errno;
      ::close(epoll_fd_);
      throw std::system_error(err, std::system_category());
    }

    ::epoll_event ev;
    ev.events = static_cast<std::uint32_t>(EPOLLIN | EPOLLET);
    ev.data.ptr = &wake_token_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wake_fd_, &ev) == -1) {
      int err = errno;
      ::close(wake_fd_);
      ::close(epoll_fd_);
      throw std::system_error(err, std::system_category());
    }

    events_.resize(64);
  }

  inline context::~context() {
    if (wake_fd_ != -1) {
      ::close(wake_fd_);
    }
    if (epoll_fd_ != -1) {
      ::close(epoll_fd_);
    }
  }

  inline void context::wake() noexcept {
    std::uint64_t value = 1;
    ::ssize_t rc = ::write(wake_fd_, &value, sizeof(value));
    (void) rc;
  }

  inline void context::drain_wake_fd() noexcept {
    std::uint64_t value = 0;
    while (true) {
      ::ssize_t rc = ::read(wake_fd_, &value, sizeof(value));
      if (rc == -1) {
        if (errno == EAGAIN) {
          break;
        }
        if (errno == EINTR) {
          continue;
        }
        break;
      }
      if (rc == 0) {
        break;
      }
    }
  }

  inline void context::request_stop() {
    stop_requested_.store(true, std::memory_order_release);
    wake();
  }

  inline void context::enqueue_task(runnable_base& task) noexcept {
    {
      std::lock_guard lk(ready_mutex_);
      ready_queue_.push_back(&task);
    }
    wake();
  }

  inline std::size_t context::drain_ready_tasks() noexcept {
    sio::intrusive::queue<&runnable_base::next_> pending;
    {
      std::lock_guard lk(ready_mutex_);
      pending = std::move(ready_queue_);
    }
    std::size_t executed = 0;
    while (auto* task = pending.pop_front()) {
      task->run();
      ++executed;
    }
    return executed;
  }

  inline void context::dispatch_event(const ::epoll_event& event) noexcept {
    if (event.data.ptr == &wake_token_) {
      drain_wake_fd();
      return;
    }
    auto* state = static_cast<fd_state*>(event.data.ptr);
    if (state != nullptr) {
      handle_descriptor_event(*state, event.events);
    }
  }

  inline std::size_t context::drive(bool block) {
    std::size_t processed = drain_ready_tasks();
    if (!block && processed != 0) {
      return processed;
    }

    const ::timespec zero_timeout{0, 0};
    const ::timespec* timeout = block ? nullptr : &zero_timeout;

    while (true) {
      int n = ::epoll_pwait2(epoll_fd_, events_.data(), static_cast<int>(events_.size()), timeout, nullptr);
      if (n == -1) {
        if (errno == EINTR) {
          if (!block) {
            break;
          }
          continue;
        }
        throw std::system_error(errno, std::system_category());
      }

      for (int i = 0; i < n; ++i) {
        dispatch_event(events_[static_cast<std::size_t>(i)]);
      }

      processed += static_cast<std::size_t>(n);
      processed += drain_ready_tasks();

      if (!block) {
        break;
      }

      if (processed != 0) {
        break;
      }

      if (stop_requested()) {
        break;
      }
    }

    return processed;
  }

  inline std::size_t context::run_one() {
    return drive(true);
  }

  inline std::size_t context::run_some() {
    return drive(false);
  }

  inline void context::run_until_empty() {
    while (run_some() != 0) {
      continue;
    }
  }
} // namespace sio::event_loop::epoll
