#ifndef CORE_BVH_TRAVERSAL_HPP
#define CORE_BVH_TRAVERSAL_HPP

#include "horizon/core/bvh.hpp"
#include "horizon/core/core.hpp"
#include <limits>
#include <stack>

namespace bvh {

struct ray_t {
  static ray_t create(core::vec3 origin, core::vec3 direction) {
    ray_t ray{};
    ray.origin = origin;
    ray.direction = direction;
    return ray;
  }
  core::vec3 origin, direction;
};

struct triangle_t {
  core::vec3 v0, v1, v2;
};

struct ray_data_t {
  static ray_data_t create(const ray_t &ray) {
    ray_data_t ray_data{};
    ray_data.origin = ray.origin;
    ray_data.direction = ray.direction;
    ray_data.inv_direction = core::vec3{core::safe_inverse(ray.direction.x),
                                        core::safe_inverse(ray.direction.y),
                                        core::safe_inverse(ray.direction.z)};
    ray_data.tmin = std::numeric_limits<float>::epsilon();
    ray_data.tmax = core::infinity;
    return ray_data;
  }
  core::vec3 origin, direction;
  core::vec3 inv_direction;
  float tmin, tmax;
};

struct triangle_intersection_t {
  bool did_intersect() { return _did_intersect; }
  bool _did_intersect;
  float t, u, v, w;
};

struct aabb_intersection_t {
  bool did_intersect() { return tmin <= tmax; }
  float tmin, tmax;
};

struct hit_data_t {
  uint32_t primitive_index = core::bvh::invalid_index;
  float t = core::infinity;
  float u, v, w;
};

inline triangle_intersection_t triangle_intersect(const triangle_t &triangle,
                                                  const ray_data_t &ray_data) {
  core::vec3 e1 = triangle.v0 - triangle.v1;
  core::vec3 e2 = triangle.v2 - triangle.v0;
  core::vec3 n = cross(e1, e2);

  core::vec3 c = triangle.v0 - ray_data.origin;
  core::vec3 r = cross(ray_data.direction, c);
  float inverse_det = 1.0f / dot(n, ray_data.direction);

  float u = dot(r, e2) * inverse_det;
  float v = dot(r, e1) * inverse_det;
  float w = 1.0f - u - v;

  triangle_intersection_t intersection;

  if (u >= 0 && v >= 0 && w >= 0) {
    float t = dot(n, c) * inverse_det;
    if (t >= ray_data.tmin && t <= ray_data.tmax) {
      intersection._did_intersect = true;
      intersection.t = t;
      intersection.u = u;
      intersection.v = v;
      intersection.w = w;
      return intersection;
    }
  }
  intersection._did_intersect = false;
  return intersection;
}

inline aabb_intersection_t aabb_intersect(const core::aabb_t &aabb,
                                          const ray_data_t &ray_data) {
  core::vec3 tmin = (aabb.min - ray_data.origin) * ray_data.inv_direction;
  core::vec3 tmax = (aabb.max - ray_data.origin) * ray_data.inv_direction;

  const core::vec3 old_tmin = tmin;
  const core::vec3 old_tmax = tmax;

  tmin = min(old_tmin, old_tmax);
  tmax = max(old_tmin, old_tmax);

  float _tmin =
      core::max(tmin[0], core::max(tmin[1], core::max(tmin[2], ray_data.tmin)));
  float _tmax =
      core::min(tmax[0], core::min(tmax[1], core::min(tmax[2], ray_data.tmax)));

  aabb_intersection_t aabb_intersection = {_tmin, _tmax};
  return aabb_intersection;
}

// TODO: better traversal
inline hit_data_t traverse(const core::bvh::bvh_t &bvh, ray_data_t &ray_data,
                           const triangle_t *p_triangles) {
  hit_data_t hit{};

  std::stack<uint32_t> stack{};
  stack.push(0);

  while (stack.size()) {
    auto node_index = stack.top();
    stack.pop();
    core::bvh::node_t node = bvh.nodes[node_index];
    aabb_intersection_t intersection = aabb_intersect(node.aabb, ray_data);
    if (intersection.did_intersect()) {
      if (node.is_leaf) {
        for (uint32_t i = 0; i < node.primitive_count; i++) {
          uint32_t primitive_index =
              bvh.primitive_indices[node.as.leaf.first_primitive_index + i];
          const triangle_t &triangle = p_triangles[primitive_index];
          triangle_intersection_t intersection =
              triangle_intersect(triangle, ray_data);
          if (intersection.did_intersect()) {
            ray_data.tmax = intersection.t;
            hit.primitive_index = primitive_index;
            hit.t = intersection.t;
            hit.u = intersection.u;
            hit.v = intersection.v;
            hit.w = intersection.w;
          }
        }
      } else {
        for (uint32_t i = 0; i < node.as.internal.children_count; i++) {
          stack.push(node.as.internal.first_child_index + i);
        }
      }
    }
  }
  return hit;
}

} // namespace bvh

#endif
