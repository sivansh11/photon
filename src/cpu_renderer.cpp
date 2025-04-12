#include "photon/cpu_renderer.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/ecs.hpp"
#include "photon/image.hpp"

namespace photon {

cpu_renderer_t::cpu_renderer_t(uint32_t width, uint32_t height,
                               core::ref<core::dispatcher_t> dispatcher) {
  _image = core::make_ref<image_t>(width, height);
}

cpu_renderer_t::~cpu_renderer_t() {}

void cpu_renderer_t::render(ecs::scene_t<> &scene) {
  for (uint32_t i = 0; i < _image->width; i++) {
    for (uint32_t j = 0; j < _image->height; j++) {
      _image->at(i, j) = core::vec4{1, 1, 0, 1};
    }
  }
}

} // namespace photon
