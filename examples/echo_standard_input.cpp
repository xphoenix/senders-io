#include <sio/event_loop/file_handle.hpp>
#include <sio/event_loop/stdexec/backend.hpp>
#include <sio/sequence/reduce.hpp>
#include <sio/buffer.hpp>

#include <exec/task.hpp>
#include <exec/when_any.hpp>

#include <iostream>
#include <unistd.h>

template <class Tp>
using task = exec::basic_task<
  Tp,
  exec::__task::__default_task_context_impl<exec::__task::__scheduler_affinity::__none>>;

template <std::size_t N>
std::span<const std::byte> as_bytes(const char (&array)[N]) {
  std::span<const char> span(array);
  return std::span<const std::byte>(
    reinterpret_cast<const std::byte*>(span.data()), span.size_bytes());
}

template <std::size_t N>
std::span<std::byte> as_bytes(char (&array)[N]) {
  std::span<char> span(array);
  return std::span<std::byte>(reinterpret_cast<std::byte*>(span.data()), span.size_bytes());
}

task<void>
  echo(sio::async::readable_byte_stream auto in, sio::async::writable_byte_stream auto out) {
  char buffer[64] = {};
  std::size_t nbytes = co_await sio::async::read_some(in, sio::buffer(buffer));
  while (nbytes) {
    auto written_bytes = co_await sio::async::write(
      out, sio::buffer(std::as_const(buffer)).prefix(nbytes));
    if (written_bytes != nbytes) {
      std::cerr << "Failed to write all bytes" << std::endl;
      co_return;
    }
    nbytes = co_await sio::async::read_some(in, sio::buffer(buffer));
  }
  co_return;
}

int main() {
  using backend = sio::event_loop::stdexec_backend::backend;
  backend loop{};
  auto out = sio::event_loop::file_handle<backend>::adopt(
    loop,
    STDOUT_FILENO,
    sio::async::mode::write);
  auto in = sio::event_loop::file_handle<backend>::adopt(
    loop,
    STDIN_FILENO,
    sio::async::mode::read);

  stdexec::sync_wait(exec::when_any(echo(std::move(in), std::move(out)), loop.run()));
}
