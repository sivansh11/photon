#ifndef PHOTON_CPU_RENDERER_HPP
#define PHOTON_CPU_RENDERER_HPP

#include "horizon/core/core.hpp"
#include "horizon/core/ecs.hpp"
#include "horizon/core/event.hpp"

#include "photon/image.hpp"

#include <cstdint>

namespace photon {

class cpu_renderer_t {
public:
  // NOTE (sivansh): dispatcher will give new dimension updates, initial
  // dimensions must be given at creation
  cpu_renderer_t(uint32_t width, uint32_t height,
                 core::ref<core::dispatcher_t> dispatcher);
  ~cpu_renderer_t();

  void render(ecs::scene_t<> &scene /* TODO: add universal camera */);

  // TODO: maybe change this ?
  core::ref<image_t> image() { return _image; }

private:
  uint32_t _width, _height;
  core::ref<core::dispatcher_t> _dispatcher;
  core::ref<image_t> _image;
};

} // namespace photon

#endif // !PHOTON_CPU_RENDERER_HPP
