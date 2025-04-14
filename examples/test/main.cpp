#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "horizon/core/components.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/ecs.hpp"
#include "horizon/core/event.hpp"
#include "horizon/core/model.hpp"
#include "photon/cpu_renderer.hpp"

int main(int argc, char **argv) {
  core::ref<core::dispatcher_t> dispatcher =
      core::make_ref<core::dispatcher_t>();
  photon::cpu_renderer_t renderer{640, 420, dispatcher};

  ecs::scene_t<> scene;
  {
    auto id = scene.create();
    scene.construct<core::raw_model_t>(id) =
        core::load_model_from_path(argv[1]);
  }

  core::camera_t camera{};
  camera.view = core::lookAt(core::vec3{0, 1, 5}, core::vec3{0, 1, 0},
                             core::vec3{0, 1, 0});
  camera.projection = core::perspective(
      45.f, float(renderer.image()->width) / float(renderer.image()->height),
      0.0001f, 1000.0f);

  renderer.render(scene, camera);

  save_image_to_disk(*renderer.image(), "test.ppm");

  return 0;
}
