#include "sio/async_resource.hpp"
#include "sio/buffer.hpp"
#include "sio/event_loop/file_handle.hpp"
#include "sio/event_loop/stdexec/backend.hpp"
#include "sio/io_concepts.hpp"

#include <catch2/catch_all.hpp>

#include <exec/finally.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <array>
#include <utility>

 using backend = sio::event_loop::stdexec_backend::backend;

template <stdexec::sender Sender>
void sync_wait(backend& loop, Sender&& sender) {
  stdexec::sync_wait(exec::when_any(std::forward<Sender>(sender), loop.run()));
}

TEST_CASE("file_handle - Open a streaming file", "[file_handle]") {
  backend loop{};
  sio::event_loop::file<backend> file{&loop, "/dev/null", sio::async::mode::read};

  sync_wait(
    loop,
    stdexec::let_value(
      file.open(),
      [](auto handle) {
        return handle.close();
      }));
}

TEST_CASE("seekable_file_handle - Read from /dev/null", "[file_handle][seekable]") {
  backend loop{};
  sio::event_loop::seekable_file<backend> file{&loop, "/dev/null", sio::async::mode::read};

  sync_wait(
    loop,
    stdexec::let_value(
      file.open(),
      [](auto handle) {
        std::array<std::byte, 8> buffer{};
        return handle.read_some(sio::buffer(buffer), 0)
             | stdexec::let_value([buffer](std::size_t nbytes) mutable {
                 CHECK(nbytes == 0);
                 return stdexec::just();
               })
             | exec::finally(handle.close());
      }));
}
