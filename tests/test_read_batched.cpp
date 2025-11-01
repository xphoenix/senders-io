#include <sio/read_batched.hpp>
#include <sio/event_loop/file_handle.hpp>
#include <sio/sequence/fork.hpp>
#include <sio/sequence/iterate.hpp>
#include <sio/sequence/ignore_all.hpp>
#include <sio/buffer.hpp>
#include "backend_matrix.hpp"

#include <catch2/catch_all.hpp>

#include <exec/finally.hpp>
#include <stdexec/execution.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include <cstring>

using namespace sio;
using namespace stdexec;
using namespace sio::async;

TEMPLATE_TEST_CASE("read_batched - Read from a file", "[read_batched]", SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  if constexpr (!Backend::available) {
    SUCCEED();
    return;
  }

  auto tmp_dir = std::filesystem::temp_directory_path();
  std::random_device rd;
  auto path = tmp_dir / ("sio-read-batched-" + std::to_string(rd()) + ".bin");

  std::vector<std::byte> data(4096, std::byte{0});
  auto write_int = [&data](std::size_t offset, int value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
  };
  write_int(0, 42);
  write_int(1024, 4242);
  write_int(2048, 424242);

  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.is_open());
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }

  CAPTURE(Backend::name);

  typename Backend::loop_type loop{};
  sio::event_loop::seekable_file<typename Backend::loop_type> resource{
    &loop,
    path,
    sio::async::mode::read,
    sio::async::creation::open_existing};

  int values[3] = {};
  using handle_type = sio::event_loop::seekable_file_handle<typename Backend::loop_type>;
  using offset_type = sio::async::offset_type_of_t<handle_type>;
  offset_type offsets[3] = {0, 1024, 2048};
  sio::mutable_buffer bytes[3] = {
    sio::mutable_buffer(&values[0], sizeof(int)),
    sio::mutable_buffer(&values[1], sizeof(int)),
    sio::mutable_buffer(&values[2], sizeof(int))};

  auto sndr = stdexec::let_value(
    resource.open(),
    [&](auto handle) {
      return sio::async::read_batched(handle, bytes, offsets)
           | exec::finally(handle.close());
    });

  Backend::sync_wait(loop, std::move(sndr));

  CHECK(values[0] == 42);
  CHECK(values[1] == 4242);
  CHECK(values[2] == 424242);

  std::error_code ec;
  std::filesystem::remove(path, ec);
}
