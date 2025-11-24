#pragma once

#include "./concepts.hpp"

#include "../io_concepts.hpp"

#include <stdexec/execution.hpp>

#include <fcntl.h>

#include <filesystem>
#include <type_traits>
#include <utility>

namespace sio::event_loop {
  template <class Loop>
    requires file_loop<Loop>
  struct file_handle {
    using loop_type = std::remove_cvref_t<Loop>;
    using state_type = file_state_t<loop_type>;
    using buffer_type = sio::mutable_buffer;
    using buffers_type = sio::mutable_buffer_span;
    using const_buffer_type = sio::const_buffer;
    using const_buffers_type = sio::const_buffer_span;
    using native_handle_type = typename loop_type::native_handle_type;

    loop_type* context_{nullptr};
    state_type state_{};

    file_handle() = default;

    file_handle(loop_type& context, state_type&& state) noexcept
      : context_{&context}
      , state_{static_cast<state_type&&>(state)} {
    }

    loop_type& context() const noexcept {
      SIO_ASSERT(context_ != nullptr);
      return *context_;
    }

    bool is_open() const noexcept {
      return state_.is_valid();
    }

    native_handle_type native_handle() const noexcept {
      return state().native_handle();
    }

    auto close() const {
      return context().close(const_cast<state_type&>(state()));
    }

    auto read_some(sio::mutable_buffer_span buffers) const {
      return context().read_some(const_cast<state_type&>(state()), buffers);
    }

    auto read_some(sio::mutable_buffer buffer) const {
      return context().read_some(const_cast<state_type&>(state()), buffer);
    }

    auto read(sio::mutable_buffer_span buffers) const {
      return context().read(const_cast<state_type&>(state()), buffers);
    }

    auto read(sio::mutable_buffer buffer) const {
      return context().read(const_cast<state_type&>(state()), buffer);
    }

    auto write_some(sio::const_buffer_span buffers) const {
      return context().write_some(const_cast<state_type&>(state()), buffers);
    }

    auto write_some(sio::const_buffer buffer) const {
      return context().write_some(const_cast<state_type&>(state()), buffer);
    }

    auto write(sio::const_buffer_span buffers) const {
      return context().write(const_cast<state_type&>(state()), buffers);
    }

    auto write(sio::const_buffer buffer) const {
      return context().write(const_cast<state_type&>(state()), buffer);
    }

  private:
    friend loop_type;

    state_type& state() noexcept {
      SIO_ASSERT(is_open());
      return state_;
    }

    const state_type& state() const noexcept {
      SIO_ASSERT(is_open());
      return state_;
    }
  };

  template <class Loop>
    requires seekable_file_loop<Loop>
  struct seekable_file_handle {
    using loop_type = std::remove_cvref_t<Loop>;
    using state_type = seekable_file_state_t<loop_type>;
    using buffer_type = sio::mutable_buffer;
    using buffers_type = sio::mutable_buffer_span;
    using const_buffer_type = sio::const_buffer;
    using const_buffers_type = sio::const_buffer_span;
    using offset_type = ::off_t;
    using native_handle_type = typename loop_type::native_handle_type;

    loop_type* context_{nullptr};
    state_type state_{};

    seekable_file_handle() = default;

    seekable_file_handle(loop_type& context, state_type&& state) noexcept
      : context_{&context}
      , state_{static_cast<state_type&&>(state)} {
    }

    loop_type& context() const noexcept {
      SIO_ASSERT(context_ != nullptr);
      return *context_;
    }

    bool is_open() const noexcept {
      return state_.is_valid();
    }

    native_handle_type native_handle() const noexcept {
      return state().native_handle();
    }

    auto close() const {
      return context().close(const_cast<state_type&>(state()));
    }

    auto read_some(sio::mutable_buffer_span buffers) const {
      return context().read_some(const_cast<state_type&>(state()), buffers);
    }

    auto read_some(sio::mutable_buffer buffer) const {
      return context().read_some(const_cast<state_type&>(state()), buffer);
    }

    auto read(sio::mutable_buffer_span buffers) const {
      return context().read(const_cast<state_type&>(state()), buffers);
    }

    auto read(sio::mutable_buffer buffer) const {
      return context().read(const_cast<state_type&>(state()), buffer);
    }

    auto read_some(sio::mutable_buffer_span buffers, ::off_t offset) const {
      return context().read_some(const_cast<state_type&>(state()), buffers, offset);
    }

    auto read_some(sio::mutable_buffer buffer, ::off_t offset) const {
      return context().read_some(const_cast<state_type&>(state()), buffer, offset);
    }

    auto read(sio::mutable_buffer_span buffers, ::off_t offset) const {
      return context().read(const_cast<state_type&>(state()), buffers, offset);
    }

    auto read(sio::mutable_buffer buffer, ::off_t offset) const {
      return context().read(const_cast<state_type&>(state()), buffer, offset);
    }

    auto write_some(sio::const_buffer_span buffers) const {
      return context().write_some(const_cast<state_type&>(state()), buffers);
    }

    auto write_some(sio::const_buffer buffer) const {
      return context().write_some(const_cast<state_type&>(state()), buffer);
    }

    auto write(sio::const_buffer_span buffers) const {
      return context().write(const_cast<state_type&>(state()), buffers);
    }

    auto write(sio::const_buffer buffer) const {
      return context().write(const_cast<state_type&>(state()), buffer);
    }

    auto write_some(sio::const_buffer_span buffers, ::off_t offset) const {
      return context().write_some(const_cast<state_type&>(state()), buffers, offset);
    }

    auto write_some(sio::const_buffer buffer, ::off_t offset) const {
      return context().write_some(const_cast<state_type&>(state()), buffer, offset);
    }

    auto write(sio::const_buffer_span buffers, ::off_t offset) const {
      return context().write(const_cast<state_type&>(state()), buffers, offset);
    }

    auto write(sio::const_buffer buffer, ::off_t offset) const {
      return context().write(const_cast<state_type&>(state()), buffer, offset);
    }

  private:
    friend loop_type;

    state_type& state() noexcept {
      SIO_ASSERT(is_open());
      return state_;
    }

    const state_type& state() const noexcept {
      SIO_ASSERT(is_open());
      return state_;
    }
  };

  template <class Loop>
    requires file_loop<Loop>
  struct file {
    using loop_type = std::remove_cvref_t<Loop>;
    using handle_type = file_handle<loop_type>;

    loop_type& context_;
    std::filesystem::path path_;
    async::mode mode_{async::mode::read};
    async::creation creation_{async::creation::open_existing};
    async::caching caching_{async::caching::unchanged};
    int dirfd_{AT_FDCWD};

    explicit file(
      loop_type& context,
      std::filesystem::path path,
      async::mode mode = async::mode::read,
      async::creation creation = async::creation::open_existing,
      async::caching caching = async::caching::unchanged,
      int dirfd = AT_FDCWD) noexcept
      : context_{context}
      , path_{static_cast<std::filesystem::path&&>(path)}
      , mode_{mode}
      , creation_{creation}
      , caching_{caching}
      , dirfd_{dirfd} {
    }

    explicit file(
      loop_type* context,
      std::filesystem::path path,
      async::mode mode = async::mode::read,
      async::creation creation = async::creation::open_existing,
      async::caching caching = async::caching::unchanged,
      int dirfd = AT_FDCWD) noexcept
      : file(*context, static_cast<std::filesystem::path&&>(path), mode, creation, caching, dirfd) {
    }

    auto open() noexcept {
      return ::stdexec::then(
        context_.open_file(path_, mode_, creation_, caching_, dirfd_),
        [pc = &context_](state_type state) {
          return handle_type{*pc, static_cast<state_type&&>(state)};
        });
    }
   private:
    using state_type = file_state_t<loop_type>;
  };

  template <class Loop>
    requires seekable_file_loop<Loop>
  struct seekable_file {
    using loop_type = std::remove_cvref_t<Loop>;
    using handle_type = seekable_file_handle<loop_type>;

    loop_type& context_;
    std::filesystem::path path_;
    async::mode mode_{async::mode::read};
    async::creation creation_{async::creation::open_existing};
    async::caching caching_{async::caching::unchanged};
    int dirfd_{AT_FDCWD};

    explicit seekable_file(
      loop_type& context,
      std::filesystem::path path,
      async::mode mode = async::mode::read,
      async::creation creation = async::creation::open_existing,
      async::caching caching = async::caching::unchanged,
      int dirfd = AT_FDCWD) noexcept
      : context_{context}
      , path_{static_cast<std::filesystem::path&&>(path)}
      , mode_{mode}
      , creation_{creation}
      , caching_{caching}
      , dirfd_{dirfd} {
    }

    explicit seekable_file(
      loop_type* context,
      std::filesystem::path path,
      async::mode mode = async::mode::read,
      async::creation creation = async::creation::open_existing,
      async::caching caching = async::caching::unchanged,
      int dirfd = AT_FDCWD) noexcept
      : seekable_file(*context, static_cast<std::filesystem::path&&>(path), mode, creation, caching, dirfd) {
    }

    auto open() noexcept {
      return ::stdexec::then(
        context_.open_seekable_file(path_, mode_, creation_, caching_, dirfd_),
        [pc = &context_](state_type state) {
          return handle_type{*pc, static_cast<state_type&&>(state)};
        });
    }
   private:
    using state_type = seekable_file_state_t<loop_type>;
  };
} // namespace sio::event_loop

namespace sio::event_loop {

  template <class Loop, class... Args>
    requires file_loop<std::remove_reference_t<Loop>>
  file(Loop& context, Args&&...)
    -> file<std::remove_reference_t<Loop>>;

  template <class Loop, class... Args>
    requires file_loop<std::remove_reference_t<Loop>>
  file(Loop* context, Args&&...)
    -> file<std::remove_reference_t<Loop>>;

  template <class Loop, class... Args>
    requires seekable_file_loop<std::remove_reference_t<Loop>>
  seekable_file(Loop& context, Args&&...)
    -> seekable_file<std::remove_reference_t<Loop>>;

  template <class Loop, class... Args>
    requires seekable_file_loop<std::remove_reference_t<Loop>>
  seekable_file(Loop* context, Args&&...)
    -> seekable_file<std::remove_reference_t<Loop>>;

} // namespace sio::event_loop
