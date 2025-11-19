#include "sio/async_resource.hpp"
#include "sio/buffer.hpp"
#include "sio/event_loop/file_handle.hpp"
#include "sio/event_loop/concepts.hpp"
#include "sio/io_concepts.hpp"
#include "common/backend_matrix.hpp"

#include <catch2/catch_all.hpp>

#include <exec/finally.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <array>
#include <utility>

using namespace stdexec;

TEMPLATE_LIST_TEST_CASE("file_handle - Open a streaming file", "[file_handle]", SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  CAPTURE(Backend::name);
  using loop_type = typename Backend::loop_type;

  if constexpr (!sio::event_loop::file_loop<loop_type>) {
    SKIP("Backend does not support file handles");
    return;
  }

  loop_type loop{};
  sio::event_loop::file file{&loop, "/dev/null", sio::async::mode::read};

  auto sender = let_value(
    file.open(),
    [](auto handle) {
      return handle.close();
    });

  stdexec::sync_wait(exec::when_any(std::move(sender), loop.run()));
}

TEMPLATE_LIST_TEST_CASE(
  "seekable_file_handle - Read from /dev/null",
  "[file_handle][seekable]",
  SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  CAPTURE(Backend::name);
  using loop_type = typename Backend::loop_type;

  if constexpr (!sio::event_loop::seekable_file_loop<loop_type>) {
    SKIP("Backend does not support seekable file handles");
    return;
  }

  loop_type loop{};
  sio::event_loop::seekable_file file{&loop, "/dev/null", sio::async::mode::read};

  auto sender = let_value(
    file.open(),
    [](auto handle) {
      std::array<std::byte, 8> buffer{};
      return handle.read_some(sio::buffer(buffer), 0)
           | let_value([buffer](std::size_t nbytes) mutable {
               CHECK(nbytes == 0);
               return just();
             })
           | exec::finally(handle.close());
    });

  stdexec::sync_wait(exec::when_any(std::move(sender), loop.run()));
}
