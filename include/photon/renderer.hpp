#ifndef PHOTON_RENDERER_HPP
#define PHOTON_RENDERER_HPP

#include "horizon/core/components.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/ecs.hpp"
#include "horizon/core/event.hpp"
#include "horizon/core/window.hpp"

#include "horizon/gfx/base.hpp"
#include "horizon/gfx/context.hpp"
#include "horizon/gfx/types.hpp"

#include <cstdint>
#include <filesystem>

namespace photon {

struct gpu_timer_t {
  gpu_timer_t(gfx::base_t &base, bool enable) : _base(base), enable(enable) {}

  void clear() {
    _base._context->wait_idle();
    for (auto [name, handle] : timers) {
      _base._context->destroy_timer(handle);
    }
    timers.clear();
  }

  void start(gfx::handle_commandbuffer_t cbuf, std::string name) {
    if (!enable)
      return;
    if (!timers.contains(name)) {
      timers[name] = _base._context->create_timer({});
    }
    _base._context->cmd_begin_timer(cbuf, timers[name]);
  }

  void end(gfx::handle_commandbuffer_t cbuf, std::string name) {
    if (!enable)
      return;
    _base._context->cmd_end_timer(cbuf, timers[name]);
  }

  std::map<std::string, float> get_times() {
    if (!enable)
      return {};
    std::map<std::string, float> res{};
    for (auto [name, handle] : timers) {
      auto time = _base._context->timer_get_time(handle);
      if (time)
        res[name] = *time;
    }
    return res;
  }

  gfx::base_t &_base;
  bool enable;
  std::map<std::string, gfx::handle_timer_t> timers{};
};

class renderer_t {
public:
  renderer_t(uint32_t width, uint32_t height, core::ref<core::window_t> window,
             core::ref<gfx::context_t> context, core::ref<gfx::base_t> base,
             core::ref<core::dispatcher_t> dispatcher,
             const std::filesystem::path &photon_assets_path);
  ~renderer_t();

  gfx::handle_image_view_t render(core::ref<ecs::scene_t<>> scene,
                                  const core::camera_t &camera);

  uint32_t width() { return _width; }
  uint32_t height() { return _height; }

  void gui();

private:
  const std::filesystem::path _photon_assets_path;
  uint32_t _width, _height;

  core::ref<core::window_t> _window;
  core::ref<core::dispatcher_t> _dispatcher;

  core::ref<gfx::context_t> _context;
  core::ref<gfx::base_t> _base;

  gfx::handle_image_t _image;
  gfx::handle_image_view_t _image_view;
  gfx::handle_image_t _depth;
  gfx::handle_image_view_t _depth_view;
  gfx::handle_image_t _raytrace_image;
  gfx::handle_image_view_t _raytrace_image_view;

  gfx::handle_pipeline_layout_t _debug_diffuse_pipeline_layout;
  gfx::handle_pipeline_t _debug_diffuse_pipeline;

  gfx::handle_pipeline_layout_t _raygen_pipeline_layout;
  gfx::handle_pipeline_t _raygen_pipeline;

  gfx::handle_pipeline_layout_t _trace_pipeline_layout;
  gfx::handle_pipeline_t _trace_pipeline;

  gfx::handle_pipeline_layout_t _shade_pipeline_layout;
  gfx::handle_pipeline_t _shade_pipeline;

  gfx::handle_buffer_t _camera_buffer;
  gfx::handle_buffer_t _param_buffer;
  gfx::handle_buffer_t _ray_data_buffer;
  gfx::handle_buffer_t _hits_buffer;
  gfx::handle_buffer_t _tlas_buffer;
  gfx::handle_buffer_t _instances_buffer = core::null_handle;

  uint32_t _num_blas_instances;

  bool _update_instances_buffer = false;

  core::ref<gpu_timer_t> _gpu_timer;
};

struct resize_event_t : public core::event_t {
  uint32_t width, height;
};

} // namespace photon

#endif // !PHOTON_RENDERER_HPP
