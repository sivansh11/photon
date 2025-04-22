#include "photon/renderer.hpp"
#include "photon/types.hpp"
#include "photon/utils.hpp"
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
    ci.vk_mips = 1;
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
  ci.vk_mips = 1;
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
  cpl.add_push_constant(sizeof(push_constant_raster_t), VK_SHADER_STAGE_ALL);
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
  cb.vk_size = sizeof(camera_t);
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
  scene->for_all<core::raw_model_t>([&](ecs::entity_id_t id,
                                        const core::raw_model_t &raw_model) {
    if (scene->has<model_t>(id))
      return;
    // upload model data to GPU
    scene->construct<model_t>(id) =
        std::move(raw_model_to_model(_base, _photon_assets_path, raw_model));
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
  camera_t shader_camera{};
  shader_camera.view = camera.view;
  shader_camera.projection = camera.projection;
  shader_camera.inv_view = core::inverse(shader_camera.view);
  shader_camera.inv_projection = core::inverse(shader_camera.projection);
  std::memcpy(_context->map_buffer(_camera_buffer), &shader_camera,
              sizeof(camera_t));

  push_constant_raster_t pc{};
  pc.camera =
      gfx::to<camera_t *>(_context->get_buffer_device_address(_camera_buffer));

  scene->for_all<model_t, core::transform_t>(
      [&](auto, const model_t &model, const core::transform_t &transform) {
        for (auto &mesh : model.meshes) {
          pc.vertices = gfx::to<core::vertex_t *>(
              _context->get_buffer_device_address(mesh.vertex_buffer));
          pc.indices = gfx::to<uint32_t *>(
              _context->get_buffer_device_address(mesh.index_buffer));
          pc.model = gfx::to<core::mat4 *>(
              _context->get_buffer_device_address(mesh.model_buffer));
          pc.inv_model = gfx::to<core::mat4 *>(
              _context->get_buffer_device_address(mesh.inv_model_buffer));
          pc.diffuse_bindless = mesh.material.diffuse_bindless.val;
          core::mat4 model = transform.mat4();
          core::mat4 inv_model = core::inverse(model);
          // TODO: add this to context
          std::memcpy(_context->map_buffer(mesh.model_buffer), &model,
                      sizeof(core::mat4));
          std::memcpy(_context->map_buffer(mesh.inv_model_buffer), &inv_model,
                      sizeof(core::mat4));
          _context->cmd_push_constants(cbuf, _debug_diffuse_pipeline,
                                       VK_SHADER_STAGE_ALL, 0,
                                       sizeof(push_constant_raster_t), &pc);
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
