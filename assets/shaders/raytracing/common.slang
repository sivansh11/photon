#include "core.slang"

// changes between frames and changes between bounces
struct current_raytracing_param_t {
  uint32_t num_rays;
};

struct push_constant_raytracing_t {
  uint32_t width;
  uint32_t height;
  ray_data_t *ray_data;              // ray_data_t[width * height]
  camera_t *camera;                  // camera_t
  current_raytracing_param_t *param; // current_raytracing_param_t
  bvh_t *tlas;                       // bvh_t
  uint32_t num_blas_instances;       // num_blas_instances
  bvh_instance_t *instances;         // instances
  hit_t *hits;                       // hit_t[width * height]
};
