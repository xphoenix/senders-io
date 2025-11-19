#include "sio/intrusive/heap.hpp"

#include <catch2/catch_all.hpp>

#include <array>
#include <functional>
#include <vector>

namespace {
  struct heap_node {
    int key;
    heap_node* prev = nullptr;
    heap_node* left = nullptr;
    heap_node* right = nullptr;
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
} // namespace

TEST_CASE("intrusive heap returns ascending keys", "[intrusive][heap]") {
  min_heap heap;
  std::array<heap_node, 6> nodes{heap_node{4}, heap_node{1}, heap_node{5}, heap_node{2}, heap_node{3}, heap_node{0}};
  for (auto& node: nodes) {
    heap.insert(&node);
  }
  std::vector<int> ordered;
  while (!heap.empty()) {
    REQUIRE(heap.front() != nullptr);
    ordered.push_back(heap.front()->key);
    heap.pop_front();
  }
  CHECK(ordered == std::vector<int>{0, 1, 2, 3, 4, 5});
}

TEST_CASE("intrusive heap erase keeps structure valid", "[intrusive][heap]") {
  min_heap heap;
  std::array<heap_node, 5> nodes{heap_node{7}, heap_node{3}, heap_node{5}, heap_node{1}, heap_node{9}};
  for (auto& node: nodes) {
    heap.insert(&node);
  }
  REQUIRE(heap.erase(&nodes[3])); // erase smallest element (1)
  REQUIRE(heap.front()->key == 3);
  REQUIRE(heap.erase(&nodes[4])); // erase leaf (9)
  REQUIRE_FALSE(heap.erase(nullptr));
  std::vector<int> ordered;
  while (!heap.empty()) {
    ordered.push_back(heap.front()->key);
    heap.pop_front();
  }
  CHECK(ordered == std::vector<int>{3, 5, 7});
}

TEST_CASE("intrusive heap supports max ordering", "[intrusive][heap]") {
  max_heap heap;
  std::array<heap_node, 4> nodes{heap_node{1}, heap_node{4}, heap_node{2}, heap_node{3}};
  for (auto& node: nodes) {
    heap.insert(&node);
  }
  std::vector<int> ordered;
  while (!heap.empty()) {
    ordered.push_back(heap.front()->key);
    heap.pop_front();
  }
  CHECK(ordered == std::vector<int>{4, 3, 2, 1});
}

TEST_CASE("intrusive heap move operations preserve ordering", "[intrusive][heap]") {
  min_heap source;
  std::array<heap_node, 3> nodes{heap_node{5}, heap_node{1}, heap_node{3}};
  for (auto& node: nodes) {
    source.insert(&node);
  }

  min_heap moved{std::move(source)};
  REQUIRE(source.empty());
  REQUIRE(moved.front()->key == 1);

  min_heap target;
  target = std::move(moved);
  REQUIRE(moved.empty());
  std::vector<int> ordered;
  while (!target.empty()) {
    ordered.push_back(target.front()->key);
    target.pop_front();
  }
  CHECK(ordered == std::vector<int>{1, 3, 5});
}

TEST_CASE("intrusive heap edge cases improve coverage", "[intrusive][heap]") {
  SECTION("pop_front and erase handle empty and singleton heaps") {
    min_heap min;
    min.pop_front();
    heap_node lone{7};
    min.insert(&lone);
    min.pop_front();
    min.insert(&lone);
    REQUIRE(min.erase(&lone));

    max_heap max;
    max.pop_front();
    heap_node solo{9};
    max.insert(&solo);
    max.pop_front();
    max.insert(&solo);
    REQUIRE(max.erase(&solo));
    REQUIRE_FALSE(max.erase(nullptr));
  }

  SECTION("self move assignment leaves heap intact") {
    min_heap heap;
    heap_node n1{1};
    heap_node n2{2};
    heap.insert(&n1);
    heap.insert(&n2);
    heap = std::move(heap);
    REQUIRE_FALSE(heap.empty());
    CHECK(heap.front()->key == 1);
    CHECK(heap.size() == 2);
  }

  SECTION("erasing the root promotes the final leaf") {
    min_heap heap;
    heap_node n1{1};
    heap_node n2{5};
    heap_node n3{6};
    heap.insert(&n1);
    heap.insert(&n2);
    heap.insert(&n3);
    REQUIRE(heap.erase(&n1));
    auto* new_root = heap.front();
    REQUIRE(new_root != nullptr);
    CHECK(new_root->prev == nullptr);
    if (new_root->left) {
      CHECK(new_root->left->prev == new_root);
    }
    if (new_root->right) {
      CHECK(new_root->right->prev == new_root);
    }
  }

  SECTION("erasing parents of the last leaf rewires hooks") {
    min_heap heap;
    std::array<heap_node, 6> nodes{heap_node{10}, heap_node{20}, heap_node{30}, heap_node{40}, heap_node{50}, heap_node{60}};
    for (auto& node: nodes) {
      heap.insert(&node);
    }

    REQUIRE(heap.erase(&nodes[2]));
    CHECK(nodes[5].prev == heap.front());
    CHECK(nodes[5].left == nullptr);
    CHECK(nodes[5].right == nullptr);
  }

  SECTION("erasing nodes with two children keeps them linked") {
    min_heap heap;
    std::array<heap_node, 5> nodes{heap_node{10}, heap_node{20}, heap_node{30}, heap_node{40}, heap_node{50}};
    for (auto& node: nodes) {
      heap.insert(&node);
    }

    REQUIRE(heap.erase(&nodes[1]));
    CHECK(nodes[1].prev == nullptr);
    CHECK(nodes[1].left == nullptr);
    CHECK(nodes[1].right == nullptr);

    std::vector<int> ordered;
    while (!heap.empty()) {
      ordered.push_back(heap.front()->key);
      heap.pop_front();
    }
    CHECK(ordered == std::vector<int>{10, 30, 40, 50});
  }

  SECTION("top_down heapify can choose the right child") {
    min_heap heap;
    heap_node root{1};
    heap_node left{10};
    heap_node right{9};
    heap_node left_left{11};
    heap_node left_right{12};
    heap.insert(&root);
    heap.insert(&left);
    heap.insert(&right);
    heap.insert(&left_left);
    heap.insert(&left_right);

    heap.pop_front();
    REQUIRE(heap.front() != nullptr);
    CHECK(heap.front()->key == 9);
  }
}

TEST_CASE("intrusive max heap edge cases mirror coverage", "[intrusive][heap]") {
  auto run_with_desc_nodes = []<class Fn>(Fn&& fn) {
    std::array<heap_node, 6> nodes{
      heap_node{60},
      heap_node{50},
      heap_node{40},
      heap_node{30},
      heap_node{20},
      heap_node{10}};
    max_heap heap;
    for (auto& node: nodes) {
      heap.insert(&node);
    }
    fn(heap, nodes);
  };

  SECTION("erase handles leaf, internal, and root nodes in max heap") {
    run_with_desc_nodes([](max_heap& heap, auto& nodes) {
      REQUIRE(heap.erase(&nodes[5])); // node == leaf branch
      CHECK(nodes[5].prev == nullptr);
    });

    run_with_desc_nodes([](max_heap& heap, auto& nodes) {
      REQUIRE(heap.erase(&nodes[2])); // parent of last leaf
      CHECK(nodes[2].prev == nullptr);
    });

    run_with_desc_nodes([](max_heap& heap, auto& nodes) {
      REQUIRE(heap.erase(&nodes[1])); // node with two children
      CHECK(nodes[1].left == nullptr);
      CHECK(nodes[1].right == nullptr);
    });

    run_with_desc_nodes([](max_heap& heap, auto& nodes) {
      REQUIRE(heap.erase(&nodes[0])); // erase root
      CHECK(nodes[0].prev == nullptr);
    });

    run_with_desc_nodes([](max_heap& heap, auto&) {
      CHECK_FALSE(heap.erase(nullptr));
    });
  }

  SECTION("max heap heapify paths use right child selections") {
    max_heap heap;
    heap_node root{10};
    heap_node left{6};
    heap_node right{9};
    heap_node left_left{5};
    heap_node left_right{4};
    heap.insert(&root);
    heap.insert(&left);
    heap.insert(&right);
    heap.insert(&left_left);
    heap.insert(&left_right);
    heap.pop_front();
    CHECK(heap.front()->key == 9);
  }

  SECTION("max heap insertions swap right children when needed") {
    max_heap heap;
    heap_node n1{5};
    heap_node n2{2};
    heap_node n3{3};
    heap.insert(&n1);
    heap.insert(&n2);
    heap.insert(&n3);
    heap_node bigger{7};
    heap.insert(&bigger);
    CHECK(heap.front()->key == 7);
  }
}
