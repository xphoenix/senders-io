#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace sio::intrusive {
  namespace detail {
#if defined(__cpp_lib_int_pow2) && __cpp_lib_int_pow2 >= 202002L
    using std::bit_ceil;
#else
    inline std::size_t bit_ceil(std::size_t value) noexcept {
      if (value <= 1) {
        return 1;
      }
      std::size_t m = value - 1;
      m |= m >> 1;
      m |= m >> 2;
      m |= m >> 4;
      m |= m >> 8;
      m |= m >> 16;
#if INTPTR_MAX > INT32_MAX
      m |= m >> 32;
#endif
      return m + 1;
    }
#endif
  } // namespace detail

  template <class Node, class Key, Key Node::* KeyPtr, Node* Node::* Prev, Node* Node::* Left, Node* Node::* Right, class Compare = std::less<Key>>
  class heap {
   public:
    using node_type = Node;
    using key_type = Key;
    using compare_type = Compare;

    heap() = default;
    heap(const heap&) = delete;
    heap& operator=(const heap&) = delete;

    heap(heap&& other) noexcept
      : root_(std::exchange(other.root_, nullptr))
      , size_(std::exchange(other.size_, 0))
      , compare_(std::move(other.compare_)) {
    }

    heap& operator=(heap&& other) noexcept {
      if (this != &other) {
        root_ = std::exchange(other.root_, nullptr);
        size_ = std::exchange(other.size_, 0);
        compare_ = std::move(other.compare_);
      }
      return *this;
    }

    [[nodiscard]] bool empty() const noexcept {
      return size_ == 0;
    }

    [[nodiscard]] std::size_t size() const noexcept {
      return size_;
    }

    [[nodiscard]] Node* front() const noexcept {
      return root_;
    }

    void insert(Node* node) noexcept {
      node->*Prev = nullptr;
      node->*Left = nullptr;
      node->*Right = nullptr;
      if (root_ == nullptr) {
        root_ = node;
        size_ = 1;
        return;
      }
      Node* parent = iterate_to_parent_of_end();
      if (parent->*Left == nullptr) {
        parent->*Left = node;
      } else {
        parent->*Right = node;
      }
      node->*Prev = parent;
      size_ += 1;
      bottom_up_heapify(node);
    }

    void pop_front() noexcept {
      if (size_ == 0) {
        return;
      }
      if (size_ == 1) {
        root_ = nullptr;
        size_ = 0;
        return;
      }
      Node* leaf = iterate_to_back();
      detach_leaf(leaf);
      size_ -= 1;
      leaf->*Left = std::exchange(root_->*Left, nullptr);
      leaf->*Right = std::exchange(root_->*Right, nullptr);
      if (leaf->*Left) {
        leaf->*Left->*Prev = leaf;
      }
      if (leaf->*Right) {
        leaf->*Right->*Prev = leaf;
      }
      leaf->*Prev = nullptr;
      root_ = leaf;
      top_down_heapify(root_);
    }

    bool erase(Node* node) noexcept {
      if (!contains(node)) {
        return false;
      }
      if (size_ == 1) {
        root_ = nullptr;
        size_ = 0;
        node->*Prev = nullptr;
        node->*Left = nullptr;
        node->*Right = nullptr;
        return true;
      }
      Node* leaf = iterate_to_back();
      detach_leaf(leaf);
      size_ -= 1;
      if (node == leaf) {
        node->*Prev = nullptr;
        node->*Left = nullptr;
        node->*Right = nullptr;
        return true;
      }
      auto* parent = node->*Prev;
      auto* left = node->*Left;
      auto* right = node->*Right;
      replace_child(parent, node, leaf);
      leaf->*Prev = parent;
      leaf->*Left = left == leaf ? nullptr : left;
      leaf->*Right = right == leaf ? nullptr : right;
      if (leaf->*Left) {
        leaf->*Left->*Prev = leaf;
      }
      if (leaf->*Right) {
        leaf->*Right->*Prev = leaf;
      }
      node->*Prev = nullptr;
      node->*Left = nullptr;
      node->*Right = nullptr;
      if (leaf->*Prev && compare_(leaf->*KeyPtr, leaf->*Prev->*KeyPtr)) {
        bottom_up_heapify(leaf);
      } else {
        top_down_heapify(leaf);
      }
      return true;
    }

   private:
    Node* root_ = nullptr;
    std::size_t size_ = 0;
    Compare compare_{};

    bool contains(Node* node) const noexcept {
      return node != nullptr && (node == root_ || node->*Prev != nullptr);
    }

    void swap_parent_child(Node* parent, Node* child) noexcept {
      Node* grand = parent->*Prev;
      if (grand) {
        replace_child(grand, parent, child);
      } else {
        root_ = child;
      }
      child->*Prev = grand;
      if (parent->*Left == child) {
        parent->*Left = std::exchange(child->*Left, parent);
        std::swap(parent->*Right, child->*Right);
      } else {
        parent->*Right = std::exchange(child->*Right, parent);
        std::swap(parent->*Left, child->*Left);
      }
      if (parent->*Left) {
        parent->*Left->*Prev = parent;
      }
      if (parent->*Right) {
        parent->*Right->*Prev = parent;
      }
      if (child->*Left) {
        child->*Left->*Prev = child;
      }
      if (child->*Right) {
        child->*Right->*Prev = child;
      }
    }

    void bottom_up_heapify(Node* node) noexcept {
      while (node->*Prev && compare_(node->*KeyPtr, node->*Prev->*KeyPtr)) {
        swap_parent_child(node->*Prev, node);
      }
    }

    void top_down_heapify(Node* node) noexcept {
      while (node->*Left) {
        Node* child = node->*Left;
        if (node->*Right && compare_(node->*Right->*KeyPtr, child->*KeyPtr)) {
          child = node->*Right;
        }
        if (compare_(child->*KeyPtr, node->*KeyPtr)) {
          swap_parent_child(node, child);
        } else {
          break;
        }
      }
    }

    void detach_leaf(Node* leaf) noexcept {
      Node* parent = leaf->*Prev;
      if (!parent) {
        root_ = nullptr;
        return;
      }
      if (parent->*Left == leaf) {
        parent->*Left = nullptr;
      } else {
        parent->*Right = nullptr;
      }
      leaf->*Prev = nullptr;
    }

    void replace_child(Node* parent, Node* current, Node* replacement) noexcept {
      if (!parent) {
        root_ = replacement;
        return;
      }
      if (parent->*Left == current) {
        parent->*Left = replacement;
      } else {
        parent->*Right = replacement;
      }
    }

    Node* iterate_to_parent_of(std::size_t position) const noexcept {
      std::size_t index = detail::bit_ceil(position);
      if (index > position) {
        index /= 4;
      } else {
        index /= 2;
      }
      Node* node = root_;
      while (index > 1 && node) {
        if (position & index) {
          node = node->*Right;
        } else {
          node = node->*Left;
        }
        index /= 2;
      }
      return node;
    }

    Node* iterate_to_parent_of_end() const noexcept {
      return iterate_to_parent_of(size_ + 1);
    }

    Node* iterate_to_back() const noexcept {
      Node* parent = iterate_to_parent_of(size_);
      if (!parent) {
        return root_;
      }
      return parent->*Right ? parent->*Right : parent->*Left;
    }
  };
} // namespace sio::intrusive
