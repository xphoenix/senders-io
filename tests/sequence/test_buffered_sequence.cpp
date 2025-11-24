/*
 * Copyright (c) 2024 Emmett Zhang
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sio/async_resource.hpp"
#include "sio/buffer.hpp"
#include "sio/event_loop/concepts.hpp"
#include "sio/event_loop/file_handle.hpp"
#include "sio/mutable_buffer.hpp"
#include "sio/sequence/buffered_sequence.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "common/backend_matrix.hpp"

#include <catch2/catch_all.hpp>

#include <array>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <stdexec/execution.hpp>
#include <exec/when_any.hpp>

using namespace stdexec;

std::filesystem::path create_temp_path(std::string_view tag) {
  const auto temp_dir = std::filesystem::temp_directory_path();
  std::string filename = "senders-io_buffered_sequence_";
  filename.append(tag);
  filename.append("_XXXXXX");

  auto templated_path = (temp_dir / filename).string();
  std::vector<char> path_template{templated_path.begin(), templated_path.end()};
  path_template.push_back('\0');

  int fd = ::mkstemp(path_template.data());
  REQUIRE(fd != -1);
  ::close(fd);
  return std::filesystem::path{path_template.data()};
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in{path, std::ios::binary};
  REQUIRE(in.is_open());
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

void write_to_file(const std::filesystem::path& path, std::string_view content) {
  std::ofstream out{path, std::ios::binary | std::ios::trunc};
  REQUIRE(out.is_open());
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  REQUIRE(out.good());
}

TEMPLATE_LIST_TEST_CASE(
  "buffered_sequence - with read_factory and single buffer",
  "[sio][buffered_sequence]",
  SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  CAPTURE(Backend::name);
  using loop_type = typename Backend::loop_type;
  if constexpr (!sio::event_loop::file_loop<loop_type>) {
    SKIP("Backend does not support file handles");
    return;
  }
  loop_type loop{};
  auto path = create_temp_path("with_read_factory_single");

  constexpr auto content = std::string_view{"hello world"};
  write_to_file(path, content);
  auto storage = std::string(content.size(), '0');
  sio::event_loop::file file{
    &loop, path, sio::async::mode::read, sio::async::creation::open_existing};
  auto sender = sio::async::use_resources(
    [&](auto handle) {
      auto factory = typename Backend::read_factory{&loop.native_context(), handle.native_handle()};
      auto buffer = sio::buffer(storage);
      auto buffered_read_some = sio::buffered_sequence(factory, buffer);
      return sio::ignore_all(std::move(buffered_read_some));
    },
    file);

  stdexec::sync_wait(exec::when_any(std::move(sender), loop.run()));

  CHECK(storage == content);
  std::filesystem::remove(path);
}

TEMPLATE_LIST_TEST_CASE(
  "buffered_sequence - with read_factory and multiple buffers",
  "[sio][buffered_sequence]",
  SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  CAPTURE(Backend::name);
  using loop_type = typename Backend::loop_type;
  if constexpr (!sio::event_loop::file_loop<loop_type>) {
    SKIP("Backend does not support file handles");
    return;
  }
  loop_type loop{};
  auto path = create_temp_path("with_read_factory_multiple");

  constexpr auto content = std::string_view{"hello world"};
  write_to_file(path, content);
  auto storage1 = std::string(6, '0');
  auto storage2 = std::string(5, '0');
  auto array = std::array<sio::mutable_buffer, 2>{sio::buffer(storage1), sio::buffer(storage2)};
  auto buffers = std::span< sio::mutable_buffer>{array};
  sio::event_loop::file file{
    &loop, path, sio::async::mode::read, sio::async::creation::open_existing};
  auto sender = sio::async::use_resources(
    [&](auto handle) {
      auto factory = typename Backend::read_factory{&loop.native_context(), handle.native_handle()};
      auto buffered_read_some = sio::buffered_sequence(factory, buffers);
      return sio::ignore_all(std::move(buffered_read_some));
    },
    file);

  stdexec::sync_wait(exec::when_any(std::move(sender), loop.run()));

  CHECK(storage1 == "hello ");
  CHECK(storage2 == "world");
  std::filesystem::remove(path);
}

TEMPLATE_LIST_TEST_CASE(
  "buffered_sequence - with write_factory and single buffer",
  "[sio][buffered_sequence]",
  SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  CAPTURE(Backend::name);
  using loop_type = typename Backend::loop_type;
  if constexpr (!sio::event_loop::file_loop<loop_type>) {
    SKIP("Backend does not support file handles");
    return;
  }
  loop_type loop{};
  auto path = create_temp_path("with_write_factory");

  const auto content = std::string{"hello world"};
  sio::event_loop::file file{
    &loop, path, sio::async::mode::write, sio::async::creation::truncate_existing};
  auto sender = sio::async::use_resources(
    [&](auto handle) {
      auto factory =
        typename Backend::write_factory{&loop.native_context(), handle.native_handle()};
      auto buffer = sio::buffer(content);
      auto buffered_write_some = sio::buffered_sequence(factory, buffer);
      return sio::ignore_all(std::move(buffered_write_some));
    },
    file);

  stdexec::sync_wait(exec::when_any(std::move(sender), loop.run()));

  CHECK(read_file(path) == content);
  std::filesystem::remove(path);
}
