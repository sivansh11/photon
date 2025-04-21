#include "photon/renderer.hpp"
#include "glm/ext/quaternion_common.hpp"
#include "glm/fwd.hpp"
#include "horizon/core/components.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/ecs.hpp"
#include "horizon/core/event.hpp"
#include "horizon/core/logger.hpp"
#include "horizon/core/math.hpp"
#include "horizon/core/model.hpp"
#include "horizon/gfx/base.hpp"
#include "horizon/gfx/context.hpp"
#include "horizon/gfx/helper.hpp"
#include "horizon/gfx/types.hpp"
#include "imgui.h"
#include <cstring>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace photon {

struct material_t {
  gfx::handle_bindless_image_t diffuse;
};

namespace shader {

struct camera_t {
  core::mat4 view;
  core::mat4 inv_view;
  core::mat4 projection;
  core::mat4 inv_projection;
};

struct mesh_t {
  core::vertex_t *vertices; // gpu pointer
  uint32_t *indices;        // gpu pointer
  core::mat4 *model;        // gpu pointer
  core::mat4 *inv_model;    // gpu pointer
  material_t material;
};

struct push_constant_t {
  mesh_t mesh;
  camera_t *camera; // gpu pointer
};

} // namespace shader

struct mesh_t {
  gfx::handle_buffer_t vertex_buffer;
  gfx::handle_buffer_t index_buffer;
  uint32_t vertex_count;
  uint32_t index_count;
  material_t material;
  gfx::handle_buffer_t model;
  gfx::handle_buffer_t inv_model;
  shader::mesh_t shader_mesh;
};

struct model_t {
  std::vector<mesh_t> meshes;
};

renderer_t::renderer_t(uint32_t width, uint32_t height,
                       core::ref<core::window_t> window,
                       core::ref<gfx::context_t> context,
                       core::ref<gfx::base_t> base,
                       core::ref<core::dispatcher_t> dispatcher,
                       const std::filesystem::path &photon_assets_path)
    : _width(width), _height(height), _window(window), _context(context),
      _base(base), _dispatcher(dispatcher),
      _photon_assets_path(photon_assets_path) {
  _dispatcher->subscribe<resize_event_t>([this](const core::event_t &event) {
    const resize_event_t &e = reinterpret_cast<const resize_event_t &>(event);
    _width = e.width;
    _height = e.height;
    _context->wait_idle();
    _context->destroy_image(_image);
    _context->destroy_image(_depth);
    _context->destroy_image_view(_image_view);
    _context->destroy_image_view(_depth_view);
    gfx::config_image_t ci{};
    ci.vk_width = _width;
    ci.vk_height = _height;
    ci.vk_depth = 1;
    ci.vk_type = VK_IMAGE_TYPE_2D;
    ci.vk_format = VK_FORMAT_R8G8B8A8_SRGB;
    ci.vk_usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.vma_allocation_create_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    ci.debug_name = "IMAGE";
    _image = _context->create_image(ci);
    _image_view = _context->create_image_view({.handle_image = _image});
    ci.vk_format = VK_FORMAT_D32_SFLOAT;
    ci.vk_usage = {};
    ci.vk_usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ci.debug_name = "DEPTH";
    _depth = _context->create_image(ci);
    _depth_view = _context->create_image_view({.handle_image = _depth});
  });
  gfx::config_image_t ci{};
  ci.vk_width = _width;
  ci.vk_height = _height;
  ci.vk_depth = 1;
  ci.vk_type = VK_IMAGE_TYPE_2D;
  ci.vk_format = VK_FORMAT_R8G8B8A8_SRGB;
  ci.vk_usage =
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ci.vma_allocation_create_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
  ci.debug_name = "IMAGE";
  _image = _context->create_image(ci);
  _image_view = _context->create_image_view({.handle_image = _image});
  ci.vk_format = VK_FORMAT_D32_SFLOAT;
  ci.vk_usage = {};
  ci.vk_usage =
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  ci.debug_name = "DEPTH";
  _depth = _context->create_image(ci);
  _depth_view = _context->create_image_view({.handle_image = _depth});

  gfx::config_pipeline_layout_t cpl{};
  cpl.add_descriptor_set_layout(_base->_bindless_descriptor_set_layout);
  cpl.add_push_constant(sizeof(shader::push_constant_t), VK_SHADER_STAGE_ALL);
  _debug_diffuse_pipeline_layout = context->create_pipeline_layout(cpl);

  gfx::config_pipeline_t cp{};
  cp.handle_pipeline_layout = _debug_diffuse_pipeline_layout;
  cp.add_color_attachment(VK_FORMAT_R8G8B8A8_SRGB,
                          gfx::default_color_blend_attachment());
  cp.set_depth_attachment(
      VK_FORMAT_D32_SFLOAT,
      VkPipelineDepthStencilStateCreateInfo{
          .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
          .depthTestEnable = VK_TRUE,
          .depthWriteEnable = VK_TRUE,
          .depthCompareOp = VK_COMPARE_OP_LESS,
          .depthBoundsTestEnable = VK_FALSE,
          .stencilTestEnable = VK_FALSE,
      });
  cp.add_shader(gfx::helper::create_slang_shader(
      *_context,
      _photon_assets_path.string() + "/shaders/debug_view/diffuse/vert.slang",
      gfx::shader_type_t::e_vertex));
  cp.add_shader(gfx::helper::create_slang_shader(
      *_context,
      _photon_assets_path.string() + "/shaders/debug_view/diffuse/frag.slang",
      gfx::shader_type_t::e_fragment));
  _debug_diffuse_pipeline = _context->create_graphics_pipeline(cp);

  gfx::config_buffer_t cb{};
  cb.vk_buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  cb.vma_allocation_create_flags =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  cb.vk_size = sizeof(shader::camera_t);
  _camera_buffer = _context->create_buffer(cb);
}

renderer_t::~renderer_t() {
  _context->wait_idle();
  _context->destroy_image(_image);
  _context->destroy_image(_depth);
  _context->destroy_image_view(_image_view);
  _context->destroy_image_view(_depth_view);
  _context->destroy_pipeline(_debug_diffuse_pipeline);
  _context->destroy_pipeline_layout(_debug_diffuse_pipeline_layout);
  _context->destroy_buffer(_camera_buffer);
}

gfx::handle_image_view_t renderer_t::render(core::ref<ecs::scene_t<>> scene,
                                            const core::camera_t &camera) {
  // prepare
  scene->for_all<core::raw_model_t>(
      [&](ecs::entity_id_t id, const core::raw_model_t &raw_model) {
        if (scene->has<model_t>(id))
          return;
        // upload model data to GPU
        model_t &model = scene->construct<model_t>(id);
        for (auto &raw_mesh : raw_model.meshes) {
          mesh_t mesh{};
          mesh.vertex_count = raw_mesh.vertices.size();
          mesh.index_count = raw_mesh.indices.size();

          gfx::config_buffer_t cb{};
          cb.vk_buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
          cb.vma_allocation_create_flags =
              VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

          // upload vertex and index
          cb.vk_size = raw_mesh.vertices.size() * sizeof(raw_mesh.vertices[0]);
          mesh.vertex_buffer = gfx::helper::create_buffer_staged(
              *_context, _base->_command_pool, cb, raw_mesh.vertices.data(),
              raw_mesh.vertices.size() * sizeof(raw_mesh.vertices[0]));

          cb.vk_size = raw_mesh.indices.size() * sizeof(raw_mesh.indices[0]);
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
                    _photon_assets_path.string() + "/textures/default.png",
                    VK_FORMAT_R8G8B8A8_SRGB);
            gfx::handle_image_view_t diffuse_image_view =
                _context->create_image_view({.handle_image = diffuse_image});
            mesh.material.diffuse = _base->new_bindless_image();
            _base->set_bindless_image(mesh.material.diffuse, diffuse_image_view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
          }

          mesh.shader_mesh.vertices = gfx::to<core::vertex_t *>(
              _context->get_buffer_device_address(mesh.vertex_buffer));
          mesh.shader_mesh.indices = gfx::to<uint32_t *>(
              _context->get_buffer_device_address(mesh.index_buffer));
          cb.vma_allocation_create_flags =
              VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
          cb.vk_size = sizeof(core::mat4);
          mesh.model = _context->create_buffer(cb);
          mesh.inv_model = _context->create_buffer(cb);
          mesh.shader_mesh.model = gfx::to<core::mat4 *>(
              _context->get_buffer_device_address(mesh.model));
          mesh.shader_mesh.inv_model = gfx::to<core::mat4 *>(
              _context->get_buffer_device_address(mesh.inv_model));
          mesh.shader_mesh.material = mesh.material;
          model.meshes.push_back(mesh);
        }
      });

  // draw
  auto cbuf = _base->current_commandbuffer();

  _context->cmd_image_memory_barrier(
      cbuf, _image, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  _context->cmd_image_memory_barrier(
      cbuf, _depth, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 0,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

  gfx::rendering_attachment_t color_rendering_attachment{};
  color_rendering_attachment.clear_value = {0, 0, 0, 0};
  color_rendering_attachment.image_layout =
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_rendering_attachment.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_rendering_attachment.store_op = VK_ATTACHMENT_STORE_OP_STORE;
  color_rendering_attachment.handle_image_view = _image_view;

  gfx::rendering_attachment_t depth_rendering_attachment{};
  depth_rendering_attachment.clear_value.depthStencil.depth = 1;
  depth_rendering_attachment.image_layout =
      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth_rendering_attachment.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth_rendering_attachment.store_op = VK_ATTACHMENT_STORE_OP_STORE;
  depth_rendering_attachment.handle_image_view = _depth_view;

  _context->cmd_begin_rendering(cbuf, {color_rendering_attachment},
                                depth_rendering_attachment,
                                VkRect2D{VkOffset2D{}, {_width, _height}});

  _context->cmd_bind_pipeline(cbuf, _debug_diffuse_pipeline);
  auto [viewport, scissor] =
      gfx::helper::fill_viewport_and_scissor_structs(_width, _height);
  _context->cmd_set_viewport_and_scissor(cbuf, viewport, scissor);
  _context->cmd_bind_descriptor_sets(cbuf, _debug_diffuse_pipeline, 0,
                                     {_base->_bindless_descriptor_set});
  shader::camera_t shader_camera{};
  shader_camera.view = camera.view;
  shader_camera.projection = camera.projection;
  shader_camera.inv_view = core::inverse(shader_camera.view);
  shader_camera.inv_projection = core::inverse(shader_camera.projection);
  std::memcpy(_context->map_buffer(_camera_buffer), &shader_camera,
              sizeof(shader::camera_t));

  shader::push_constant_t pc{};
  pc.camera = gfx::to<shader::camera_t *>(
      _context->get_buffer_device_address(_camera_buffer));

  scene->for_all<model_t, core::transform_t>(
      [&](auto, const model_t &model, const core::transform_t &transform) {
        for (auto &mesh : model.meshes) {
          pc.mesh = mesh.shader_mesh;
          core::mat4 model = transform.mat4();
          core::mat4 inv_model = core::inverse(model);
          // TODO: add this to context
          std::memcpy(_context->map_buffer(mesh.model), &model,
                      sizeof(core::mat4));
          std::memcpy(_context->map_buffer(mesh.inv_model), &inv_model,
                      sizeof(core::mat4));
          _context->cmd_push_constants(cbuf, _debug_diffuse_pipeline,
                                       VK_SHADER_STAGE_ALL, 0,
                                       sizeof(shader::push_constant_t), &pc);
          _context->cmd_draw(cbuf, mesh.index_count, 1, 0, 0);
        }
      });
  _context->cmd_end_rendering(cbuf);

  _context->cmd_image_memory_barrier(
      cbuf, _image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
  _context->cmd_image_memory_barrier(
      cbuf, _depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

  return _image_view;
}

void renderer_t::gui() {
  ImGui::Begin("Photon Settings");
  ImGui::End();
}

} // namespace photon
