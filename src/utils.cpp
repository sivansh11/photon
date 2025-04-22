#include "photon/utils.hpp"

#include "horizon/core/model.hpp"
#include "horizon/gfx/context.hpp"
#include "horizon/gfx/helper.hpp"
#include "horizon/gfx/types.hpp"
#include "photon/types.hpp"
#include <algorithm>
#include <vulkan/vulkan_core.h>

namespace photon {

model_t raw_model_to_model(core::ref<gfx::base_t> base,
                           const std::filesystem::path &photon_assets_path,
                           const core::raw_model_t &raw_model) {
  model_t model{};

  for (auto &raw_mesh : raw_model.meshes) {
    mesh_t mesh{};

    gfx::config_buffer_t cb{};
    cb.vk_buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    cb.vma_allocation_create_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    // upload mesh
    mesh.vertex_count = raw_mesh.vertices.size();
    cb.vk_size = raw_mesh.vertices.size() * sizeof(raw_mesh.vertices[0]);
    mesh.vertex_buffer = gfx::helper::create_buffer_staged(
        base->_info.context, base->_command_pool, cb, raw_mesh.vertices.data(),
        cb.vk_size);

    mesh.index_count = raw_mesh.indices.size();
    cb.vk_size = raw_mesh.indices.size() * sizeof(raw_mesh.indices[0]);
    mesh.index_buffer = gfx::helper::create_buffer_staged(
        base->_info.context, base->_command_pool, cb, raw_mesh.indices.data(),
        cb.vk_size);

    // upload texture
    auto itr = std::find_if(raw_mesh.material_description.texture_infos.begin(),
                            raw_mesh.material_description.texture_infos.end(),
                            [](const core::texture_info_t info) {
                              return info.texture_type ==
                                     core::texture_type_t::e_diffuse_map;
                            });

    if (itr != raw_mesh.material_description.texture_infos.end()) {
      // TODO: handle image deletion
      mesh.material.diffuse = gfx::helper::load_image_from_path_instant(
          base->_info.context, base->_command_pool, itr->file_path,
          VK_FORMAT_R8G8B8A8_SRGB);
      mesh.material.diffuse_view = base->_info.context.create_image_view(
          {.handle_image = mesh.material.diffuse});
      mesh.material.diffuse_bindless = base->new_bindless_image();
      base->set_bindless_image(mesh.material.diffuse_bindless,
                               mesh.material.diffuse_view,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else {
      // TODO: cache default
      mesh.material.diffuse = gfx::helper::load_image_from_path_instant(
          base->_info.context, base->_command_pool,
          photon_assets_path.string() + "/textures/default.png",
          VK_FORMAT_R8G8B8A8_SRGB);
      mesh.material.diffuse_view = base->_info.context.create_image_view(
          {.handle_image = mesh.material.diffuse});
      mesh.material.diffuse_bindless = base->new_bindless_image();
      base->set_bindless_image(mesh.material.diffuse_bindless,
                               mesh.material.diffuse_view,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    cb.vma_allocation_create_flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    cb.vk_size = sizeof(core::mat4);
    mesh.model_buffer = base->_info.context.create_buffer(cb);
    mesh.inv_model_buffer = base->_info.context.create_buffer(cb);

    model.meshes.push_back(mesh);
  }

  return model;
}

} // namespace photon
