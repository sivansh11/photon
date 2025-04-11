#include "horizon/core/aabb.hpp"
#include "horizon/core/bvh.hpp"
#include "horizon/core/core.hpp"
#include "horizon/core/model.hpp"
#include "horizon/core/window.hpp"

#include "horizon/gfx/context.hpp"

#include "camera.hpp"
#include "image.hpp"
#include "traversal.hpp"

#include <vector>

int main(int argc, char **argv) {
  check(argc == 2, "vision [model path]");

  core::raw_model_t model = core::load_model_from_path(argv[1]);

  std::vector<core::aabb_t> aabbs;
  std::vector<core::vec3> centers;
  std::vector<bvh::triangle_t> triangles;

  for (auto mesh : model.meshes) {
    for (uint32_t i = 0; i < mesh.indices.size(); i += 3) {
      bvh::triangle_t triangle = {
          .v0 = mesh.vertices[mesh.indices[i + 0]].position,
          .v1 = mesh.vertices[mesh.indices[i + 1]].position,
          .v2 = mesh.vertices[mesh.indices[i + 2]].position,
      };
      core::vec3 center = (triangle.v0 + triangle.v1 + triangle.v2) / 3.0f;
      core::aabb_t aabb =
          core::aabb_t{}.grow(triangle.v0).grow(triangle.v1).grow(triangle.v2);
      aabbs.push_back(aabb);
      centers.push_back(center);
      triangles.push_back(triangle);
    }
  }

  core::bvh::options_t options = {
      .o_min_primitive_count = 1,
      .o_max_primitive_count = 1,
      .o_object_split_search_type =
          core::bvh::object_split_search_type_t::e_binned_sah,
      .o_primitive_intersection_cost = 1.1f,
      .o_node_intersection_cost = 1.f,
      .o_samples = 8,
  };

  core::bvh::bvh_t bvh = core::bvh::build_bvh2(aabbs.data(), centers.data(),
                                               triangles.size(), options);

  camera_t camera = camera_t::create(640, 420, -45, {0, 1, 5}, {0, 1, 0});

  image_t image{640, 420};

  for (uint32_t i = 0; i < 640; i++)
    for (uint32_t j = 0; j < 420; j++) {
      bvh::ray_data_t ray = camera.ray_gen(i, j);
      auto hit = bvh::traverse(bvh, ray, triangles.data());
      if (hit.primitive_index != core::bvh::invalid_index) {
        image.at(i, j) = core::vec4{
            ((hit.primitive_index * 8765 + 135) % 255) / 255.f,
            ((hit.primitive_index * 4 * 856 + 74334) % 255) / 255.f,
            ((hit.primitive_index * 2 * 879 + 86) % 255) / 255.f, 1.f};
      } else {
        image.at(i, j) = core::vec4{0, 0, 0, 0};
      }
    }

  save_image_to_disk(image, "test.ppm");

  return 0;
}
