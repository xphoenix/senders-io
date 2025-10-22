#include <sio/buffer.hpp>
#include <sio/event_loop/file_handle.hpp>
#include <sio/event_loop/stdexec/backend.hpp>
#include <sio/read_batched.hpp>

#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <array>
#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
  using backend = sio::event_loop::stdexec_backend::backend;

  if (argc < 2) {
    std::cerr << "usage: batched_reads <file>" << std::endl;
    return 1;
  }

  std::filesystem::path path = argv[1];

  backend loop{};
  sio::event_loop::seekable_file<backend> file{
    &loop,
    path,
    sio::async::mode::read,
    sio::async::creation::open_existing};

  std::vector<std::array<std::byte, 64>> storage(3);
  std::vector<sio::mutable_buffer> buffers;
  buffers.reserve(storage.size());
  for (auto& chunk : storage) {
    buffers.emplace_back(chunk.data(), chunk.size());
  }

  using handle_type = sio::event_loop::seekable_file_handle<backend>;
  using offset_type = sio::async::offset_type_of_t<handle_type>;
  std::vector<offset_type> offsets = {0, 64, 128};

  auto read_sender = stdexec::let_value(
    file.open(),
    [&](auto handle) {
      return sio::async::read_batched(handle, buffers, offsets)
           | exec::finally(handle.close());
    });

  stdexec::sync_wait(exec::when_any(std::move(read_sender), loop.run()));

  for (std::size_t i = 0; i < storage.size(); ++i) {
    auto* bytes = reinterpret_cast<const char*>(storage[i].data());
    std::cout << "chunk " << i << ": ";
    std::cout.write(bytes, storage[i].size());
    std::cout << '\n';
  }
}
