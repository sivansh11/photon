#ifndef PHOTON_TYPES_HPP
#define PHOTON_TYPES_HPP

#include "horizon/core/aabb.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/math.hpp"
#include "horizon/core/model.hpp"
#include "horizon/core/bvh.hpp"

#include "horizon/gfx/context.hpp"
#include "horizon/gfx/base.hpp"
#include "horizon/gfx/types.hpp"
#include "imgui.h"

#include <vector>

namespace photon {

struct material_t {
  gfx::handle_image_t diffuse;
  gfx::handle_image_view_t diffuse_view;
  gfx::handle_bindless_image_t diffuse_bindless;
};

struct camera_t {
  core::mat4 view;
  core::mat4 inv_view;
  core::mat4 projection;
  core::mat4 inv_projection;
};

struct triangle_t {
  core::vec3 v0, v1, v2;
  core::aabb_t aabb() const {
    return core::aabb_t{}.grow(v0).grow(v1).grow(v2);
  }
  core::vec3 center() const { return (v0 + v1 + v2) / 3.f; }
};

/*
 * Not required
struct triangle_indices_t {
  uint32_t i0, i1, i2;
};
*/

struct mesh_t {
  gfx::handle_buffer_t vertex_buffer;
  gfx::handle_buffer_t index_buffer;

  // bvh
  gfx::handle_buffer_t nodes_buffer;
  /* bvh indices buffer
   * To get the bvh triangle, directly use index
   * To get the vertices, the indices are as follows
   * indices are as follows
   *    primitive_index * 3 + 0
   *    primitive_index * 3 + 1
   *    primitive_index * 3 + 2
   * */
  gfx::handle_buffer_t primitive_index_buffer;
  gfx::handle_buffer_t bvh_triangles_buffer;

  gfx::handle_buffer_t model_buffer;
  gfx::handle_buffer_t inv_model_buffer;
  material_t material;
  uint32_t vertex_count;
  uint32_t index_count;

  core::ref<gfx::context_t> context;

  mesh_t() { context = nullptr; }

  ~mesh_t() {
    if (context != nullptr) {
      context->destroy_buffer(vertex_buffer);
      context->destroy_buffer(index_buffer);
      context->destroy_buffer(nodes_buffer);
      context->destroy_buffer(primitive_index_buffer);
      context->destroy_buffer(bvh_triangles_buffer);
      context->destroy_buffer(model_buffer);
      context->destroy_buffer(inv_model_buffer);
      context->destroy_image_view(material.diffuse_view);
      context->destroy_image(material.diffuse);
    }
  }

  mesh_t(const mesh_t &) = delete;
  mesh_t &operator=(const mesh_t &) = delete;

  mesh_t(mesh_t &&other) {
    vertex_buffer = other.vertex_buffer;
    index_buffer = other.index_buffer;
    nodes_buffer = other.nodes_buffer;
    primitive_index_buffer = other.primitive_index_buffer;
    bvh_triangles_buffer = other.bvh_triangles_buffer;
    model_buffer = other.model_buffer;
    inv_model_buffer = other.inv_model_buffer;
    material = other.material;
    vertex_count = other.vertex_count;
    index_count = other.index_count;
    context = other.context;

    other.vertex_buffer = core::null_handle;
    other.index_buffer = core::null_handle;
    other.nodes_buffer = core::null_handle;
    other.primitive_index_buffer = core::null_handle;
    other.bvh_triangles_buffer = core::null_handle;
    other.model_buffer = core::null_handle;
    other.inv_model_buffer = core::null_handle;
    other.context = 0;
  }

  mesh_t &operator=(mesh_t &&other) {
    vertex_buffer = other.vertex_buffer;
    index_buffer = other.index_buffer;
    nodes_buffer = other.nodes_buffer;
    primitive_index_buffer = other.primitive_index_buffer;
    bvh_triangles_buffer = other.bvh_triangles_buffer;
    model_buffer = other.model_buffer;
    inv_model_buffer = other.inv_model_buffer;
    material = other.material;
    vertex_count = other.vertex_count;
    index_count = other.index_count;
    context = other.context;

    other.vertex_buffer = core::null_handle;
    other.index_buffer = core::null_handle;
    other.nodes_buffer = core::null_handle;
    other.primitive_index_buffer = core::null_handle;
    other.bvh_triangles_buffer = core::null_handle;
    other.model_buffer = core::null_handle;
    other.inv_model_buffer = core::null_handle;
    other.context = 0;

    return *this;
  }
};

struct push_constant_raster_t {
  core::vertex_t *vertices;
  uint32_t *indices;
  core::mat4 *model;
  core::mat4 *inv_model;
  uint32_t diffuse_bindless;
  camera_t *camera;
};

struct ray_data_t {
  core::vec3 origin, direction;
  core::vec3 inv_direction;
  float tmin, tmax;
  uint32_t pixel_index;
};

// changes between frames and changes between bounces
struct current_raytracing_param_t {
  uint32_t num_rays;
};

struct bvh_t {
  core::bvh::node_t *nodes;
  uint32_t *primitive_indices;
};

struct bvh_instance_t {
  core::vertex_t *vertices;
  uint32_t *indices;

  // bvh
  core::bvh::node_t *nodes;
  /* bvh indices buffer
   * To get the bvh triangle, directly use index
   * To get the vertices, the indices are as follows
   * indices in normal index buffer are as follows
   *    primitive_index * 3 + 0
   *    primitive_index * 3 + 1
   *    primitive_index * 3 + 2
   * */
  uint32_t *primitive_indices;
  triangle_t *bvh_triangles;

  core::mat4 *model;
  core::mat4 *inv_model;

  gfx::handle_bindless_image_t diffuse_bindless;
};

struct hit_t {
  bool did_intersect() { return primitive_index != core::bvh::invalid_index; }
  uint32_t blas_index = core::bvh::invalid_index;
  uint32_t primitive_index = core::bvh::invalid_index;
  float t = core::infinity;
  float u = 0, v = 0, w = 0;
};

struct push_constant_raytracing_t {
  uint32_t width;
  uint32_t height;
  ray_data_t *ray_data;              // ray_data_t[width * height]
  camera_t *camera;                  // camera_t
  current_raytracing_param_t *param; // current_raytracing_param_t
  bvh_t *tlas;                       // bvh_t
  uint32_t num_blas_instances;       //
  bvh_instance_t *instances;         //
  hit_t *hits;                       // hit_t[width * height]
};

struct model_t {
  std::vector<mesh_t> meshes;
};

} // namespace photon

#endif // !PHOTON_TYPES_HPP
