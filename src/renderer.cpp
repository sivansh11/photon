#include "photon/renderer.hpp"
#include "horizon/core/components.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/ecs.hpp"
#include "horizon/core/model.hpp"
#include "horizon/gfx/base.hpp"
#include "horizon/gfx/context.hpp"
#include "horizon/gfx/helper.hpp"
#include "horizon/gfx/types.hpp"
#include <vector>
#include <vulkan/vulkan_core.h>

namespace photon {

struct material_t {
  gfx::handle_bindless_image_t diffuse;
};

struct mesh_t {
  gfx::handle_buffer_t vertex_buffer;
  gfx::handle_buffer_t index_buffer;
  uint32_t vertex_count;
  uint32_t index_count;
  material_t material;
};

struct model_t {
  std::vector<mesh_t> meshes;
};

renderer_t::renderer_t(uint32_t width, uint32_t height,
                       core::ref<core::window_t> window,
                       core::ref<gfx::context_t> context,
                       core::ref<gfx::base_t> base,
                       core::ref<core::dispatcher_t> dispatcher)
    : _width(width), _height(height), _window(window), _context(context),
      _base(base), _dispatcher(dispatcher) {}

renderer_t::~renderer_t() {}

void renderer_t::render(core::ref<ecs::scene_t<>> scene,
                        const core::camera_t &camera) {
  // prepare
  scene->for_all<core::raw_model_t>([&](ecs::entity_id_t id,
                                        const core::raw_model_t &raw_model) {
    if (scene->has<model_t>(id))
      return;
    // upload model data to GPU
    model_t &model = scene->construct<model_t>(id);
    for (auto &raw_mesh : raw_model.meshes) {
      mesh_t mesh{};
      mesh.vertex_count = raw_mesh.vertices.size();
      mesh.index_count = raw_mesh.indices.size();

      gfx::config_buffer_t cb{};

      // upload vertex and index
      cb.vk_size = raw_mesh.vertices.size() * sizeof(raw_mesh.vertices[0]);
      cb.vk_buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      mesh.vertex_buffer = gfx::helper::create_buffer_staged(
          *_context, _base->_command_pool, cb, raw_mesh.vertices.data(),
          raw_mesh.vertices.size() * sizeof(raw_mesh.vertices[0]));

      cb.vk_size = raw_mesh.indices.size() * sizeof(raw_mesh.indices[0]);
      cb.vk_buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      mesh.index_buffer = gfx::helper::create_buffer_staged(
          *_context, _base->_command_pool, cb, raw_mesh.indices.data(),
          raw_mesh.indices.size() * sizeof(raw_mesh.indices[0]));

      // upload texture
      auto itr =
          std::find_if(raw_mesh.material_description.texture_infos.begin(),
                       raw_mesh.material_description.texture_infos.end(),
                       [](const core::texture_info_t &texture_info) {
                         return texture_info.texture_type ==
                                core::texture_type_t::e_diffuse_map;
                       });
      if (itr != raw_mesh.material_description.texture_infos.end()) {
        // TODO: handle deletion, maybe set it in the material struct as it
        // is, and add a custom deletor
        gfx::handle_image_t diffuse_image =
            gfx::helper::load_image_from_path_instant(
                *_context, _base->_command_pool, itr->file_path,
                VK_FORMAT_R8G8B8A8_SRGB);
        gfx::handle_image_view_t diffuse_image_view =
            _context->create_image_view({.handle_image = diffuse_image});
        mesh.material.diffuse = _base->new_bindless_image();
        _base->set_bindless_image(mesh.material.diffuse, diffuse_image_view,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      } else {
        // default texture
        // TODO: cache default texture
        gfx::handle_image_t diffuse_image =
            gfx::helper::load_image_from_path_instant(
                *_context, _base->_command_pool,
                "../../assets/textures/default.png", VK_FORMAT_R8G8B8A8_SRGB);
        gfx::handle_image_view_t diffuse_image_view =
            _context->create_image_view({.handle_image = diffuse_image});
        mesh.material.diffuse = _base->new_bindless_image();
        _base->set_bindless_image(mesh.material.diffuse, diffuse_image_view,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }
      model.meshes.push_back(mesh);
    }
  });

  // draw
}

} // namespace photon
