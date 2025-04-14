#include "photon/cpu_renderer.hpp"

#include "horizon/core/aabb.hpp"
#include "horizon/core/bvh.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/ecs.hpp"

#include "horizon/core/math.hpp"
#include "horizon/core/model.hpp"
#include "photon/image.hpp"

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

struct bvh_triangle_instance_t {
  core::bvh::triangle_t *triangles{};
  core::bvh::triangle_indices_t *triangle_indices{};
  core::vertex_t *vertices{}; // raw vertices
  core::bvh::node_t *nodes{};
  uint32_t *primitive_indices{};

  // user id
  // transform
};

struct mesh_t {
  bvh_triangle_instance_t instance{};
};

struct model_t {
  std::vector<mesh_t> meshes{};
};

core::bvh::hit_data_t traverse(const bvh_triangle_instance_t &instance,
                               core::bvh::ray_data_t &ray_data) {
  core::bvh::hit_data_t hit{};

  std::stack<uint32_t> stack{};
  stack.push(0);

  while (stack.size()) {
    auto node_index = stack.top();
    stack.pop();
    core::bvh::node_t node = instance.nodes[node_index];
    core::bvh::aabb_intersection_t intersection =
        aabb_intersect(node.aabb, ray_data);
    if (intersection.did_intersect()) {
      if (node.is_leaf) {
        for (uint32_t i = 0; i < node.primitive_count; i++) {
          uint32_t primitive_index =
              instance
                  .primitive_indices[node.as.leaf.first_primitive_index + i];
          const core::bvh::triangle_t &triangle =
              instance.triangles[primitive_index];
          core::bvh::triangle_intersection_t intersection =
              core::bvh::triangle_intersect((core::bvh::triangle_t &)triangle,
                                            ray_data);
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
        .o_max_primitive_count = std::numeric_limits<uint32_t>::max(),
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

  // render
  scene.for_all<model_t>([&](auto id, model_t &model) {
    for (uint32_t i = 0; i < _image->width; i++) {
      for (uint32_t j = 0; j < _image->height; j++) {
        core::vec2 uv;
        uv.x = float(i) / float(_image->width - 1);
        uv.y = float(j) / float(_image->height - 1);
        core::bvh::ray_data_t ray_data = create_ray(
            uv, core::inverse(camera.projection), core::inverse(camera.view));
        for (auto &mesh : model.meshes) {
          auto hit = traverse(mesh.instance, ray_data);
          if (hit.primitive_index != core::bvh::invalid_index) {
            _image->at(i, j) = core::vec4{
                ((hit.primitive_index * 8765 + 135) % 255) / 255.f,
                ((hit.primitive_index * 4 * 856 + 74334) % 255) / 255.f,
                ((hit.primitive_index * 2 * 879 + 86) % 255) / 255.f, 1.f};
          }
        }
      }
    }
  });
}

} // namespace photon
