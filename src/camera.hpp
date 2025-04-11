#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "horizon/core/math.hpp"
#include "traversal.hpp"

// TODO: put this in core
struct camera_t {
  static camera_t create(uint32_t width, uint32_t height, float vfov,
                         core::vec3 from, core::vec3 at,
                         core::vec3 up = core::vec3{0, 1, 0}) {
    camera_t camera{};
    camera.width = width;
    camera.height = height;
    camera.vfov = vfov;
    camera.from = from;
    camera.at = at;
    camera.up = up;

    float aspect_ratio = float(width) / float(height);
    float focal_length = core::length(from - at);
    float theta = core::radians(vfov);
    float h = tan(theta / 2.f);
    float viewport_height = 2.f * h * focal_length;
    float viewport_width = viewport_height * aspect_ratio;

    camera.w = core::normalize(from - at);
    camera.u = core::normalize(core::cross(up, camera.w));
    camera.v = core::cross(camera.w, camera.u);

    core::vec3 viewport_u = viewport_width * camera.u;
    core::vec3 viewport_v = viewport_height * -camera.v;

    camera.pixel_delta_u = viewport_u / float(width);
    camera.pixel_delta_v = viewport_v / float(height);

    core::vec3 viewport_upper_left =
        from - (focal_length * camera.w) - viewport_u / 2.f - viewport_v / 2.f;
    camera.pixel_00_loc = viewport_upper_left +
                          0.5f * (camera.pixel_delta_u + camera.pixel_delta_v);
    return camera;
  }

  bvh::ray_data_t ray_gen(uint32_t x, uint32_t y) {
    core::vec3 pixel_center =
        pixel_00_loc + (float(x) * pixel_delta_u) + (float(y) * pixel_delta_v);
    core::vec3 direction = pixel_center - from;
    return bvh::ray_data_t::create(bvh::ray_t::create(from, direction));
  }

  uint32_t width, height;
  float vfov;
  core::vec3 from, at, up;

private:
  core::vec3 pixel_00_loc, pixel_delta_u, pixel_delta_v, u, v, w;
};

#endif
