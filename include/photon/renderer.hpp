#ifndef PHOTON_RENDERER_HPP
#define PHOTON_RENDERER_HPP

#include "horizon/core/components.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/ecs.hpp"
#include "horizon/core/event.hpp"
#include "horizon/core/window.hpp"

#include "horizon/gfx/base.hpp"
#include "horizon/gfx/context.hpp"

#include <cstdint>

namespace photon {

class renderer_t {
public:
  renderer_t(uint32_t width, uint32_t height, core::ref<core::window_t> window,
             core::ref<gfx::context_t> context, core::ref<gfx::base_t> base,
             core::ref<core::dispatcher_t> dispatcher);
  ~renderer_t();

  // TODO: figure out what to return ?
  void render(core::ref<ecs::scene_t<>> scene, const core::camera_t &camera);

private:
  uint32_t _width, _height;

  core::ref<core::window_t> _window;
  core::ref<core::dispatcher_t> _dispatcher;

  core::ref<gfx::context_t> _context;
  core::ref<gfx::base_t> _base;
};

} // namespace photon

#endif // !PHOTON_RENDERER_HPP
