#include "horizon/core/core.hpp"
#include "horizon/core/ecs.hpp"
#include "horizon/core/event.hpp"
#include "photon/cpu_renderer.hpp"
#include "photon/image.hpp"

int main(int argc, char **argv) {
  core::ref<core::dispatcher_t> dispatcher =
      core::make_ref<core::dispatcher_t>();
  photon::cpu_renderer_t renderer{640, 420, dispatcher};

  ecs::scene_t<> scene;

  renderer.render(scene);

  save_image_to_disk(*renderer.image(), "test.ppm");

  return 0;
}
