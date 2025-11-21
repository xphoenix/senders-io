#define private public
#define protected public
#include "sio/intrusive/heap.hpp"
#undef private
#undef protected

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <numeric>
#include <ranges>
#include <random>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
  struct heap_node {
    int key = 0;
    heap_node* prev = nullptr;
    heap_node* left = nullptr;
    heap_node* right = nullptr;
  };

  struct dual_hook_node {
    int key = 0;
    dual_hook_node* min_prev = nullptr;
    dual_hook_node* min_left = nullptr;
    dual_hook_node* min_right = nullptr;
    dual_hook_node* max_prev = nullptr;
    dual_hook_node* max_left = nullptr;
    dual_hook_node* max_right = nullptr;
  };

  using min_heap = sio::intrusive::heap<
    heap_node,
    int,
    &heap_node::key,
    &heap_node::prev,
    &heap_node::left,
    &heap_node::right>;

  using max_heap = sio::intrusive::heap<
    heap_node,
    int,
    &heap_node::key,
    &heap_node::prev,
    &heap_node::left,
    &heap_node::right,
    std::greater<int>>;

  using dual_min_heap = sio::intrusive::heap<
    dual_hook_node,
    int,
    &dual_hook_node::key,
    &dual_hook_node::min_prev,
    &dual_hook_node::min_left,
    &dual_hook_node::min_right>;

  using dual_max_heap = sio::intrusive::heap<
    dual_hook_node,
    int,
    &dual_hook_node::key,
    &dual_hook_node::max_prev,
    &dual_hook_node::max_left,
    &dual_hook_node::max_right,
    std::greater<int>>;

  template <class Range>
  auto make_heap_nodes(const Range& values) {
    std::vector<std::unique_ptr<heap_node>> nodes;
    nodes.reserve(std::size(values));
    for (int value: values) {
      auto node = std::make_unique<heap_node>();
      node->key = value;
      nodes.push_back(std::move(node));
    }
    return nodes;
  }

  template <class Range>
  auto make_dual_nodes(const Range& values) {
    std::vector<std::unique_ptr<dual_hook_node>> nodes;
    nodes.reserve(std::size(values));
    for (int value: values) {
      auto node = std::make_unique<dual_hook_node>();
      node->key = value;
      nodes.push_back(std::move(node));
    }
    return nodes;
  }

  template <class Node, Node* Node::* Prev, Node* Node::* Left, Node* Node::* Right>
  bool hooks_are_detached(const Node& node) noexcept {
    return node.*Prev == nullptr && node.*Left == nullptr && node.*Right == nullptr;
  }

  template <class Node, Node* Node::* Left, Node* Node::* Right, class Compare>
  bool is_heapified(const Node* node, Compare comp) noexcept {
    if (!node) {
      return true;
    }
    const Node* left = node->*Left;
    const Node* right = node->*Right;
    if (left && comp(left->key, node->key)) {
      return false;
    }
    if (right && comp(right->key, node->key)) {
      return false;
    }
    return is_heapified<Node, Left, Right>(left, comp) && is_heapified<Node, Left, Right>(right, comp);
  }

  template <class Heap>
  std::vector<int> drain_heap(Heap& heap) {
    std::vector<int> values;
    values.reserve(heap.size());
    while (!heap.empty()) {
      auto* node = heap.front();
      REQUIRE(node != nullptr);
      values.push_back(node->key);
      heap.pop_front();
    }
    return values;
  }

  template <class Node>
  void remove_node(std::vector<Node*>& nodes, Node* value) {
    nodes.erase(std::remove(nodes.begin(), nodes.end(), value), nodes.end());
  }

  struct throwing_compare {
    static inline bool throw_next = false;

    bool operator()(int lhs, int rhs) const {
      if (throw_next) {
        throw std::runtime_error{"comparator failure"};
      }
      return lhs < rhs;
    }
  };

  using throwing_heap = sio::intrusive::heap<
    heap_node,
    int,
    &heap_node::key,
    &heap_node::prev,
    &heap_node::left,
    &heap_node::right,
    throwing_compare>;

  template <std::size_t N>
  std::array<heap_node, N> make_array_nodes(const std::array<int, N>& values) {
    std::array<heap_node, N> nodes{};
    for (std::size_t i = 0; i < N; ++i) {
      nodes[i].key = values[i];
    }
    return nodes;
  }

  template <std::size_t N>
  std::array<heap_node, N> make_array_nodes(std::initializer_list<int> values) {
    REQUIRE(values.size() == N);
    std::array<int, N> tmp{};
    std::copy(values.begin(), values.end(), tmp.begin());
    return make_array_nodes(tmp);
  }

  template <class Node, Node* Node::* Left, Node* Node::* Right>
  std::size_t count_nodes(const Node* node) {
    if (!node) {
      return 0;
    }
    return 1 + count_nodes<Node, Left, Right>(node->*Left) + count_nodes<Node, Left, Right>(node->*Right);
  }

  template <class Heap, class Nodes>
  void exercise_heap_core(Heap& heap, Nodes& nodes) {
    REQUIRE(nodes.size() >= 4);
    heap.pop_front();
    heap.insert(&nodes[0]);
    heap.pop_front();
    heap.insert(&nodes[0]);
    REQUIRE(heap.erase(&nodes[0]));
    for (auto& node: nodes) {
      heap.insert(&node);
    }
    CHECK_FALSE(heap.erase(nullptr));
    CHECK(heap.erase(&nodes.back()));
    CHECK(heap.erase(&nodes[1]));
    CHECK(heap.erase(&nodes[0]));
    heap.pop_front();
    while (!heap.empty()) {
      heap.pop_front();
    }
  }
} // namespace

TEST_CASE("intrusive::heap specification compliance", "[intrusive][heap]") {
  SECTION("1. Basic functionality / happy path") {
    SECTION("construction & empty-state") {
      min_heap min;
      max_heap max;
      CHECK(min.empty());
      CHECK(min.size() == 0);
      CHECK(min.front() == nullptr);
      min.pop_front();
      CHECK(max.empty());
      CHECK(max.size() == 0);
      CHECK(max.front() == nullptr);
      max.pop_front();
      CHECK(sio::intrusive::detail::bit_ceil(std::size_t{1}) == 1);
      CHECK(sio::intrusive::detail::bit_ceil(std::size_t{2}) == 2);
    }

    SECTION("construct from varied orders") {
      const std::vector<std::vector<int>> datasets{
        {3, 1, 4, 0, 2},
        {0, 1, 2, 3, 4, 5},
        {9, 8, 7, 6, 5},
        {5, 5, 5, 5}};
      for (const auto& data: datasets) {
        min_heap min;
        auto min_nodes = make_heap_nodes(data);
        for (auto& node: min_nodes) {
          min.insert(node.get());
        }
        const auto asc = drain_heap(min);
        CHECK(std::is_sorted(asc.begin(), asc.end()));

        max_heap max;
        auto max_nodes = make_heap_nodes(data);
        for (auto& node: max_nodes) {
          max.insert(node.get());
        }
        const auto desc = drain_heap(max);
        CHECK(std::is_sorted(desc.begin(), desc.end(), std::greater<int>{}));
      }
    }

    SECTION("single-element behavior") {
      heap_node node{.key = 42};
      min_heap heap;
      heap.insert(&node);
      REQUIRE(heap.size() == 1);
      REQUIRE(heap.front() == &node);
      heap.pop_front();
      CHECK(heap.empty());

      max_heap max;
      max.insert(&node);
      REQUIRE(max.size() == 1);
      REQUIRE(max.front() == &node);
      max.pop_front();
      CHECK(max.empty());
    }

    SECTION("two-element orderings") {
      std::array<heap_node, 2> nodes{heap_node{.key = 7}, heap_node{.key = 2}};
      min_heap min;
      min.insert(&nodes[1]);
      min.insert(&nodes[0]);
      REQUIRE(min.front() == &nodes[1]);
      auto ascending = drain_heap(min);
      CHECK(ascending == std::vector<int>{2, 7});

      max_heap max;
      max.insert(&nodes[0]);
      max.insert(&nodes[1]);
      REQUIRE(max.front() == &nodes[0]);
      auto descending = drain_heap(max);
      CHECK(descending == std::vector<int>{7, 2});
    }

    SECTION("general min/max correctness for fixed set") {
      std::array<heap_node, 5> nodes = make_array_nodes<5>({1, 5, 3, 9, 2});
      min_heap min;
      for (auto& node: nodes) {
        min.insert(&node);
      }
      REQUIRE(min.front()->key == 1);
      auto asc = drain_heap(min);
      CHECK(asc == std::vector<int>({1, 2, 3, 5, 9}));

      std::array<heap_node, 5> nodes_copy = make_array_nodes<5>({1, 5, 3, 9, 2});
      max_heap max;
      for (auto& node: nodes_copy) {
        max.insert(&node);
      }
      REQUIRE(max.front()->key == 9);
      auto desc = drain_heap(max);
      CHECK(desc == std::vector<int>({9, 5, 3, 2, 1}));

      std::array<dual_hook_node, 5> dual_nodes{};
      dual_min_heap min_dual;
      dual_max_heap max_dual;
      const std::array<int, 5> values{1, 5, 3, 9, 2};
      for (std::size_t i = 0; i < dual_nodes.size(); ++i) {
        dual_nodes[i].key = values[i];
        min_dual.insert(&dual_nodes[i]);
        max_dual.insert(&dual_nodes[i]);
      }

      std::vector<int> alternating;
      while (!min_dual.empty() && !max_dual.empty()) {
        auto* min_node = min_dual.front();
        alternating.push_back(min_node->key);
        REQUIRE(max_dual.erase(min_node));
        min_dual.pop_front();
        CHECK(is_heapified<dual_hook_node, &dual_hook_node::min_left, &dual_hook_node::min_right>(min_dual.front(), std::less<int>{}));

        if (max_dual.empty()) {
          break;
        }
        auto* max_node = max_dual.front();
        alternating.push_back(max_node->key);
        REQUIRE(min_dual.erase(max_node));
        max_dual.pop_front();
        CHECK(is_heapified<dual_hook_node, &dual_hook_node::max_left, &dual_hook_node::max_right>(max_dual.front(), std::greater<int>{}));
      }
      std::vector<int> sorted = alternating;
      std::sort(sorted.begin(), sorted.end());
      std::vector<int> expected(values.begin(), values.end());
      std::sort(expected.begin(), expected.end());
      CHECK(sorted == expected);
    }
  }

  SECTION("2. Size-related edge cases") {
    SECTION("empty to non-empty cycles") {
      std::array<heap_node, 4> nodes = make_array_nodes<4>({4, 1, 7, 3});
      min_heap heap;
      for (int cycle = 0; cycle < 3; ++cycle) {
        for (auto& node: nodes) {
          node.key += cycle;
          heap.insert(&node);
        }
        REQUIRE(heap.size() == nodes.size());
        auto drained = drain_heap(heap);
        CHECK(drained.size() == nodes.size());
        CHECK(std::is_sorted(drained.begin(), drained.end()));
      }

      max_heap max;
      for (int cycle = 0; cycle < 3; ++cycle) {
        for (auto& node: nodes) {
          node.key = cycle * 10 + node.key;
          max.insert(&node);
        }
        REQUIRE(max.size() == nodes.size());
        auto drained = drain_heap(max);
        CHECK(std::is_sorted(drained.begin(), drained.end(), std::greater<int>{}));
      }
    }

    SECTION("boundary sizes and power-of-two patterns") {
      const std::array<std::size_t, 10> target_sizes{0, 1, 2, 3, 4, 5, 6, 7, 8, 16};
      for (std::size_t target_size: target_sizes) {
        min_heap heap;
        std::vector<std::unique_ptr<heap_node>> storage;
        storage.reserve(target_size);
        for (std::size_t i = 0; i < target_size; ++i) {
          auto node = std::make_unique<heap_node>();
          node->key = static_cast<int>(target_size - i);
          heap.insert(node.get());
          storage.push_back(std::move(node));
        }
        CHECK(heap.size() == target_size);
        CHECK(is_heapified<heap_node, &heap_node::left, &heap_node::right>(heap.front(), std::less<int>{}));
        auto drained = drain_heap(heap);
        CHECK(drained.size() == target_size);
      }
    }

    SECTION("large N for index computations") {
      constexpr int N = 5000;
      std::vector<int> values(N);
      std::iota(values.begin(), values.end(), 0);
      std::mt19937 rng{1337};
      std::shuffle(values.begin(), values.end(), rng);
      auto nodes = make_heap_nodes(values);
      min_heap heap;
      for (auto& node: nodes) {
        heap.insert(node.get());
      }
      auto drained = drain_heap(heap);
      CHECK(drained.size() == values.size());
      CHECK(std::is_sorted(drained.begin(), drained.end()));
    }
  }

  SECTION("3. Duplicate keys & stability semantics") {
    SECTION("all equal keys remain reachable") {
      std::array<heap_node, 4> nodes = make_array_nodes<4>({5, 5, 5, 5});
      min_heap heap;
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      for (std::size_t remaining = nodes.size(); remaining > 0; --remaining) {
        REQUIRE(heap.front()->key == 5);
        heap.pop_front();
        CHECK(heap.size() == remaining - 1);
      }
      CHECK(heap.empty());
    }

    SECTION("mixed duplicates expose extrema") {
      std::array<dual_hook_node, 5> nodes{};
      const std::array<int, 5> values{3, 5, 5, 7, 5};
      dual_min_heap min;
      dual_max_heap max;
      for (std::size_t i = 0; i < values.size(); ++i) {
        nodes[i].key = values[i];
        min.insert(&nodes[i]);
        max.insert(&nodes[i]);
      }
      REQUIRE(min.front()->key == 3);
      REQUIRE(max.front()->key == 7);

      auto* min_node = min.front();
      max.erase(min_node);
      min.pop_front();
      auto* max_node = max.front();
      min.erase(max_node);
      max.pop_front();
      while (!min.empty()) {
        REQUIRE(min.front()->key == 5);
        auto* ptr = min.front();
        REQUIRE(max.erase(ptr));
        min.pop_front();
        if (!max.empty()) {
          CHECK(max.front()->key == 5);
        }
      }
      CHECK(max.empty());
    }
  }

  SECTION("4. Arbitrary erase / update operations") {
    auto nodes = make_array_nodes<7>({7, 3, 5, 1, 4, 6, 8});

    SECTION("erase leaf, root, and middle nodes") {
      min_heap heap;
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      REQUIRE(heap.erase(&nodes.back()));
      CHECK(hooks_are_detached<heap_node, &heap_node::prev, &heap_node::left, &heap_node::right>(nodes.back()));

      REQUIRE(heap.erase(&nodes[1]));
      CHECK(is_heapified<heap_node, &heap_node::left, &heap_node::right>(heap.front(), std::less<int>{}));

      REQUIRE(heap.erase(heap.front()));
      CHECK(is_heapified<heap_node, &heap_node::left, &heap_node::right>(heap.front(), std::less<int>{}));
    }

    SECTION("erase parents of last leaf triggers special rewiring") {
      min_heap heap;
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      REQUIRE(heap.erase(&nodes[1]));
      CHECK(nodes[1].left == nullptr);
      CHECK(nodes[1].right == nullptr);
      CHECK(nodes[1].prev == nullptr);
    }

    SECTION("random erase sequences keep structure valid") {
      min_heap heap;
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      std::vector<int> order{0, 3, 6, 2, 5, 1, 4};
      for (int idx: order) {
        REQUIRE(heap.erase(&nodes[static_cast<std::size_t>(idx)]));
        CHECK(is_heapified<heap_node, &heap_node::left, &heap_node::right>(heap.front(), std::less<int>{}));
      }
      CHECK(heap.empty());
    }

    SECTION("erase everything through shuffle order") {
      min_heap heap;
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      std::vector<std::size_t> erase_order(nodes.size());
      std::iota(erase_order.begin(), erase_order.end(), 0);
      std::mt19937 rng{1337};
      std::shuffle(erase_order.begin(), erase_order.end(), rng);
      for (auto idx: erase_order) {
        REQUIRE(heap.erase(&nodes[idx]));
        CHECK(hooks_are_detached<heap_node, &heap_node::prev, &heap_node::left, &heap_node::right>(nodes[idx]));
      }
      CHECK(heap.empty());
    }

    SECTION("simulated key updates via erase + insert") {
      min_heap heap;
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      heap_node* largest = nullptr;
      for (auto& node: nodes) {
        if (!largest || node.key > largest->key) {
          largest = &node;
        }
      }
      REQUIRE(heap.erase(largest));
      largest->key = -5;
      heap.insert(largest);
      CHECK(heap.front() == largest);

      heap_node* smallest = heap.front();
      REQUIRE(heap.erase(smallest));
      smallest->key = 50;
      heap.insert(smallest);
      CHECK(is_heapified<heap_node, &heap_node::left, &heap_node::right>(heap.front(), std::less<int>{}));
    }

    SECTION("manual wiring covers equality and reheap branches") {
      min_heap heap;
      std::array<heap_node, 5> manual{};
      auto reset = [&] {
        heap.root_ = nullptr;
        heap.size_ = 0;
        for (auto& node: manual) {
          node.prev = nullptr;
          node.left = nullptr;
          node.right = nullptr;
        }
      };

      reset();
      heap.root_ = &manual[0];
      heap.size_ = 4;
      manual[0].left = &manual[1];
      manual[1].prev = &manual[0];
      manual[0].right = &manual[2];
      manual[2].prev = &manual[0];
      manual[2].left = &manual[3];
      manual[3].prev = &manual[2];
      manual[1].left = &manual[3];
      auto* leaf_first = heap.iterate_to_back();
      REQUIRE(leaf_first == &manual[3]);
      REQUIRE(heap.erase(&manual[1]));

      reset();
      heap.root_ = &manual[0];
      heap.size_ = 5;
      manual[0].left = &manual[1];
      manual[1].prev = &manual[0];
      manual[0].right = &manual[2];
      manual[2].prev = &manual[0];
      manual[2].right = &manual[4];
      manual[4].prev = &manual[2];
      manual[1].right = &manual[4];
      manual[2].left = &manual[3];
      manual[3].prev = &manual[2];
      manual[4].key = 5;
      auto* leaf_second = heap.iterate_to_back();
      REQUIRE(leaf_second == &manual[4]);
      REQUIRE(heap.erase(&manual[1]));

      reset();
      heap.root_ = &manual[0];
      heap.size_ = 3;
      manual[0].key = 50;
      manual[1].key = 40;
      manual[2].key = 10;
      manual[0].left = &manual[1];
      manual[1].prev = &manual[0];
      manual[0].right = &manual[2];
      manual[2].prev = &manual[0];
      REQUIRE(heap.erase(&manual[1]));
    }
  }

  SECTION("5. Intrusive-specific edge cases") {
    SECTION("hooks cleared after erase and reinsert works") {
      heap_node node{.key = 10};
      min_heap heap;
      heap.insert(&node);
      REQUIRE(heap.erase(&node));
      CHECK(hooks_are_detached<heap_node, &heap_node::prev, &heap_node::left, &heap_node::right>(node));
      heap.insert(&node);
      CHECK(heap.size() == 1);
    }

    SECTION("destroy heap with live nodes leaves them reusable") {
      std::array<heap_node, 3> nodes = make_array_nodes<3>({1, 2, 3});
      {
        min_heap heap;
        for (auto& node: nodes) {
          heap.insert(&node);
        }
      }
      min_heap heap;
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      CHECK(heap.size() == nodes.size());
    }

    SECTION("node reuse with new key") {
      heap_node node{.key = 5};
      min_heap heap;
      heap.insert(&node);
      heap.pop_front();
      node.key = 1;
      heap.insert(&node);
      CHECK(heap.front()->key == 1);
    }

    SECTION("multiple hooks keep heaps independent") {
      std::array<dual_hook_node, 4> nodes{};
      const std::array<int, 4> values{9, 1, 5, 2};
      dual_min_heap min;
      dual_max_heap max;
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        nodes[i].key = values[i];
        min.insert(&nodes[i]);
        max.insert(&nodes[i]);
      }
      REQUIRE(min.front()->key == 1);
      REQUIRE(max.front()->key == 9);

      auto* node = min.front();
      REQUIRE(max.erase(node));
      min.pop_front();
      CHECK(max.front()->key == 9);

      auto* max_node = max.front();
      REQUIRE(min.erase(max_node));
      max.pop_front();
      CHECK(min.front()->key == 2);
    }
  }

  SECTION("6. API surface: constructors, assignment, swap") {
    SECTION("default and move constructors") {
      min_heap heap;
      std::array<heap_node, 3> nodes = make_array_nodes<3>({4, 2, 6});
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      min_heap moved{std::move(heap)};
      CHECK(heap.empty());
      CHECK(moved.size() == nodes.size());

      min_heap assigned;
      assigned = std::move(moved);
      CHECK(moved.empty());
      CHECK(assigned.size() == nodes.size());

      assigned = std::move(assigned);
      CHECK(assigned.size() == nodes.size());
    }

    SECTION("swap scenarios") {
      std::array<heap_node, 4> nodes = make_array_nodes<4>({8, 3, 7, 1});
      min_heap a;
      min_heap b;
      a.insert(&nodes[0]);
      a.insert(&nodes[1]);
      b.insert(&nodes[2]);
      b.insert(&nodes[3]);
      std::swap(a, b);
      CHECK(a.front()->key == 1);
      CHECK(b.front()->key == 3);

      min_heap empty;
      std::swap(a, empty);
      CHECK(a.empty());
      CHECK(empty.size() == 2);
    }

    SECTION("copy and range construction are disabled") {
      STATIC_REQUIRE(!std::is_copy_constructible_v<min_heap>);
      STATIC_REQUIRE(!std::is_copy_assignable_v<min_heap>);
    }
  }

  SECTION("7. Iterators / traversal interface") {
    SECTION("no iterator exposure") {
      STATIC_REQUIRE(!std::ranges::range<min_heap>);
      min_heap heap;
      std::array<heap_node, 3> nodes = make_array_nodes<3>({3, 1, 2});
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      std::size_t counted = count_nodes<heap_node, &heap_node::left, &heap_node::right>(heap.front());
      CHECK(counted == heap.size());
    }
  }

  SECTION("8. Comparator / key-extractor edge cases") {
    SECTION("custom comparator orderings") {
      std::array<heap_node, 5> nodes = make_array_nodes<5>({11, 4, 7, 2, 9});
      max_heap heap;
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      CHECK(heap.front()->key == 11);
      auto drained = drain_heap(heap);
      CHECK(std::is_sorted(drained.begin(), drained.end(), std::greater<int>{}));
    }

    SECTION("non-strict comparator behavior via duplicates") {
      std::array<heap_node, 5> nodes = make_array_nodes<5>({5, 5, 6, 5, 6});
      min_heap heap;
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      std::vector<int> drained;
      while (!heap.empty()) {
        drained.push_back(heap.front()->key);
        heap.pop_front();
      }
      CHECK(std::is_sorted(drained.begin(), drained.end()));
      CHECK(drained.front() == 5);
      CHECK(drained.back() == 6);
    }

    SECTION("key extractor field updates require reheapify") {
      std::array<heap_node, 3> nodes = make_array_nodes<3>({8, 3, 10});
      min_heap heap;
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      auto* node = heap.front();
      REQUIRE(node == &nodes[1]);
      REQUIRE(heap.erase(node));
      node->key = 0;
      heap.insert(node);
      CHECK(heap.front()->key == 0);
    }
  }

  SECTION("9. Exception safety tests") {
    SECTION("operations are noexcept and comparator hooks can throw independently") {
      STATIC_REQUIRE(noexcept(std::declval<min_heap&>().insert(static_cast<heap_node*>(nullptr))));
      STATIC_REQUIRE(noexcept(std::declval<min_heap&>().pop_front()));
      STATIC_REQUIRE(noexcept(std::declval<min_heap&>().erase(static_cast<heap_node*>(nullptr))));

      std::array<heap_node, 4> nodes = make_array_nodes<4>({7, 2, 9, 1});
      throwing_heap heap;
      exercise_heap_core(heap, nodes);
      heap.insert(&nodes[0]);
      heap.insert(&nodes[1]);
      throwing_compare::throw_next = true;
      CHECK_THROWS_AS(heap.compare_(nodes[0].key, nodes[1].key), std::runtime_error);
      throwing_compare::throw_next = false;
      while (!heap.empty()) {
        heap.pop_front();
      }
      CHECK(heap.empty());
    }
  }

  SECTION("10. Stress / fuzz tests") {
    constexpr int operations = 1500;
    dual_min_heap min;
    dual_max_heap max;
    std::vector<std::unique_ptr<dual_hook_node>> storage;
    std::vector<dual_hook_node*> active;
    std::multiset<int> reference;
    std::mt19937 rng{1337};
    std::uniform_int_distribution<int> key_dist{-1000, 1000};
    std::uniform_int_distribution<int> op_dist{0, 4};

    auto insert_node = [&]() {
      auto node = std::make_unique<dual_hook_node>();
      node->key = key_dist(rng);
      auto* raw = node.get();
      storage.push_back(std::move(node));
      min.insert(raw);
      max.insert(raw);
      active.push_back(raw);
      reference.insert(raw->key);
    };

    for (int step = 0; step < operations; ++step) {
      int op = op_dist(rng);
      if (active.empty()) {
        op = 0;
      }
      switch (op) {
        case 0: insert_node(); break;
        case 1: {
          auto* node = min.front();
          REQUIRE(node != nullptr);
          auto it = reference.find(node->key);
          REQUIRE(it != reference.end());
          reference.erase(it);
          REQUIRE(max.erase(node));
          min.pop_front();
          remove_node(active, node);
          break;
        }
        case 2: {
          auto* node = max.front();
          REQUIRE(node != nullptr);
          auto it = reference.find(node->key);
          REQUIRE(it != reference.end());
          reference.erase(it);
          REQUIRE(min.erase(node));
          max.pop_front();
          remove_node(active, node);
          break;
        }
        case 3: {
          const std::size_t idx = static_cast<std::size_t>(rng() % active.size());
          auto* node = active[idx];
          auto it = reference.find(node->key);
          REQUIRE(it != reference.end());
          reference.erase(it);
          REQUIRE(min.erase(node));
          REQUIRE(max.erase(node));
          remove_node(active, node);
          break;
        }
        case 4: {
          const std::size_t idx = static_cast<std::size_t>(rng() % active.size());
          auto* node = active[idx];
          auto it = reference.find(node->key);
          REQUIRE(it != reference.end());
          reference.erase(it);
          REQUIRE(min.erase(node));
          REQUIRE(max.erase(node));
          node->key += key_dist(rng) % 5;
          min.insert(node);
          max.insert(node);
          reference.insert(node->key);
          break;
        }
      }

      CHECK(min.size() == reference.size());
      CHECK(max.size() == reference.size());
      if (reference.empty()) {
        CHECK(min.front() == nullptr);
        CHECK(max.front() == nullptr);
      } else {
        CHECK(min.front()->key == *reference.begin());
        CHECK(max.front()->key == *reference.rbegin());
      }
    }
  }

  SECTION("11. Misuse / defensive programming") {
    SECTION("erase rejects nodes not in the heap") {
      heap_node phantom{.key = 1};
      min_heap heap;
      CHECK_FALSE(heap.erase(&phantom));
      CHECK_FALSE(heap.erase(nullptr));
    }

    SECTION("contains distinguishes root vs detached nodes") {
      std::array<heap_node, 2> nodes = make_array_nodes<2>({2, 1});
      min_heap heap;
      for (auto& node: nodes) {
        heap.insert(&node);
      }
      CHECK(heap.contains(heap.front()));
      CHECK(heap.contains(&nodes[1]));
      heap.erase(&nodes[1]);
      CHECK_FALSE(heap.contains(&nodes[1]));
    }

    SECTION("direct helper coverage for defensive paths") {
      min_heap heap;
      heap_node node{.key = 1};
      heap.root_ = &node;
      heap.size_ = 1;
      heap.detach_leaf(&node);
      CHECK(heap.root_ == nullptr);

      heap.root_ = nullptr;
      heap.size_ = 5;
      CHECK(heap.iterate_to_parent_of(5) == nullptr);
      CHECK(heap.iterate_to_back() == nullptr);

      heap.root_ = &node;
      heap.size_ = 2;
      node.left = nullptr;
      node.right = nullptr;
      CHECK(heap.iterate_to_parent_of_end() == &node);
    }

    SECTION("bottom-up swap covers child pointer branches") {
      min_heap heap;
      heap_node parent_left{.key = 10};
      heap_node left_child{.key = 1};
      parent_left.left = &left_child;
      left_child.prev = &parent_left;
      heap.root_ = &parent_left;
      heap.size_ = 2;
      heap.bottom_up_heapify(&left_child);
      CHECK(heap.root_ == &left_child);

      heap_node parent_right{.key = 10};
      heap_node right_child{.key = 1};
      parent_right.right = &right_child;
      right_child.prev = &parent_right;
      heap.root_ = &parent_right;
      heap.size_ = 2;
      heap.bottom_up_heapify(&right_child);
      CHECK(heap.root_ == &right_child);
    }
  }
}
