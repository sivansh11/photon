#ifndef CBVH
#define CBVH

#include "horizon/core/aabb.hpp"
#include "horizon/core/bvh.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/logger.hpp"
#include "horizon/core/math.hpp"
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace bvh {

template <typename T> struct qaabb_t {
  struct qvec3 {
    T x, y, z;
  };
  qvec3 min, max;
};

template <typename T>
inline std::ostream &operator<<(std::ostream &o, const qaabb_t<T> qaabb) {
  o << '(' << uint32_t(qaabb.min.x) << ',' << uint32_t(qaabb.min.y) << ','
    << uint32_t(qaabb.min.z) << "), (" << '(' << uint32_t(qaabb.max.x) << ','
    << uint32_t(qaabb.max.y) << ',' << uint32_t(qaabb.max.z) << ")";
  return o;
}

inline std::ostream &operator<<(std::ostream &o, const core::aabb_t &aabb) {
  o << "min:" << core::to_string(aabb.min)
    << " max:" << core::to_string(aabb.max);
  return o;
}

template <typename T> struct cnode_t {
  qaabb_t<T> qaabb;
  uint32_t is_leaf : 1;
  uint32_t primitive_count : 31;
  union as_t {
    struct leaf_t {
      uint32_t first_primitive_index : FIRST_INDEX_BITS_SIZE;
      uint32_t dummy : 32 - FIRST_INDEX_BITS_SIZE;
    } leaf;
    struct internal_t {
      uint32_t first_child_index : FIRST_INDEX_BITS_SIZE;
      uint32_t children_count : 32 - FIRST_INDEX_BITS_SIZE;
    } internal;
  } as;
};
static_assert(sizeof(cnode_t<uint8_t>) == 16, "sizeof(cnode_t) != 16");

template <typename T> struct cbvh_t {
  core::aabb_t root_aabb;
  std::vector<cnode_t<T>> nodes;
  std::vector<uint32_t> primitive_indices;
};

template <typename T>
inline qaabb_t<T> quntize_aabb(const core::aabb_t &parent,
                               const core::aabb_t &child) {
  qaabb_t<T> qaabb{};

  const core::vec3 parent_extent = parent.max - parent.min;
  const core::vec3 inverse_parent_extent = 1.f / parent_extent;

  core::vec3 relative_min = (child.min - parent.min) * inverse_parent_extent;
  core::vec3 relative_max = (parent.max - child.max) * inverse_parent_extent;

  // relative -> -1, 1
  // to
  // relative ->  0, 1
  relative_min = (relative_min + 1.f) / 2.f;
  relative_max = (relative_max + 1.f) / 2.f;

  // TODO: change this ?
  constexpr float scaling_factor = T(-1);

  qaabb.min.x = static_cast<T>(
      core::max(core::min(0.f, relative_min.x * scaling_factor), 1.f));
  qaabb.min.y = static_cast<T>(
      core::max(core::min(0.f, relative_min.y * scaling_factor), 1.f));
  qaabb.min.z = static_cast<T>(
      core::max(core::min(0.f, relative_min.z * scaling_factor), 1.f));
  qaabb.max.x = static_cast<T>(
      core::max(core::min(0.f, relative_max.x * scaling_factor), 1.f));
  qaabb.max.y = static_cast<T>(
      core::max(core::min(0.f, relative_max.y * scaling_factor), 1.f));
  qaabb.max.z = static_cast<T>(
      core::max(core::min(0.f, relative_max.z * scaling_factor), 1.f));

  return qaabb;
}

template <typename T>
inline core::aabb_t dequntize_aabb(const core::aabb_t &parent,
                                   const qaabb_t<T> &child) {
  core::aabb_t aabb{};

  const core::vec3 parent_extent = parent.max - parent.min;

  constexpr float scaling_factor = T(-1);
  constexpr float inverse_scaling_factor = 1.f / scaling_factor;

  core::vec3 relative_min, relative_max;
  relative_min.x = child.min.x * inverse_scaling_factor;
  relative_min.y = child.min.y * inverse_scaling_factor;
  relative_min.z = child.min.z * inverse_scaling_factor;
  relative_max.x = child.max.x * inverse_scaling_factor;
  relative_max.y = child.max.y * inverse_scaling_factor;
  relative_max.z = child.max.z * inverse_scaling_factor;

  relative_min = (relative_min * 2.f) - 1.f;
  relative_max = (relative_max * 2.f) - 1.f;

  aabb.min = parent.min + (relative_min * parent_extent);
  aabb.max = parent.max - (relative_max * parent_extent);

  return aabb;
}

template <typename T>
inline cbvh_t<T> convert_bvh_to_cbvh(const core::bvh::bvh_t &bvh) {
  cbvh_t<T> cbvh{};
  check(bvh.nodes.size() >= 1, "bvh should have atleast 1 node");
  cbvh.root_aabb = bvh.nodes[0].aabb;

  // TODO: calculate parents while building bvh
  std::vector<uint32_t> parents{};
  parents.resize(bvh.nodes.size(), core::bvh::invalid_index);

  for (uint32_t i = 0; i < bvh.nodes.size(); i++) {
    const core::bvh::node_t &node = bvh.nodes[i];
    if (node.is_leaf)
      continue;
    for (uint32_t child_index = node.as.internal.first_child_index;
         child_index <
         node.as.internal.first_child_index + node.as.internal.children_count;
         child_index++) {
      parents[child_index] = i;
    }
  }

  // actual convertion
  for (uint32_t i = 0; i < bvh.nodes.size(); i++) {
    const core::bvh::node_t &node = bvh.nodes[i];
    cnode_t<T> cnode{};
    cnode.is_leaf = node.is_leaf;
    cnode.primitive_count = node.primitive_count;
    if (node.is_leaf) {
      cnode.as.leaf.first_primitive_index = node.as.leaf.first_primitive_index;
      cnode.as.leaf.dummy = node.as.leaf.dummy;
    } else {
      cnode.as.internal.first_child_index = node.as.internal.first_child_index;
      cnode.as.internal.children_count = node.as.internal.children_count;
    }
    if (i == 0) {
      cnode.qaabb = quntize_aabb<T>(cbvh.root_aabb, node.aabb);
    } else {
      cnode.qaabb = quntize_aabb<T>(bvh.nodes[parents[i]].aabb, node.aabb);
    }
    cbvh.nodes.push_back(cnode);
  }

  cbvh.primitive_indices = bvh.primitive_indices;

  return cbvh;
}

} // namespace bvh

#endif // !CBVH
