#pragma once

#include "context.hpp"
#include "details.hpp"
#include "scheduler.hpp"

#include <stdexec/execution.hpp>

#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>
#include <iostream>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

namespace sio::event_loop::iouring {
  struct env {
    scheduler loop_scheduler_;

    auto query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept
      -> scheduler {
      return loop_scheduler_;
    }
  };

  template <
    class StopToken,
    class Callback,
    bool Enable = stdexec::stoppable_token_for<StopToken, Callback>>
  struct stop_callback_storage;

  template <class StopToken, class Callback>
  struct stop_callback_storage<StopToken, Callback, true> {
    using callback_type = stdexec::stop_callback_for_t<StopToken, Callback>;

    void emplace(const StopToken& token, Callback cb) noexcept(
      std::is_nothrow_constructible_v<callback_type, const StopToken&, Callback>) {
      callback_.emplace(token, std::move(cb));
    }

    void reset() noexcept {
      callback_.reset();
    }

   private:
    std::optional<callback_type> callback_{};
  };

  template <class StopToken, class Callback>
  struct stop_callback_storage<StopToken, Callback, false> {
    void emplace(const StopToken&, Callback) noexcept {
    }

    void reset() noexcept {
    }
  };

  template <class Derived, class Receiver>
  class submission_operation : public completion_base {
   protected:
    using receiver_type = Receiver;
    using env_type = stdexec::env_of_t<receiver_type&>;
    using stop_token_type = stdexec::stop_token_of_t<env_type>;

    struct on_stop {
      Derived* self;

      void operator()() noexcept {
        self->on_stop_requested();
      }
    };

    using stop_storage = stop_callback_storage<stop_token_type, on_stop>;

    submission_operation(io_context& ctx, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : completion_base{ctx, &submission_operation::dispatch}
      , receiver_(static_cast<Receiver&&>(rcvr)) {
    }

    ~submission_operation() = default;

    Derived& derived() noexcept {
      return static_cast<Derived&>(*this);
    }

    void on_stop_requested() noexcept {
      this->request_cancel();
    }

    void reset_stop_callback() noexcept {
      stop_callback_.reset();
    }

    Receiver& receiver() & noexcept {
      return receiver_;
    }

    Receiver&& receiver() && noexcept {
      return static_cast<Receiver&&>(receiver_);
    }

    static void dispatch(completion_base* base, const ::io_uring_cqe& cqe) noexcept {
      auto& self = static_cast<Derived&>(*base);
      self.reset_stop_callback();
      self.derived().on_completion(cqe);
    }

   public:
    friend void tag_invoke(stdexec::start_t, Derived& self) noexcept {
      auto stop_token = stdexec::get_stop_token(stdexec::get_env(self.receiver()));
      if (stop_token.stop_requested()) {
        stdexec::set_stopped(std::move(self).receiver());
        return;
      }
      try {
        if constexpr (stdexec::stoppable_token<stop_token_type>) {
          if (stop_token.stop_possible()) {
            self.stop_callback_.emplace(stop_token, on_stop{&self});
          }
        }
        self.context.with_submission_queue([&](::io_uring_sqe& sqe) {
          self.derived().prepare_submission(sqe);
          self.context.register_completion(self, sqe);
        });
      } catch (const std::system_error& err) {
        self.reset_stop_callback();
        auto ec = err.code();
        stdexec::set_error(std::move(self).receiver(), std::move(ec));
      }
    }

   private:
    stop_storage stop_callback_{};
    Receiver receiver_;
  };

  struct close_submission {
    explicit close_submission(int fd) noexcept
      : fd_{fd} {
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_CLOSE;
      sqe_.fd = fd_;
      sqe = sqe_;
    }

    int fd_{};
  };

  template <class Receiver>
  class close_operation
    : public submission_operation<close_operation<Receiver>, Receiver>
    , public close_submission {
   public:
    close_operation(io_context& ctx, int fd, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : submission_operation<close_operation<Receiver>, Receiver>{ctx, static_cast<Receiver&&>(rcvr)}
      , close_submission{fd} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      submit(sqe);
    }

    void on_completion(const ::io_uring_cqe& cqe) noexcept {
      if (this->cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(*this).receiver());
        return;
      }
      if (cqe.res == 0) {
        stdexec::set_value(std::move(*this).receiver());
      } else {
        auto ec = std::error_code(-cqe.res, std::system_category());
        stdexec::set_error(std::move(*this).receiver(), std::move(ec));
      }
    }
  };

  struct close_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{};
    int fd_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return close_operation<Receiver>{*context_, fd_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct read_submission {
    read_submission(sio::mutable_buffer_span buffers, int fd, ::off_t offset) noexcept
      : buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_READV;
      sqe_.fd = fd_;
      sqe_.off = offset_;
      sqe_.addr = std::bit_cast<__u64>(buffers_.begin());
      sqe_.len = buffers_.size();
      sqe = sqe_;
    }

    sio::mutable_buffer_span buffers_{};
    int fd_{};
    ::off_t offset_{};
  };

  struct read_submission_single {
    read_submission_single(sio::mutable_buffer buffer, int fd, ::off_t offset) noexcept
      : buffer_{buffer}
      , fd_{fd}
      , offset_{offset} {
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_READ;
      sqe_.fd = fd_;
      sqe_.off = offset_;
      sqe_.addr = std::bit_cast<__u64>(buffer_.data());
      sqe_.len = buffer_.size();
      sqe = sqe_;
    }

    sio::mutable_buffer buffer_{};
    int fd_{};
    ::off_t offset_{};
  };

  template <class Submission, class Receiver>
  class read_operation_base
    : public submission_operation<read_operation_base<Submission, Receiver>, Receiver>
    , public Submission {
   public:
    read_operation_base(
      io_context& ctx,
      auto data,
      int fd,
      ::off_t offset,
      Receiver&& rcvr) noexcept(std::is_nothrow_move_constructible_v<Receiver>)
      : submission_operation<
          read_operation_base<Submission, Receiver>,
          Receiver>{ctx, static_cast<Receiver&&>(rcvr)}
      , Submission{data, fd, offset} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      this->Submission::submit(sqe);
    }

    void on_completion(const ::io_uring_cqe& cqe) noexcept {
      if (this->cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(*this).receiver());
        return;
      }
      if (cqe.res >= 0) {
        stdexec::set_value(std::move(*this).receiver(), static_cast<std::size_t>(cqe.res));
      } else {
        auto ec = std::error_code(-cqe.res, std::system_category());
        stdexec::set_error(std::move(*this).receiver(), std::move(ec));
      }
    }
  };

  template <class Receiver>
  using read_operation = read_operation_base<read_submission, Receiver>;

  template <class Receiver>
  using read_operation_single = read_operation_base<read_submission_single, Receiver>;

  struct read_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{};
    sio::mutable_buffer_span buffers_{};
    int fd_{};
    ::off_t offset_{};

    read_sender(
      io_context& ctx,
      sio::mutable_buffer_span buffers,
      int fd,
      ::off_t offset = 0) noexcept
      : context_{&ctx}
      , buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return read_operation<Receiver>{
        *context_, buffers_, fd_, offset_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct read_sender_single {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{};
    sio::mutable_buffer buffer_{};
    int fd_{};
    ::off_t offset_{};

    read_sender_single(
      io_context& ctx,
      sio::mutable_buffer buffer,
      int fd,
      ::off_t offset = 0) noexcept
      : context_{&ctx}
      , buffer_{buffer}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return read_operation_single<Receiver>{
        *context_, buffer_, fd_, offset_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct write_submission {
    write_submission(sio::const_buffer_span buffers, int fd, ::off_t offset) noexcept
      : buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_WRITEV;
      sqe_.fd = fd_;
      sqe_.off = offset_;
      sqe_.addr = std::bit_cast<__u64>(buffers_.begin());
      sqe_.len = buffers_.size();
      sqe = sqe_;
    }

    sio::const_buffer_span buffers_{};
    int fd_{};
    ::off_t offset_{};
  };

  struct write_submission_single {
    write_submission_single(sio::const_buffer buffer, int fd, ::off_t offset) noexcept
      : buffer_{buffer}
      , fd_{fd}
      , offset_{offset} {
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_WRITE;
      sqe_.fd = fd_;
      sqe_.off = offset_;
      sqe_.addr = std::bit_cast<__u64>(buffer_.data());
      sqe_.len = buffer_.size();
      sqe = sqe_;
    }

    sio::const_buffer buffer_{};
    int fd_{};
    ::off_t offset_{};
  };

  template <class Submission, class Receiver>
  class write_operation_base
    : public submission_operation<write_operation_base<Submission, Receiver>, Receiver>
    , public Submission {
   public:
    write_operation_base(
      io_context& ctx,
      auto data,
      int fd,
      ::off_t offset,
      Receiver&& rcvr) noexcept(std::is_nothrow_move_constructible_v<Receiver>)
      : submission_operation<
          write_operation_base<Submission, Receiver>,
          Receiver>{ctx, static_cast<Receiver&&>(rcvr)}
      , Submission{data, fd, offset} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      this->Submission::submit(sqe);
    }

    void on_completion(const ::io_uring_cqe& cqe) noexcept {
      if (this->cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(*this).receiver());
        return;
      }
      if (cqe.res >= 0) {
        stdexec::set_value(std::move(*this).receiver(), static_cast<std::size_t>(cqe.res));
      } else {
        auto ec = std::error_code(-cqe.res, std::system_category());
        stdexec::set_error(std::move(*this).receiver(), std::move(ec));
      }
    }
  };

  template <class Receiver>
  using write_operation = write_operation_base<write_submission, Receiver>;

  template <class Receiver>
  using write_operation_single = write_operation_base<write_submission_single, Receiver>;

  struct write_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{};
    sio::const_buffer_span buffers_{};
    int fd_{};
    ::off_t offset_{-1};

    write_sender(
      io_context& ctx,
      sio::const_buffer_span buffers,
      int fd,
      ::off_t offset = -1) noexcept
      : context_{&ctx}
      , buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return write_operation<Receiver>{
        *context_, buffers_, fd_, offset_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct write_sender_single {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{};
    sio::const_buffer buffer_{};
    int fd_{};
    ::off_t offset_{-1};

    write_sender_single(
      io_context& ctx,
      sio::const_buffer buffer,
      int fd,
      ::off_t offset = -1) noexcept
      : context_{&ctx}
      , buffer_{buffer}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return write_operation_single<Receiver>{
        *context_, buffer_, fd_, offset_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct write_factory {
    io_context* context_{};
    int fd_{};

    write_sender operator()(sio::const_buffer_span data, ::off_t offset) const noexcept {
      return write_sender{*context_, data, fd_, offset};
    }

    write_sender_single operator()(sio::const_buffer data, ::off_t offset) const noexcept {
      return write_sender_single{*context_, data, fd_, offset};
    }
  };

  struct read_factory {
    io_context* context_{};
    int fd_{};

    read_sender operator()(sio::mutable_buffer_span data, ::off_t offset) const noexcept {
      return read_sender{*context_, data, fd_, offset};
    }

    read_sender_single operator()(sio::mutable_buffer data, ::off_t offset) const noexcept {
      return read_sender_single{*context_, data, fd_, offset};
    }
  };

  struct open_data {
    std::filesystem::path path_{};
    int dirfd_{AT_FDCWD};
    int flags_{0};
    ::mode_t mode_{0};
  };

  struct open_submission {
    explicit open_submission(open_data data) noexcept
      : data_{static_cast<open_data&&>(data)} {
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_OPENAT;
      sqe_.fd = data_.dirfd_;
      sqe_.addr = std::bit_cast<__u64>(data_.path_.c_str());
      sqe_.open_flags = data_.flags_;
      sqe_.len = data_.mode_;
      sqe = sqe_;
    }

    open_data data_;
  };

  template <class Receiver, class State>
  class file_open_operation_base
    : public submission_operation<file_open_operation_base<Receiver, State>, Receiver>
    , public open_submission {
   public:
    file_open_operation_base(
      open_data data,
      io_context& ctx,
      async::mode mode,
      async::creation creation,
      async::caching caching,
      Receiver&& rcvr)
      : submission_operation<
          file_open_operation_base<Receiver, State>,
          Receiver>{ctx, static_cast<Receiver&&>(rcvr)}
      , open_submission{static_cast<open_data&&>(data)}
      , mode_{mode}
      , creation_{creation}
      , caching_{caching} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      submit(sqe);
    }

    void on_completion(const ::io_uring_cqe& cqe) noexcept {
      if (this->cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(*this).receiver());
        return;
      }
      if (cqe.res >= 0) {
        State state{this->context, cqe.res, mode_, creation_, caching_};
        stdexec::set_value(std::move(*this).receiver(), std::move(state));
      } else {
        auto ec = std::error_code(-cqe.res, std::system_category());
        stdexec::set_error(std::move(*this).receiver(), std::move(ec));
      }
    }

   private:
    async::mode mode_;
    async::creation creation_;
    async::caching caching_;
  };

  template <class Receiver, class State>
  using file_open_operation = file_open_operation_base<Receiver, State>;

  template <class State>
  struct file_open_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(State),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{};
    open_data data_{};
    async::mode mode_{async::mode::read};
    async::creation creation_{async::creation::open_existing};
    async::caching caching_{async::caching::unchanged};

    file_open_sender(
      io_context& ctx,
      open_data data,
      async::mode mode,
      async::creation creation,
      async::caching caching) noexcept
      : context_{&ctx}
      , data_{static_cast<open_data&&>(data)}
      , mode_{mode}
      , creation_{creation}
      , caching_{caching} {
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return file_open_operation<Receiver, State>{
        open_data{data_}, *context_, mode_, creation_, caching_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

  template <class Protocol, class Receiver>
  class open_operation {
   public:
    open_operation(io_context& ctx, Protocol protocol, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : context_{&ctx}
      , protocol_{std::move(protocol)}
      , receiver_{static_cast<Receiver&&>(rcvr)} {
    }

    open_operation(open_operation&&) = delete;
    open_operation(const open_operation&) = delete;
    open_operation& operator=(open_operation&&) = delete;
    open_operation& operator=(const open_operation&) = delete;

    friend void tag_invoke(stdexec::start_t, open_operation& op) noexcept {
      auto stop_token = stdexec::get_stop_token(stdexec::get_env(op.receiver_));
      if (stop_token.stop_requested()) {
        stdexec::set_stopped(std::move(op.receiver_));
        return;
      }

      int fd = ::socket(op.protocol_.family(), op.protocol_.type(), op.protocol_.protocol());
      if (fd == -1) {
        auto ec = std::error_code(errno, std::system_category());
        stdexec::set_error(std::move(op.receiver_), std::move(ec));
      } else {
        stdexec::set_value(
          std::move(op.receiver_),
          socket_state<Protocol>{*op.context_, fd, std::move(op.protocol_)});
      }
    }

   private:
    io_context* context_;
    Protocol protocol_;
    Receiver receiver_;
  };

  template <class Protocol>
  struct open_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(socket_state<Protocol>),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{};
    Protocol protocol_;

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return open_operation<Protocol, Receiver>{
        *context_, protocol_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

  template <class Protocol, class Receiver>
  class connect_operation
    : public submission_operation<connect_operation<Protocol, Receiver>, Receiver> {
   public:
    connect_operation(
      socket_state<Protocol>& state,
      typename Protocol::endpoint peer_endpoint,
      Receiver&& rcvr) noexcept(std::is_nothrow_move_constructible_v<Receiver>)
      : submission_operation<
          connect_operation<Protocol, Receiver>,
          Receiver>{state.context(), static_cast<Receiver&&>(rcvr)}
      , state_{&state}
      , peer_endpoint_{peer_endpoint} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_CONNECT;
      sqe_.fd = state_->native_handle();
      sqe_.addr = std::bit_cast<__u64>(peer_endpoint_.data());
      sqe_.off = peer_endpoint_.size();
      sqe = sqe_;
    }

    void on_completion(const ::io_uring_cqe& cqe) noexcept {
      if (this->cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(*this).receiver());
        return;
      }
      if (cqe.res == 0) {
        stdexec::set_value(std::move(*this).receiver());
      } else {
        auto ec = std::error_code(-cqe.res, std::system_category());
        stdexec::set_error(std::move(*this).receiver(), std::move(ec));
      }
    }

   private:
    socket_state<Protocol>* state_{};
    typename Protocol::endpoint peer_endpoint_{};
  };

  template <class Protocol>
  struct connect_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    socket_state<Protocol>* state_{};
    typename Protocol::endpoint peer_endpoint_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return connect_operation<Protocol, Receiver>{
        *state_, peer_endpoint_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {state_->context().get_scheduler()};
    }
  };

  template <class Protocol, class Receiver>
  class sendmsg_operation
    : public submission_operation<sendmsg_operation<Protocol, Receiver>, Receiver> {
   public:
    sendmsg_operation(socket_state<Protocol>& state, Receiver&& rcvr, ::msghdr msg) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : submission_operation<
          sendmsg_operation<Protocol, Receiver>,
          Receiver>{state.context(), static_cast<Receiver&&>(rcvr)}
      , state_{&state}
      , msg_{msg} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_SENDMSG;
      sqe_.fd = state_->native_handle();
      sqe_.addr = std::bit_cast<__u64>(&msg_);
      sqe = sqe_;
    }

    void on_completion(const ::io_uring_cqe& cqe) noexcept {
      if (this->cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(*this).receiver());
        return;
      }
      if (cqe.res >= 0) {
        stdexec::set_value(std::move(*this).receiver(), static_cast<std::size_t>(cqe.res));
      } else {
        auto ec = std::error_code(-cqe.res, std::system_category());
        stdexec::set_error(std::move(*this).receiver(), std::move(ec));
      }
    }

   private:
    socket_state<Protocol>* state_{};
    ::msghdr msg_{};
  };

  template <class Protocol>
  struct sendmsg_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    socket_state<Protocol>* state_{};
    ::msghdr msg_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return sendmsg_operation<Protocol, Receiver>{
        *state_, static_cast<Receiver&&>(receiver), msg_};
    }

    env get_env() const noexcept {
      return {state_->context().get_scheduler()};
    }
  };

  template <class Protocol, class Receiver>
  class accept_operation
    : public submission_operation<accept_operation<Protocol, Receiver>, Receiver> {
   public:
    accept_operation(acceptor_state<Protocol>& state, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : submission_operation<
          accept_operation<Protocol, Receiver>,
          Receiver>{state.context(), static_cast<Receiver&&>(rcvr)}
      , state_{&state}
      , addrlen_{static_cast<socklen_t>(state.local_endpoint().size())} {
      std::cout << "[" << this << "] accept created" << std::endl;
    }

    ~accept_operation() {
      std::cout << "[" << this << "] accept destroyed" << std::endl;
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_ACCEPT;
      sqe_.fd = state_->native_handle();
      sqe_.addr = std::bit_cast<__u64>(state_->local_endpoint().data());
      sqe_.addr2 = std::bit_cast<__u64>(&addrlen_);
      sqe = sqe_;
      std::cout << "[" << this << "] Submit accept" << std::endl;
    }

    void on_completion(const ::io_uring_cqe& cqe) noexcept {
      if (this->cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
        std::cout << "[" << this << "] accept stopped" << std::endl;
        stdexec::set_stopped(std::move(*this).receiver());
      } else if (cqe.res >= 0) {
        std::cout << "[" << this << "] accept successed" << std::endl;
        socket_state<Protocol> state{state_->context(), cqe.res, state_->protocol()};
        stdexec::set_value(std::move(*this).receiver(), std::move(state));
      } else {
        std::cout << "[" << this << "] accept error" << std::endl;
        auto ec = std::error_code(-cqe.res, std::system_category());
        stdexec::set_error(std::move(*this).receiver(), std::move(ec));
      }
    }

   private:
    acceptor_state<Protocol>* state_{};
    socklen_t addrlen_{};
  };

  template <class Protocol>
  struct accept_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(socket_state<Protocol>),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    acceptor_state<Protocol>* state_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return accept_operation<Protocol, Receiver>{*state_, static_cast<Receiver&&>(receiver)};
    }

    // env get_env() const noexcept {
    //   return {state_->context().get_scheduler()};
    // }
  };

  template <class Protocol>
  typename socket_state<Protocol>::endpoint socket_state<Protocol>::local_endpoint() const {
    endpoint ep{};
    ::sockaddr_storage storage{};
    socklen_t length = static_cast<socklen_t>(sizeof(storage));
    if (::getsockname(native_handle(), reinterpret_cast<sockaddr*>(&storage), &length) == -1) {
      throw std::system_error(errno, std::system_category());
    }
    std::memcpy(ep.data(), &storage, static_cast<std::size_t>(length));
    return ep;
  }

  template <class Protocol>
  typename socket_state<Protocol>::endpoint socket_state<Protocol>::remote_endpoint() const {
    endpoint ep{};
    ::sockaddr_storage storage{};
    socklen_t length = static_cast<socklen_t>(sizeof(storage));
    if (::getpeername(native_handle(), reinterpret_cast<sockaddr*>(&storage), &length) == -1) {
      throw std::system_error(errno, std::system_category());
    }
    std::memcpy(ep.data(), &storage, static_cast<std::size_t>(length));
    return ep;
  }
} // namespace sio::event_loop::iouring
