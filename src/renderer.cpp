#include "photon/renderer.hpp"
#include "horizon/core/bvh.hpp"
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
#include <cassert>
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
    _context->destroy_image(_raytrace_image);
    _context->destroy_image_view(_image_view);
    _context->destroy_image_view(_depth_view);
    _context->destroy_image_view(_raytrace_image_view);
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
    ci.vk_format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.vk_usage = {};
    ci.vk_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    ci.debug_name = "RAYTRACE_IMAGE";
    _raytrace_image = _context->create_image(ci);
    _raytrace_image_view =
        _context->create_image_view({.handle_image = _raytrace_image});
    _base->set_bindless_storage_image(0, _raytrace_image_view);

    _context->destroy_buffer(_ray_data_buffer);
    gfx::config_buffer_t cb{};
    cb.vk_buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    cb.vma_allocation_create_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    cb.vk_size = sizeof(ray_data_t) * _width * _height;
    _ray_data_buffer = _context->create_buffer(cb);
    cb.vk_size = sizeof(hit_t) * _width * _height *
                 1.75; // overallocating for debug data
    _hits_buffer = _context->create_buffer(cb);
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
  ci.vk_format = VK_FORMAT_R8G8B8A8_UNORM;
  ci.vk_usage = {};
  ci.vk_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
  ci.debug_name = "RAYTRACE_IMAGE";
  _raytrace_image = _context->create_image(ci);
  _raytrace_image_view =
      _context->create_image_view({.handle_image = _raytrace_image});
  assert(_base->new_bindless_storage_image().val == 0);
  _base->set_bindless_storage_image(0, _raytrace_image_view);

  { // _debug_diffuse_pipeline
    gfx::config_pipeline_layout_t cpl{};
    cpl.add_descriptor_set_layout(_base->_bindless_descriptor_set_layout);
    cpl.add_push_constant(sizeof(push_constant_raster_t), VK_SHADER_STAGE_ALL);
    _debug_diffuse_pipeline_layout = context->create_pipeline_layout(cpl);

    gfx::config_pipeline_t cp{};
    cp.debug_name = "_debug_diffuse_pipeline";
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
  }

  { // _raygen_pipeline
    gfx::config_pipeline_layout_t cpl{};
    cpl.add_descriptor_set_layout(_base->_bindless_descriptor_set_layout);
    cpl.add_push_constant(sizeof(push_constant_raytracing_t),
                          VK_SHADER_STAGE_ALL);
    _raygen_pipeline_layout = context->create_pipeline_layout(cpl);

    gfx::config_pipeline_t cp{};
    cp.debug_name = "_raygen_pipeline";
    cp.handle_pipeline_layout = _raygen_pipeline_layout;
    cp.add_shader(gfx::helper::create_slang_shader(
        *_context,
        _photon_assets_path.string() + "/shaders/raytracing/raygen.slang",
        gfx::shader_type_t::e_compute));
    _raygen_pipeline = _context->create_compute_pipeline(cp);
  }

  { // _trace_pipeline
    gfx::config_pipeline_layout_t cpl{};
    cpl.add_descriptor_set_layout(_base->_bindless_descriptor_set_layout);
    cpl.add_push_constant(sizeof(push_constant_raytracing_t),
                          VK_SHADER_STAGE_ALL);
    _trace_pipeline_layout = context->create_pipeline_layout(cpl);

    gfx::config_pipeline_t cp{};
    cp.debug_name = "_trace_pipeline";
    cp.handle_pipeline_layout = _trace_pipeline_layout;
    cp.add_shader(gfx::helper::create_slang_shader(
        *_context,
        _photon_assets_path.string() + "/shaders/raytracing/trace.slang",
        gfx::shader_type_t::e_compute));
    _trace_pipeline = _context->create_compute_pipeline(cp);
  }

  { // _shade_pipeline
    gfx::config_pipeline_layout_t cpl{};
    cpl.add_descriptor_set_layout(_base->_bindless_descriptor_set_layout);
    cpl.add_push_constant(sizeof(push_constant_raytracing_t),
                          VK_SHADER_STAGE_ALL);
    _shade_pipeline_layout = context->create_pipeline_layout(cpl);

    gfx::config_pipeline_t cp{};
    cp.debug_name = "_shade_pipeline";
    cp.handle_pipeline_layout = _shade_pipeline_layout;
    cp.add_shader(gfx::helper::create_slang_shader(
        *_context,
        _photon_assets_path.string() + "/shaders/raytracing/shade.slang",
        gfx::shader_type_t::e_compute));
    _shade_pipeline = _context->create_compute_pipeline(cp);
  }

  gfx::config_buffer_t cb{};
  cb.vk_buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  cb.vma_allocation_create_flags =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  cb.vk_size = sizeof(camera_t);
  _camera_buffer = _context->create_buffer(cb);
  cb.vma_allocation_create_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
  cb.vk_size = sizeof(current_raytracing_param_t);
  _param_buffer = _context->create_buffer(cb);
  cb.vk_size = sizeof(ray_data_t) * _width * _height;
  _ray_data_buffer = _context->create_buffer(cb);
  cb.vk_size =
      sizeof(hit_t) * _width * _height * 1.75f; // overallocating for debug data
  _hits_buffer = _context->create_buffer(cb);

  _gpu_timer = core::make_ref<gpu_timer_t>(*_base, true);
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
    _update_instances_buffer = true;
    // upload model data to GPU
    scene->construct<model_t>(id) =
        std::move(raw_model_to_model(_base, _photon_assets_path, raw_model));
  });

  if (_update_instances_buffer) {
    _update_instances_buffer = false;
    std::vector<bvh_instance_t> instances{};
    scene->for_all<model_t>([&](auto, const model_t &model) {
      for (const auto &mesh : model.meshes) {

        // struct bvh_instance_t {
        //   core::vertex_t *vertices;
        //   uint32_t *indices;
        //
        //   // bvh
        //   core::bvh::node_t *nodes;
        //   /* bvh indices buffer
        //    * To get the bvh triangle, directly use index
        //    * To get the vertices, the indices are as follows
        //    * indices in normal index buffer are as follows
        //    *    primitive_index * 3 + 0
        //    *    primitive_index * 3 + 1
        //    *    primitive_index * 3 + 2
        //    * */
        //   uint32_t *primitive_indices;
        //   triangle_t *bvh_triangles;
        //
        //   core::mat4 *model;
        //   core::mat4 inv_model;
        //
        //   gfx::handle_bindless_image_t diffuse_bindless;
        // };
        bvh_instance_t instance{};
        instance.vertices = gfx::to<core::vertex_t *>(
            _context->get_buffer_device_address(mesh.vertex_buffer));
        instance.indices = gfx::to<uint32_t *>(
            _context->get_buffer_device_address(mesh.index_buffer));
        instance.nodes = gfx::to<core::bvh::node_t *>(
            _context->get_buffer_device_address(mesh.nodes_buffer));
        instance.primitive_indices = gfx::to<uint32_t *>(
            _context->get_buffer_device_address(mesh.primitive_index_buffer));
        instance.bvh_triangles = gfx::to<triangle_t *>(
            _context->get_buffer_device_address(mesh.bvh_triangles_buffer));
        instance.model = gfx::to<core::mat4 *>(
            _context->get_buffer_device_address(mesh.model_buffer));
        instance.inv_model = gfx::to<core::mat4 *>(
            _context->get_buffer_device_address(mesh.inv_model_buffer));

        instances.push_back(instance);
      }
    });
    if (_instances_buffer != core::null_handle) {
      _context->destroy_buffer(_instances_buffer);
    }

    _num_blas_instances = instances.size();

    gfx::config_buffer_t cb{};
    cb.vk_size = instances.size() * sizeof(instances[0]);
    cb.vk_buffer_usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    cb.vma_allocation_create_flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    _instances_buffer = gfx::helper::create_buffer_staged(
        *_context, _base->_command_pool, cb, instances.data(), cb.vk_size);
  }

  // draw
  auto cbuf = _base->current_commandbuffer();

  camera_t shader_camera{};
  shader_camera.view = camera.view;
  shader_camera.projection = camera.projection;
  shader_camera.inv_view = core::inverse(shader_camera.view);
  shader_camera.inv_projection = core::inverse(shader_camera.projection);
  std::memcpy(_context->map_buffer(_camera_buffer), &shader_camera,
              sizeof(camera_t));

  if (false) {
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
    push_constant_raster_t pc{};
    pc.camera = gfx::to<camera_t *>(
        _context->get_buffer_device_address(_camera_buffer));

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
  }

  // raytracing pass
  {
    _context->cmd_image_memory_barrier(
        cbuf, _raytrace_image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // uint32_t width;
    // uint32_t height;
    // ray_data_t *ray_data;              // ray_data_t[width * height]
    // camera_t *camera;                  // camera_t
    // current_raytracing_param_t *param; // current_raytracing_param_t
    // bvh_t *tlas;                       // bvh_t
    // uint32_t num_blas_instances;       //
    // bvh_instance_t *instances;         //
    // hit_t *hits;                       // hit_t[width * height]
    push_constant_raytracing_t pc{};
    pc.width = _width;
    pc.height = _height;
    pc.ray_data = gfx::to<ray_data_t *>(
        _context->get_buffer_device_address(_ray_data_buffer));
    pc.camera = gfx::to<camera_t *>(
        _context->get_buffer_device_address(_camera_buffer));
    pc.param = gfx::to<current_raytracing_param_t *>(
        _context->get_buffer_device_address(_param_buffer));
    pc.tlas = 0; // NOTE: DONT USE
    pc.num_blas_instances = _num_blas_instances;
    pc.instances = gfx::to<bvh_instance_t *>(
        _context->get_buffer_device_address(_instances_buffer));
    pc.hits =
        gfx::to<hit_t *>(_context->get_buffer_device_address(_hits_buffer));

    _gpu_timer->start(cbuf, "raygen");
    _context->cmd_bind_pipeline(cbuf, _raygen_pipeline);
    _context->cmd_bind_descriptor_sets(cbuf, _raygen_pipeline, 0,
                                       {_base->_bindless_descriptor_set});
    _context->cmd_push_constants(cbuf, _raygen_pipeline, VK_SHADER_STAGE_ALL, 0,
                                 sizeof(push_constant_raytracing_t), &pc);
    _context->cmd_dispatch(cbuf, (_width + 8 - 1) / 8, (_height + 8 - 1) / 8,
                           1);
    _gpu_timer->end(cbuf, "raygen");
    _context->cmd_buffer_memory_barrier(
        cbuf, _ray_data_buffer,
        _context->get_buffer(_ray_data_buffer).config.vk_size, 0,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    _context->cmd_buffer_memory_barrier(
        cbuf, _param_buffer, _context->get_buffer(_param_buffer).config.vk_size,
        0, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    _context->cmd_image_memory_barrier(
        cbuf, _raytrace_image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    _gpu_timer->start(cbuf, "trace");
    _context->cmd_bind_pipeline(cbuf, _trace_pipeline);
    _context->cmd_bind_descriptor_sets(cbuf, _trace_pipeline, 0,
                                       {_base->_bindless_descriptor_set});
    _context->cmd_push_constants(cbuf, _trace_pipeline, VK_SHADER_STAGE_ALL, 0,
                                 sizeof(push_constant_raytracing_t), &pc);
    _context->cmd_dispatch(cbuf, (_width * _height + 64 - 1) / 64, 1, 1);
    _gpu_timer->end(cbuf, "trace");
    _context->cmd_buffer_memory_barrier(
        cbuf, _hits_buffer, _context->get_buffer(_hits_buffer).config.vk_size,
        0, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    _gpu_timer->start(cbuf, "shade");
    _context->cmd_bind_pipeline(cbuf, _shade_pipeline);
    _context->cmd_bind_descriptor_sets(cbuf, _shade_pipeline, 0,
                                       {_base->_bindless_descriptor_set});
    _context->cmd_push_constants(cbuf, _shade_pipeline, VK_SHADER_STAGE_ALL, 0,
                                 sizeof(push_constant_raytracing_t), &pc);
    _context->cmd_dispatch(cbuf, (_width * _height + 64 - 1) / 64, 1, 1);
    _gpu_timer->end(cbuf, "shade");
    _context->cmd_image_memory_barrier(
        cbuf, _raytrace_image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    _context->cmd_image_memory_barrier(
        cbuf, _raytrace_image, VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  }

  return _raytrace_image_view;
}

void renderer_t::gui() {
  ImGui::Begin("Photon Settings");
  ImGui::Text("%f", ImGui::GetIO().Framerate);
  ImGui::Text("%f %f", float(_width), float(_height));
  for (auto [name, time] : _gpu_timer->get_times()) {
    ImGui::Text("%s took %fms", name.c_str(), time);
  }
  ImGui::End();
}

} // namespace photon
