#pragma once

#include "details.hpp"

#include <liburing.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <system_error>

namespace sio::event_loop::iouring {
  class scheduler;
  struct completion_base;
  enum class run_mode;
  struct run_sender;

  class io_context {
   public:
    explicit io_context(unsigned int queue_depth = 128);
    io_context(const io_context&) = delete;
    io_context& operator=(const io_context&) = delete;
    io_context(io_context&&) = delete;
    io_context& operator=(io_context&&) = delete;
    ~io_context();

    scheduler get_scheduler() noexcept;

    ::io_uring& native_handle() noexcept {
      return ring_;
    }

    const ::io_uring& native_handle() const noexcept {
      return ring_;
    }

    bool stop_requested() const noexcept {
      return stop_requested_.load(std::memory_order_acquire);
    }

    void request_stop();

    void run_until_empty();

    std::size_t run_some();

    std::size_t run_one();

    run_sender run(run_mode mode = run_mode::stopped) noexcept;

    template <class Fn>
    void with_submission_queue(Fn&& fn) {
      std::unique_lock lk(submit_mutex_);
      ::io_uring_sqe* sqe = ::io_uring_get_sqe(&ring_);
      if (!sqe) {
        submit_locked();
        sqe = ::io_uring_get_sqe(&ring_);
        if (!sqe) {
          throw std::system_error(ENOSPC, std::system_category());
        }
      }
      fn(*sqe);
      ::io_uring_submit(&ring_);
    }

    void register_completion(completion_base& op, ::io_uring_sqe& sqe) noexcept;

    void cancel(completion_base& op) noexcept;

   private:
    friend class scheduler;

    void awaken();

    void submit_locked();

    std::size_t drive(bool block);

    void dispatch(const ::io_uring_cqe& cqe) noexcept;

    ::io_uring ring_{};
    std::mutex submit_mutex_{};
    std::atomic<bool> stop_requested_{false};
  };

  // Inline definitions ----------------------------------------------------- //

  inline io_context::io_context(unsigned int queue_depth) {
    int rc = ::io_uring_queue_init(queue_depth, &ring_, 0);
    if (rc != 0) {
      throw std::system_error(-rc, std::system_category());
    }
  }

  inline io_context::~io_context() {
    ::io_uring_queue_exit(&ring_);
  }

  inline void io_context::submit_locked() {
    int rc = ::io_uring_submit(&ring_);
    if (rc < 0) {
      throw std::system_error(-rc, std::system_category());
    }
  }

  inline void io_context::register_completion(completion_base& op, ::io_uring_sqe& sqe) noexcept {
    ::io_uring_sqe_set_data(&sqe, &op);
  }

  inline void io_context::cancel(completion_base& op) noexcept {
    op.cancelled.store(true, std::memory_order_release);
    try {
      with_submission_queue([&](::io_uring_sqe& sqe) {
        ::io_uring_prep_cancel(&sqe, &op, 0);
        ::io_uring_sqe_set_data(&sqe, nullptr);
      });
    } catch (...) {
      // Swallow errors during cancellation; the caller interprets via CQE.
    }
  }

  inline void io_context::awaken() {
    with_submission_queue([](::io_uring_sqe& sqe) {
      ::io_uring_prep_nop(&sqe);
      ::io_uring_sqe_set_data(&sqe, nullptr);
    });
  }

  inline void io_context::request_stop() {
    stop_requested_.store(true, std::memory_order_release);
    awaken();
  }

  inline std::size_t io_context::drive(bool block) {
    std::size_t processed = 0;
    while (true) {
      ::io_uring_cqe* cqe = nullptr;
      int rc = block ? ::io_uring_wait_cqe(&ring_, &cqe) : ::io_uring_peek_cqe(&ring_, &cqe);
      if (rc == -EAGAIN) {
        break;
      }
      if (rc == -EINTR) {
        continue;
      }
      if (rc < 0) {
        throw std::system_error(-rc, std::system_category());
      }
      if (!cqe) {
        break;
      }
      dispatch(*cqe);
      ::io_uring_cqe_seen(&ring_, cqe);
      ++processed;
      if (!block) {
        // continue draining ready completions without blocking
        continue;
      }
      if (stop_requested()) {
        break;
      }
    }
    return processed;
  }

  inline void io_context::dispatch(const ::io_uring_cqe& cqe) noexcept {
    auto* base = static_cast<completion_base*>(
      reinterpret_cast<void*>(::io_uring_cqe_get_data64(&cqe)));
    if (base != nullptr) {
      base->complete(cqe);
    }
  }

  inline std::size_t io_context::run_some() {
    return drive(false);
  }

  inline std::size_t io_context::run_one() {
    return drive(true);
  }

  inline void io_context::run_until_empty() {
    while (run_some() != 0) {
      continue;
    }
  }
} // namespace sio::event_loop::iouring
