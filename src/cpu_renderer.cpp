#include "photon/cpu_renderer.hpp"

#include "glm/fwd.hpp"
#include "horizon/core/aabb.hpp"
#include "horizon/core/bvh.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/ecs.hpp"
#include "horizon/core/logger.hpp"
#include "horizon/core/math.hpp"
#include "horizon/core/model.hpp"
#include "horizon/core/stb_image.h"

#include "horizon/gfx/helper.hpp"

#include "photon/image.hpp"

#include <algorithm>
#include <cstring>
#include <ostream>
#include <stack>
#include <sys/types.h>
#include <vector>

namespace core {

namespace bvh {

// TODO: add this to bvh.hpp as a #define TRIANGLE_BVH maybe
struct triangle_t {
  vec3 v0, v1, v2;

  vec3 center() const { return (v0 + v1 + v2) / 3.f; }
  aabb_t aabb() const { return aabb_t{}.grow(v0).grow(v1).grow(v2); }
};

// TODO: add this to bvh.hpp as a #define TRIANGLE_BVH maybe
struct triangle_indices_t {
  uint32_t i0, i1, i2;
};

// TODO: add this to ray.hpp maybe
struct ray_t {
  static ray_t create(core::vec3 origin, core::vec3 direction) {
    ray_t ray{};
    ray.origin = origin;
    ray.direction = direction;
    return ray;
  }
  core::vec3 origin, direction;
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
  uint32_t instance_index = core::bvh::invalid_index;
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

} // namespace bvh

} // namespace core

namespace photon {

namespace utils {

// static stack
template <typename T, size_t max_stack> struct stack {
  stack() : _top(0) {}
  ~stack() {}

  void push(T val) { _data[_top++] = val; }

  T top() { return _data[_top - 1]; }

  void pop() { --_top; }

  uint32_t size() { return _top; }

  T _data[max_stack];
  uint32_t _top;
};

} // namespace utils

struct texture_t {
  uint32_t width, height;
  uint8_t *pixel_data;

  core::vec4 sample(float u, float v) {
    u = u - core::floor(u);
    v = v - core::floor(v);
    uint32_t x = u * width;
    uint32_t y = v * height;
    return {float(pixel_data[(y * width + x) * 4 + 0]) / 255.f,
            float(pixel_data[(y * width + x) * 4 + 1]) / 255.f,
            float(pixel_data[(y * width + x) * 4 + 2]) / 255.f, 1};
  }
};

struct material_t {
  texture_t diffuse{};
};

struct bvh_triangle_instance_t {
  core::bvh::triangle_t *triangles{};
  core::bvh::triangle_indices_t *triangle_indices{};
  core::vertex_t *vertices{}; // raw vertices
  core::bvh::node_t *nodes{};
  uint32_t *primitive_indices{};
  core::aabb_t root{};
  material_t material{};

  // user id
  // transform
};

struct mesh_t {
  bvh_triangle_instance_t instance{};
};

struct model_t {
  std::vector<mesh_t> meshes{};
};

core::bvh::hit_data_t traverse_bvh2(const bvh_triangle_instance_t &instance,
                                    core::bvh::ray_data_t &ray_data) {
  core::bvh::hit_data_t hit;
  hit.primitive_index = core::bvh::invalid_index;

  utils::stack<uint32_t, 16> stack;

  core::bvh::node_t root = instance.nodes[0];
  if (!core::bvh::aabb_intersect(root.aabb, ray_data).did_intersect())
    return hit;

  if (root.is_leaf) {
    for (uint32_t i = 0; i < root.primitive_count; i++) {
      uint32_t primitive_index =
          instance.primitive_indices[root.as.leaf.first_primitive_index + i];
      core::bvh::triangle_t triangle = instance.triangles[primitive_index];
      core::bvh::triangle_intersection_t intersection =
          core::bvh::triangle_intersect(triangle, ray_data);
      if (intersection.did_intersect()) {
        ray_data.tmax = intersection.t;
        hit.primitive_index = primitive_index;
        hit.t = intersection.t;
        hit.u = intersection.u;
        hit.v = intersection.v;
        hit.w = intersection.w;
      }
    }
    return hit;
  }

  uint32_t current = 1;
  while (true) {
    const core::bvh::node_t left = instance.nodes[current];
    const core::bvh::node_t right = instance.nodes[current + 1];

    core::bvh::aabb_intersection_t left_intersection =
        core::bvh::aabb_intersect(left.aabb, ray_data);
    core::bvh::aabb_intersection_t right_intersection =
        core::bvh::aabb_intersect(right.aabb, ray_data);

    uint32_t start = 0;
    uint32_t end = 0;
    if (left_intersection.did_intersect() && left.is_leaf) {
      if (right_intersection.did_intersect() && right.is_leaf) {
        start = left.as.leaf.first_primitive_index;
        end = right.as.leaf.first_primitive_index + right.primitive_count;
      } else {
        start = left.as.leaf.first_primitive_index;
        end = left.as.leaf.first_primitive_index + left.primitive_count;
      }
    } else if (right_intersection.did_intersect() && right.is_leaf) {
      start = right.as.leaf.first_primitive_index;
      end = right.as.leaf.first_primitive_index + right.primitive_count;
    }
    for (uint32_t i = start; i < end; i++) {
      uint32_t primitive_index = instance.primitive_indices[i];
      core::bvh::triangle_t triangle = instance.triangles[primitive_index];
      core::bvh::triangle_intersection_t intersection =
          core::bvh::triangle_intersect(triangle, ray_data);
      if (intersection.did_intersect()) {
        ray_data.tmax = intersection.t;
        hit.primitive_index = primitive_index;
        hit.t = intersection.t;
        hit.u = intersection.u;
        hit.v = intersection.v;
        hit.w = intersection.w;
      }
    }
    if (left_intersection.did_intersect() && !left.is_leaf) {
      if (right_intersection.did_intersect() && !right.is_leaf) {
        if (left_intersection.tmin <= right_intersection.tmin) {
          current = left.as.internal.first_child_index;
          stack.push(right.as.internal.first_child_index);
        } else {
          current = right.as.internal.first_child_index;
          stack.push(left.as.internal.first_child_index);
        }
      } else {
        current = left.as.internal.first_child_index;
      }
    } else {
      if (right_intersection.did_intersect() && !right.is_leaf) {
        current = right.as.internal.first_child_index;
      } else {
        if (!stack.size())
          return hit;
        current = stack.top();
        stack.pop();
      }
    }
  }
  return hit;
}

core::bvh::hit_data_t traverse_tlas(const core::bvh::bvh_t &bvh,
                                    bvh_triangle_instance_t *instances,
                                    core::bvh::ray_data_t &ray_data) {
  core::bvh::hit_data_t hit{};

  utils::stack<uint32_t, 8> stack{};
  stack.push(0);

  while (stack.size()) {
    auto node_index = stack.top();
    stack.pop();
    core::bvh::node_t node = bvh.nodes[node_index];
    core::bvh::aabb_intersection_t intersection =
        core::bvh::aabb_intersect(node.aabb, ray_data);
    if (intersection.did_intersect()) {
      if (node.is_leaf) {
        for (uint32_t i = 0; i < node.primitive_count; i++) {
          uint32_t instance_index =
              bvh.primitive_indices[node.as.leaf.first_primitive_index + i];

          auto blas_hit = traverse_bvh2(instances[instance_index], ray_data);
          if (blas_hit.primitive_index != core::bvh::invalid_index) {
            // hit
            hit = blas_hit;
            hit.instance_index = instance_index;
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

model_t create_bvh_model_from_raw_model(const core::raw_model_t &raw_model) {
  model_t model{};

  for (const auto &raw_mesh : raw_model.meshes) {
    std::vector<core::bvh::triangle_t> triangles{};
    std::vector<core::bvh::triangle_indices_t> triangle_indices{};
    std::vector<core::aabb_t> aabbs{};
    std::vector<core::vec3> centers{};
    for (uint32_t i = 0; i < raw_mesh.indices.size(); i += 3) {
      core::bvh::triangle_t triangle{
          .v0 = raw_mesh.vertices[raw_mesh.indices[i + 0]].position,
          .v1 = raw_mesh.vertices[raw_mesh.indices[i + 1]].position,
          .v2 = raw_mesh.vertices[raw_mesh.indices[i + 2]].position,
      };
      core::bvh::triangle_indices_t triangle_indice{
          .i0 = raw_mesh.indices[i + 0],
          .i1 = raw_mesh.indices[i + 1],
          .i2 = raw_mesh.indices[i + 2],
      };
      triangles.push_back(triangle);
      triangle_indices.push_back(triangle_indice);
      aabbs.push_back(triangle.aabb());
      centers.push_back(triangle.center());
    }

    core::bvh::options_t options{
        .o_min_primitive_count = 1,
        .o_max_primitive_count = 8,
        .o_object_split_search_type =
            core::bvh::object_split_search_type_t::e_binned_sah,
        .o_primitive_intersection_cost = 1.1f,
        .o_node_intersection_cost = 1.f,
        .o_samples = 8,
    };

    core::bvh::bvh_t bvh = core::bvh::build_bvh2(aabbs.data(), centers.data(),
                                                 triangles.size(), options);

    mesh_t mesh{};
    mesh.instance.triangles = new core::bvh::triangle_t[triangles.size()];
    mesh.instance.triangle_indices =
        new core::bvh::triangle_indices_t[triangle_indices.size()];
    mesh.instance.vertices = new core::vertex_t[raw_mesh.vertices.size()];
    mesh.instance.nodes = new core::bvh::node_t[bvh.nodes.size()];
    mesh.instance.primitive_indices =
        new uint32_t[bvh.primitive_indices.size()];
    std::memcpy(mesh.instance.triangles, triangles.data(),
                triangles.size() * sizeof(triangles[0]));
    std::memcpy(mesh.instance.triangle_indices, triangle_indices.data(),
                triangle_indices.size() * sizeof(triangle_indices[0]));
    std::memcpy(mesh.instance.vertices, raw_mesh.vertices.data(),
                raw_mesh.vertices.size() * sizeof(raw_mesh.vertices[0]));
    std::memcpy(mesh.instance.nodes, bvh.nodes.data(),
                bvh.nodes.size() * sizeof(bvh.nodes[0]));
    std::memcpy(mesh.instance.primitive_indices, bvh.primitive_indices.data(),
                bvh.primitive_indices.size() *
                    sizeof(bvh.primitive_indices[0]));
    mesh.instance.root = mesh.instance.nodes[0].aabb;

    auto itr = std::find_if(raw_mesh.material_description.texture_infos.begin(),
                            raw_mesh.material_description.texture_infos.end(),
                            [](const core::texture_info_t &texture_info) {
                              return texture_info.texture_type ==
                                         core::texture_type_t::e_diffuse_map |
                                     texture_info.texture_type ==
                                         core::texture_type_t::e_diffuse_color;
                            });

    if (itr != raw_mesh.material_description.texture_infos.end() &&
        itr->texture_type == core::texture_type_t::e_diffuse_map) {
      // load texture
      int width, height, channels;
      stbi_set_flip_vertically_on_load(true);
      stbi_uc *pixels = stbi_load(itr->file_path.string().c_str(), &width,
                                  &height, &channels, STBI_rgb_alpha);

      mesh.instance.material.diffuse.width = width;
      mesh.instance.material.diffuse.height = height;
      mesh.instance.material.diffuse.pixel_data = pixels;
      // TODO: image free
      // stbi_image_free(pixels);
    } else if (itr != raw_mesh.material_description.texture_infos.end() &&
               itr->texture_type == core::texture_type_t::e_diffuse_color) {
      // default texture
      mesh.instance.material.diffuse.width = 1;
      mesh.instance.material.diffuse.height = 1;
      mesh.instance.material.diffuse.pixel_data =
          new uint8_t[1 * 1 * 4]; // 4 channels
      mesh.instance.material.diffuse.pixel_data[0] = itr->diffuse_color.r * 255;
      mesh.instance.material.diffuse.pixel_data[1] = itr->diffuse_color.g * 255;
      mesh.instance.material.diffuse.pixel_data[2] = itr->diffuse_color.b * 255;
      mesh.instance.material.diffuse.pixel_data[3] = 255;
      // std::memset(mesh.instance.material.diffuse.pixel_data, 255, 1 * 1 * 4);
    } else {
      // default texture
      mesh.instance.material.diffuse.width = 1;
      mesh.instance.material.diffuse.height = 1;
      mesh.instance.material.diffuse.pixel_data =
          new uint8_t[1 * 1 * 4]; // 4 channels
      std::memset(mesh.instance.material.diffuse.pixel_data, 255, 1 * 1 * 4);
    }

    model.meshes.push_back(mesh);
  }
  return model;
}

// taken from
// https://sibaku.github.io/computer-graphics/2017/01/10/Camera-Ray-Generation.html
core::bvh::ray_data_t create_ray(core::vec2 px, core::mat4 PInv,
                                 core::mat4 VInv) {

  core::vec2 pxNDS = px * 2.f - 1.f;
  core::vec3 pointNDS = core::vec3(pxNDS, -1.);
  core::vec4 pointNDSH = core::vec4(pointNDS, 1.0);
  core::vec4 dirEye = PInv * pointNDSH;
  dirEye.w = 0.;
  core::vec3 dirWorld = core::vec3(VInv * dirEye);
  core::vec3 scale, translation, skew;
  core::quat rotation;
  core::vec4 perspective;
  core::decompose(core::inverse(VInv), scale, rotation, translation, skew,
                  perspective);
  return core::bvh::ray_data_t::create(
      core::bvh::ray_t::create(-translation, dirWorld));
}

cpu_renderer_t::cpu_renderer_t(uint32_t width, uint32_t height,
                               core::ref<core::dispatcher_t> dispatcher) {
  _image = core::make_ref<image_t>(width, height);
}

cpu_renderer_t::~cpu_renderer_t() {}

std::ostream &operator<<(std::ostream &o, const core::vec3 &v) {
  o << core::to_string(v);
  return o;
}

void cpu_renderer_t::render(ecs::scene_t<> &scene,
                            const core::camera_t &camera) {
  // prepare scene
  scene.for_all<core::raw_model_t>([&](ecs::entity_id_t id,
                                       const core::raw_model_t &raw_model) {
    if (scene.has<model_t>(id))
      return;
    scene.construct<model_t>(id) = create_bvh_model_from_raw_model(raw_model);
  });
  // create tlas here
  std::vector<bvh_triangle_instance_t> instances{};
  std::vector<core::aabb_t> aabbs{};
  std::vector<core::vec3> centers{};
  std::vector<material_t> materials{};
  scene.for_all<model_t>([&](auto, const model_t &model) {
    for (const auto &mesh : model.meshes) {
      instances.push_back(mesh.instance);
      aabbs.push_back(mesh.instance.root);
      centers.push_back(mesh.instance.root.center());
      materials.push_back(mesh.instance.material);
    }
  });

  core::bvh::options_t options{
      .o_min_primitive_count = 1,
      .o_max_primitive_count = 8,
      .o_object_split_search_type =
          core::bvh::object_split_search_type_t::e_binned_sah,
      .o_primitive_intersection_cost = 1.5f,
      .o_node_intersection_cost = 1.f,
      .o_samples = 16,
  };
  core::bvh::bvh_t tlas = core::bvh::build_bvh2(aabbs.data(), centers.data(),
                                                instances.size(), options);

  // render
  // TODO: multithread this
  scene.for_all<model_t>([&](auto id, model_t &model) {
    for (uint32_t i = 0; i < _image->width; i++) {
      for (uint32_t j = 0; j < _image->height; j++) {
        core::vec2 uv;
        uv.x = float(i) / float(_image->width - 1);
        uv.y = float(j) / float(_image->height - 1);
        core::bvh::ray_data_t ray_data = create_ray(
            uv, core::inverse(camera.projection), core::inverse(camera.view));

        // TLAS rendering
        auto hit = traverse_tlas(tlas, instances.data(), ray_data);
        if (hit.primitive_index != core::bvh::invalid_index) {
          bvh_triangle_instance_t instance = instances[hit.instance_index];
          core::bvh::triangle_indices_t triangle_indices =
              instance.triangle_indices
                  [instance.primitive_indices[hit.primitive_index]];
          core::vertex_t v0, v1, v2;
          v0 = instance.vertices[triangle_indices.i0];
          v1 = instance.vertices[triangle_indices.i1];
          v2 = instance.vertices[triangle_indices.i2];

          float u = hit.u, v = hit.v, w = hit.w;

          core::vertex_t vertex{};
          vertex.position = u * v0.position + v * v1.position + w * v2.position;
          vertex.normal = u * v0.normal + v * v1.normal + w * v2.normal;
          vertex.uv = u * v0.uv + v * v1.uv + w * v2.uv;
          vertex.tangent = u * v0.tangent + v * v1.tangent + w * v2.tangent;
          vertex.bi_tangent =
              u * v0.bi_tangent + v * v1.bi_tangent + w * v2.bi_tangent;

          // _image->at(i, j) =
          //     instance.material.diffuse.sample(vertex.uv.x, vertex.uv.y);

          _image->at(i, j) = core::vec4{
              ((hit.primitive_index * 8765 + 135) % 255) / 255.f,
              ((hit.primitive_index * 4 * 856 + 74334) % 255) / 255.f,
              ((hit.primitive_index * 2 * 879 + 86) % 255) / 255.f, 1.f};
        }

        // No TLAS rendering
        // for (auto &mesh : model.meshes) {
        //   auto hit = traverse_bvh2(mesh.instance, ray_data);
        //   if (hit.primitive_index != core::bvh::invalid_index) {
        //     _image->at(i, j) = core::vec4{
        //         ((hit.primitive_index * 8765 + 135) % 255) / 255.f,
        //         ((hit.primitive_index * 4 * 856 + 74334) % 255) / 255.f,
        //         ((hit.primitive_index * 2 * 879 + 86) % 255) / 255.f, 1.f};
        //   }
        // }
      }
    }
  });
}

} // namespace photon
