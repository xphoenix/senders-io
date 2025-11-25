/*
 * Copyright (c) 2024 Maikel Nadolski
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
#pragma once

#include <sys/socket.h>
#include <sys/un.h>

#include <algorithm>
#include <cstring>
#include <string_view>
#include <cstddef>

namespace sio::local {
  class endpoint {
   public:
    using native_handle_type = ::sockaddr_un;

    explicit endpoint(std::string_view path)
      : addr_{.sun_family = AF_LOCAL} {
      const bool filesystem = path.empty() || path.front() != '\0';
      const std::size_t max_len = filesystem ? sizeof(addr_.sun_path) - 1 : sizeof(addr_.sun_path);
      const std::size_t len = std::min(path.size(), max_len);
      std::memcpy(addr_.sun_path, path.data(), len);
      if (filesystem) {
        addr_.sun_path[len] = '\0';
        size_ = static_cast<socklen_t>(offsetof(::sockaddr_un, sun_path) + len + 1);
      } else {
        size_ = static_cast<socklen_t>(offsetof(::sockaddr_un, sun_path) + len);
      }
      path_length_ = static_cast<socklen_t>(len);
      is_filesystem_ = filesystem;
    }

    const ::sockaddr_un* data() const noexcept {
      return &addr_;
    }

    ::size_t size() const noexcept {
      return size_;
    }

    std::string_view path() const noexcept {
      return std::string_view{addr_.sun_path, static_cast<std::size_t>(path_length_)};
    }

    bool is_filesystem() const noexcept {
      return is_filesystem_;
    }

    int family() const noexcept {
      return AF_LOCAL;
    }

   private:
    ::sockaddr_un addr_{.sun_family = AF_LOCAL};
    socklen_t size_{static_cast<socklen_t>(sizeof(::sockaddr_un))};
    socklen_t path_length_{0};
    bool is_filesystem_{true};
  };
}
