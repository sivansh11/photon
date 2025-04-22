#ifndef PHOTON_TYPES_HPP
#define PHOTON_TYPES_HPP

#include "horizon/core/math.hpp"
#include "horizon/core/model.hpp"

#include "horizon/gfx/context.hpp"
#include "horizon/gfx/base.hpp"
#include "horizon/gfx/types.hpp"

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

struct mesh_t {
  gfx::handle_buffer_t vertex_buffer;
  gfx::handle_buffer_t index_buffer;
  gfx::handle_buffer_t model_buffer;
  gfx::handle_buffer_t inv_model_buffer;
  material_t material;
  uint32_t vertex_count;
  uint32_t index_count;
};

struct push_constant_t {
  core::vertex_t *vertices;
  uint32_t *indices;
  core::mat4 *model;
  core::mat4 *inv_model;
  uint32_t diffuse_bindless;
  camera_t *camera;
};

struct model_t {
  std::vector<mesh_t> meshes;
};

} // namespace photon

#endif // !PHOTON_TYPES_HPP
