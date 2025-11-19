/*
 * Copyright (c) 2021-2022 Facebook, Inc. and its affiliates
 * Copyright (c) 2021-2022 NVIDIA Corporation
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

#include <cstddef>
#include <utility>

namespace sio::intrusive {
  template <auto Next>
  struct iterator;

  template <class Item, Item* Item::* Next>
  struct iterator<Next> {
    using difference_type = std::ptrdiff_t;
    Item* item_ = nullptr;

    Item& operator*() const noexcept {
      return *item_;
    }

    Item* operator->() const noexcept {
      return item_;
    }

    iterator& operator++() noexcept {
      item_ = item_->*Next;
      return *this;
    }

    iterator operator++(int) noexcept {
      iterator copy{*this};
      item_ = item_->*Next;
      return copy;
    }

    friend bool operator==(const iterator& lhs, const iterator& rhs) noexcept {
      return lhs.item_ == rhs.item_;
    }

    friend bool operator!=(const iterator& lhs, const iterator& rhs) noexcept {
      return lhs.item_ != rhs.item_;
    }
  };

  template <auto Next, auto Prev>
  class list;

  template <class Item, Item* Item::* Next, Item* Item::* Prev>
  class list<Next, Prev> {
   public:
    list() noexcept = default;

    list(list&& other) noexcept
      : head_(std::exchange(other.head_, nullptr))
      , tail_(std::exchange(other.tail_, nullptr)) {
    }

    list& operator=(list other) noexcept {
      std::swap(head_, other.head_);
      std::swap(tail_, other.tail_);
      return *this;
    }

    iterator<Next> begin() const noexcept {
      return iterator<Next>{head_};
    }

    iterator<Next> end() const noexcept {
      return iterator<Next>{};
    }

    [[nodiscard]] bool empty() const noexcept {
      return head_ == nullptr;
    }

    [[nodiscard]] Item* front() const noexcept {
      return head_;
    }

    [[nodiscard]] Item* back() const noexcept {
      return tail_;
    }

    [[nodiscard]] Item* front() noexcept {
      return head_;
    }

    [[nodiscard]] Item* back() noexcept {
      return tail_;
    }

    [[nodiscard]] Item* pop_front() noexcept {
      if (head_ == nullptr) {
        return nullptr;
      }
      Item* item = std::exchange(head_, head_->*Next);
      if (head_ == nullptr) {
        tail_ = nullptr;
      } else {
        head_->*Prev = nullptr;
      }
      item->*Next = nullptr;
      item->*Prev = nullptr;
      return item;
    }

    void push_front(Item* item) noexcept {
      item->*Prev = nullptr;
      item->*Next = head_;
      if (head_ != nullptr) {
        head_->*Prev = item;
      } else {
        tail_ = item;
      }
      head_ = item;
    }

    void push_back(Item* item) noexcept {
      item->*Next = nullptr;
      item->*Prev = tail_;
      if (tail_ == nullptr) {
        head_ = item;
      } else {
        tail_->*Next = item;
      }
      tail_ = item;
    }

    void erase(Item* item) noexcept {
      if (item == nullptr) {
        return;
      }
      if (item->*Prev == nullptr) {
        head_ = item->*Next;
      } else {
        item->*Prev->*Next = item->*Next;
      }
      if (item->*Next == nullptr) {
        tail_ = item->*Prev;
      } else {
        item->*Next->*Prev = item->*Prev;
      }
      item->*Next = nullptr;
      item->*Prev = nullptr;
    }

    void append(list other) noexcept {
      if (other.empty())
        return;
      auto* other_head = std::exchange(other.head_, nullptr);
      if (empty()) {
        head_ = other_head;
      } else {
        tail_->*Next = other_head;
      }
      tail_ = std::exchange(other.tail_, nullptr);
    }

    void prepend(list other) noexcept {
      if (other.empty())
        return;

      other.tail_->*Next = head_;
      head_ = other.head_;
      if (tail_ == nullptr) {
        tail_ = other.tail_;
      }

      other.tail_ = nullptr;
      other.head_ = nullptr;
    }

   private:
    Item* head_ = nullptr;
    Item* tail_ = nullptr;
  };
} // namespace sio::intrusive
