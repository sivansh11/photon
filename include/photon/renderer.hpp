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

class renderer_t {
public:
  renderer_t(uint32_t width, uint32_t height, core::ref<core::window_t> window,
             core::ref<gfx::context_t> context, core::ref<gfx::base_t> base,
             core::ref<core::dispatcher_t> dispatcher,
             const std::filesystem::path &photon_assets_path);
  ~renderer_t();

  gfx::handle_image_view_t render(core::ref<ecs::scene_t<>> scene,
                                  const core::camera_t &camera);

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

  gfx::handle_pipeline_layout_t _debug_diffuse_pipeline_layout;
  gfx::handle_pipeline_t _debug_diffuse_pipeline;

  gfx::handle_buffer_t _camera_buffer;
};

struct resize_event_t : public core::event_t {
  uint32_t width, height;
};

} // namespace photon

#endif // !PHOTON_RENDERER_HPP
