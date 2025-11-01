#include "sio/async_resource.hpp"
#include "sio/buffer.hpp"
#include "sio/event_loop/file_handle.hpp"
#include "sio/io_concepts.hpp"
#include "backend_matrix.hpp"

#include <catch2/catch_all.hpp>

#include <exec/finally.hpp>
#include <stdexec/execution.hpp>

#include <array>
#include <utility>

using namespace stdexec;

TEMPLATE_TEST_CASE("file_handle - Open a streaming file", "[file_handle]", SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  if constexpr (!Backend::available) {
    SUCCEED();
    return;
  }

  CAPTURE(Backend::name);
  typename Backend::loop_type loop{};
  sio::event_loop::file<typename Backend::loop_type> file{
    &loop, "/dev/null", sio::async::mode::read};

  Backend::sync_wait(
    loop,
    let_value(
      file.open(),
      [](auto handle) {
        return handle.close();
      }));
}

TEMPLATE_TEST_CASE(
  "seekable_file_handle - Read from /dev/null",
  "[file_handle][seekable]",
  SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  if constexpr (!Backend::available) {
    SUCCEED();
    return;
  }

  CAPTURE(Backend::name);
  typename Backend::loop_type loop{};
  sio::event_loop::seekable_file<typename Backend::loop_type> file{
    &loop, "/dev/null", sio::async::mode::read};

  Backend::sync_wait(
    loop,
    let_value(
      file.open(),
      [](auto handle) {
        std::array<std::byte, 8> buffer{};
        return handle.read_some(sio::buffer(buffer), 0)
             | let_value([buffer](std::size_t nbytes) mutable {
                 CHECK(nbytes == 0);
                 return just();
               })
             | exec::finally(handle.close());
      }));
}
