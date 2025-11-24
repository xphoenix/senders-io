#pragma once

#include "../../intrusive/list.hpp"
#include "../../intrusive/queue.hpp"

#include <stdexec/execution.hpp>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <system_error>
#include <utility>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace sio::event_loop::epoll {
  class scheduler;
  struct run_sender;
  struct fd_operation_base_common;
  namespace detail {
    template <class Receiver>
    class schedule_operation;
  }

  enum class interest {
    none,
    read,
    write
  };

  struct descriptor_token {
    using slot_type = std::uint32_t;
    using epoch_type = std::uint32_t;

    static constexpr slot_type invalid_slot = std::numeric_limits<slot_type>::max();

    constexpr descriptor_token() = default;

    constexpr descriptor_token(slot_type slot_value, epoch_type epoch_value) noexcept
      : slot{slot_value}
      , epoch{epoch_value} {
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
      return slot != invalid_slot;
    }

    [[nodiscard]] constexpr descriptor_token native_handle() const noexcept {
      return *this;
    }

    slot_type slot{invalid_slot};
    epoch_type epoch{0};
  };

  struct runnable_base {
    runnable_base* next_{nullptr};
    void (*execute_)(runnable_base&) noexcept{nullptr};

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

  class context;

  struct descriptor_entry {
    struct wait_queue {
      fd_operation_base_common* head{nullptr};
      fd_operation_base_common* tail{nullptr};

      [[nodiscard]] bool empty() const noexcept {
        return head == nullptr;
      }

      void push_back(fd_operation_base_common* op) noexcept;

      void erase(fd_operation_base_common* op) noexcept;

      fd_operation_base_common* pop_front() noexcept;

      wait_queue take_all() noexcept {
        wait_queue tmp{head, tail};
        head = nullptr;
        tail = nullptr;
        return tmp;
      }

      void clear() noexcept {
        head = nullptr;
        tail = nullptr;
      }
    };

    descriptor_entry() = default;
    descriptor_entry(const descriptor_entry&) = delete;
    descriptor_entry& operator=(const descriptor_entry&) = delete;

    void set_context(context* ctx, descriptor_token::slot_type slot) noexcept;

    descriptor_token::epoch_type bump_epoch() noexcept;

    void reset_lists() noexcept;

    void add_waiter(fd_operation_base_common& op, interest what) noexcept;

    void remove_waiter(fd_operation_base_common& op) noexcept;

    void handle_events(std::uint32_t events) noexcept;

    [[nodiscard]] std::uint32_t compute_mask_locked() const noexcept;

    void update_interest_locked(std::uint32_t mask) noexcept;

    [[nodiscard]] int native_handle() const noexcept {
      return fd_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool has_fd() const noexcept {
      return native_handle() >= 0;
    }

    context* ctx_{nullptr};
    descriptor_token::slot_type slot_{descriptor_token::invalid_slot};
    descriptor_token::epoch_type epoch_{0};
    std::atomic<int> fd_{-1};
    bool registered_{false};
    std::uint32_t interest_mask_{0};
    std::mutex mutex_{};
    wait_queue read_waiters_{};
    wait_queue write_waiters_{};
    descriptor_entry* free_next_{nullptr};
    descriptor_entry* free_prev_{nullptr};
  };

  class fd_operation_base_common : public runnable_base {
   public:
    fd_operation_base_common(context& ctx, descriptor_token token, void (*fn)(runnable_base&) noexcept) noexcept;
    fd_operation_base_common(const fd_operation_base_common&) = delete;
    fd_operation_base_common& operator=(const fd_operation_base_common&) = delete;

    bool wait_on(interest what) noexcept;
    void clear_waiting() noexcept;
    bool try_cancel() noexcept;
    [[nodiscard]] context& loop_context() const noexcept {
      return ctx_;
    }

    [[nodiscard]] descriptor_entry* entry() const noexcept {
      return entry_;
    }

    [[nodiscard]] bool ensure_entry() noexcept;

    void release_entry() noexcept {
      entry_ = nullptr;
    }

    bool stop_requested() const noexcept {
      return cancelled_.load(std::memory_order_acquire);
    }

    void request_stop() noexcept;

    bool wait_for(interest what) noexcept {
      return wait_on(what);
    }

    fd_operation_base_common* wait_next_{nullptr};
    fd_operation_base_common* wait_prev_{nullptr};
    interest waiting_interest_{interest::none};
    bool waiting_{false};
    std::atomic<bool> cancelled_{false};

   private:
    friend struct descriptor_entry;

    context& ctx_;
    descriptor_token token_{};
    descriptor_entry* entry_{nullptr};
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

    descriptor_token register_descriptor(int fd);

    descriptor_entry* lookup(descriptor_token token) noexcept;

    [[nodiscard]] int native_handle(descriptor_token token) const;

    std::error_code release_entry(descriptor_token token) noexcept;

   private:
    friend class scheduler;
    friend struct run_sender;
    template <class Receiver>
    friend class detail::schedule_operation;
    friend class fd_operation_base_common;
    friend struct descriptor_entry;

    struct wake_token {
    };

    void wake() noexcept;

    void drain_wake_fd() noexcept;

    std::size_t drain_ready_tasks() noexcept;

    std::size_t drive(bool block);

    void dispatch_event(const ::epoll_event& event) noexcept;

    descriptor_entry* allocate_slot();

    void update_interest(descriptor_entry& entry, std::uint32_t mask) noexcept;

    static descriptor_token::epoch_type next_epoch(descriptor_entry& entry) noexcept;

    int epoll_fd_{-1};
    int wake_fd_{-1};
    wake_token wake_token_{};
    std::array<::epoll_event, 256> events_{};
    sio::intrusive::queue<&runnable_base::next_> ready_queue_{};
    std::mutex ready_mutex_{};
    std::atomic<bool> stop_requested_{false};
    std::deque<descriptor_entry> entries_{};
    sio::intrusive::list<&descriptor_entry::free_next_, &descriptor_entry::free_prev_> free_list_{};
    mutable std::shared_mutex entries_mutex_{};
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
    auto* entry = static_cast<descriptor_entry*>(event.data.ptr);
    if (entry != nullptr && entry->has_fd()) {
      entry->handle_events(event.events);
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

  inline fd_operation_base_common::fd_operation_base_common(
    context& ctx,
    descriptor_token token,
    void (*fn)(runnable_base&) noexcept) noexcept
    : runnable_base{fn}
    , ctx_{ctx}
    , token_{token} {
  }

  inline bool fd_operation_base_common::wait_on(interest what) noexcept {
    waiting_interest_ = what;
    waiting_ = true;
    if (!ensure_entry()) {
      waiting_interest_ = interest::none;
      waiting_ = false;
      return false;
    }
    entry_->add_waiter(*this, what);
    return true;
  }

  inline void fd_operation_base_common::clear_waiting() noexcept {
    waiting_interest_ = interest::none;
    waiting_ = false;
  }

  inline bool fd_operation_base_common::try_cancel() noexcept {
    bool already = cancelled_.exchange(true, std::memory_order_acq_rel);
    if (!already && waiting_ && entry_ != nullptr) {
      entry_->remove_waiter(*this);
    }
    return !already;
  }

  inline bool fd_operation_base_common::ensure_entry() noexcept {
    if (entry_ != nullptr) {
      if (!entry_->has_fd() || entry_->epoch_ != token_.epoch) {
        entry_ = nullptr;
      } else {
        return true;
      }
    }
    entry_ = ctx_.lookup(token_);
    return entry_ != nullptr;
  }

  inline void fd_operation_base_common::request_stop() noexcept {
    if (try_cancel()) {
      ctx_.enqueue_task(*this);
    }
  }

  inline void descriptor_entry::wait_queue::push_back(fd_operation_base_common* op) noexcept {
    op->wait_next_ = nullptr;
    op->wait_prev_ = tail;
    if (tail == nullptr) {
      head = op;
    } else {
      tail->wait_next_ = op;
    }
    tail = op;
  }

  inline void descriptor_entry::wait_queue::erase(fd_operation_base_common* op) noexcept {
    if (op->wait_prev_ == nullptr) {
      head = op->wait_next_;
    } else {
      op->wait_prev_->wait_next_ = op->wait_next_;
    }
    if (op->wait_next_ == nullptr) {
      tail = op->wait_prev_;
    } else {
      op->wait_next_->wait_prev_ = op->wait_prev_;
    }
    op->wait_prev_ = nullptr;
    op->wait_next_ = nullptr;
  }

  inline fd_operation_base_common* descriptor_entry::wait_queue::pop_front() noexcept {
    if (head == nullptr) {
      return nullptr;
    }
    fd_operation_base_common* op = head;
    head = op->wait_next_;
    if (head == nullptr) {
      tail = nullptr;
    } else {
      head->wait_prev_ = nullptr;
    }
    op->wait_next_ = nullptr;
    op->wait_prev_ = nullptr;
    return op;
  }

  inline void descriptor_entry::set_context(context* ctx, descriptor_token::slot_type slot) noexcept {
    ctx_ = ctx;
    slot_ = slot;
  }

  inline descriptor_token::epoch_type descriptor_entry::bump_epoch() noexcept {
    ++epoch_;
    if (epoch_ == 0) {
      ++epoch_;
    }
    return epoch_;
  }

  inline void descriptor_entry::reset_lists() noexcept {
    read_waiters_.clear();
    write_waiters_.clear();
  }

  inline std::uint32_t descriptor_entry::compute_mask_locked() const noexcept {
    std::uint32_t mask = 0;
    if (!read_waiters_.empty()) {
      mask |= static_cast<std::uint32_t>(EPOLLIN | EPOLLRDHUP | EPOLLERR);
    }
    if (!write_waiters_.empty()) {
      mask |= static_cast<std::uint32_t>(EPOLLOUT | EPOLLERR);
    }
    return mask;
  }

  inline void descriptor_entry::update_interest_locked(std::uint32_t mask) noexcept {
    if (ctx_ != nullptr) {
      ctx_->update_interest(*this, mask);
    }
  }

  inline void descriptor_entry::add_waiter(fd_operation_base_common& op, interest what) noexcept {
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

  inline void descriptor_entry::remove_waiter(fd_operation_base_common& op) noexcept {
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

  inline void descriptor_entry::handle_events(std::uint32_t events) noexcept {
    wait_queue ready_readers{};
    wait_queue ready_writers{};

    const bool wake_read = (events & (EPOLLIN | EPOLLERR | EPOLLRDHUP | EPOLLHUP)) != 0;
    const bool wake_write = (events & (EPOLLOUT | EPOLLERR | EPOLLHUP)) != 0;

    if (wake_read || wake_write) {
      std::lock_guard lk(mutex_);
      if (wake_read) {
        ready_readers = read_waiters_.take_all();
      }
      if (wake_write) {
        ready_writers = write_waiters_.take_all();
      }
      update_interest_locked(compute_mask_locked());
    }

    auto resume = [this](wait_queue& list) {
      while (auto* op = list.pop_front()) {
        op->clear_waiting();
        ctx_->enqueue_task(*op);
      }
    };

    resume(ready_readers);
    resume(ready_writers);
  }

  inline descriptor_entry* context::allocate_slot() {
    if (auto* entry = free_list_.pop_front()) {
      return entry;
    }
    auto slot = static_cast<descriptor_token::slot_type>(entries_.size());
    entries_.emplace_back();
    descriptor_entry& entry = entries_.back();
    entry.set_context(this, slot);
    return &entry;
  }

  inline descriptor_token::epoch_type context::next_epoch(descriptor_entry& entry) noexcept {
    return entry.bump_epoch();
  }

  inline descriptor_token context::register_descriptor(int fd) {
    std::unique_lock lk(entries_mutex_);
    descriptor_entry* entry = allocate_slot();
    entry->set_context(this, entry->slot_);
    entry->fd_.store(fd, std::memory_order_release);
    entry->registered_ = false;
    entry->interest_mask_ = 0;
    entry->reset_lists();
    auto epoch = next_epoch(*entry);
    return descriptor_token{entry->slot_, epoch};
  }

  inline descriptor_entry* context::lookup(descriptor_token token) noexcept {
    if (!token.is_valid()) {
      return nullptr;
    }
    std::shared_lock lk(entries_mutex_);
    if (token.slot >= entries_.size()) {
      return nullptr;
    }
    descriptor_entry& entry = entries_[token.slot];
    if (entry.epoch_ != token.epoch || !entry.has_fd()) {
      return nullptr;
    }
    return &entry;
  }

  inline int context::native_handle(descriptor_token token) const {
    if (!token.is_valid()) {
      throw std::system_error(std::make_error_code(std::errc::bad_file_descriptor));
    }
    std::shared_lock lk(entries_mutex_);
    if (token.slot >= entries_.size()) {
      throw std::system_error(std::make_error_code(std::errc::bad_file_descriptor));
    }
    const descriptor_entry& entry = entries_[token.slot];
    if (entry.epoch_ != token.epoch || !entry.has_fd()) {
      throw std::system_error(std::make_error_code(std::errc::bad_file_descriptor));
    }
    return entry.fd_.load(std::memory_order_acquire);
  }

  inline std::error_code context::release_entry(descriptor_token token) noexcept {
    descriptor_entry* entry = nullptr;
    descriptor_entry::wait_queue ready_readers{};
    descriptor_entry::wait_queue ready_writers{};
    int fd = -1;
    {
      std::unique_lock slots_lock(entries_mutex_);
      if (!token.is_valid() || token.slot >= entries_.size()) {
        return std::make_error_code(std::errc::bad_file_descriptor);
      }
      entry = &entries_[token.slot];
      if (entry->epoch_ != token.epoch || !entry->has_fd()) {
        return std::make_error_code(std::errc::bad_file_descriptor);
      }
      fd = entry->fd_.load(std::memory_order_acquire);
      {
        std::lock_guard lk(entry->mutex_);
        ready_readers = entry->read_waiters_.take_all();
        ready_writers = entry->write_waiters_.take_all();
        entry->interest_mask_ = 0;
        entry->registered_ = false;
      }
      entry->fd_.store(-1, std::memory_order_release);
      entry->reset_lists();
      free_list_.push_back(entry);
      next_epoch(*entry);
    }

    if (fd >= 0) {
      ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
      if (entry != nullptr) {
        ::close(fd);
      }
    }

    auto resume = [this](descriptor_entry::wait_queue& list) {
      while (auto* op = list.pop_front()) {
        op->clear_waiting();
        enqueue_task(*op);
      }
    };

    resume(ready_readers);
    resume(ready_writers);
    return {};
  }

  inline void context::update_interest(descriptor_entry& entry, std::uint32_t mask) noexcept {
    int fd = entry.fd_.load(std::memory_order_acquire);
    if (fd < 0) {
      return;
    }
    if (mask == entry.interest_mask_) {
      return;
    }
    ::epoll_event ev;
    ev.events = mask;
    ev.data.ptr = &entry;
    if (!entry.registered_) {
      if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0) {
        entry.registered_ = true;
        entry.interest_mask_ = mask;
      }
      return;
    }
    if (mask == 0) {
      if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0) {
        entry.registered_ = false;
        entry.interest_mask_ = 0;
      }
      return;
    }
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0) {
      entry.interest_mask_ = mask;
    }
  }
}
