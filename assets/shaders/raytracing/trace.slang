#include "common.slang"

[vk::push_constant]
push_constant_raytracing_t pc;

public static const uint32_t STACK_SIZE = 16;
static groupshared uint32_t stack[64][STACK_SIZE];

hit_t intersect_blas(const node_t *nodes, const uint32_t *primitive_indices,
                     ray_data_t ray, triangle_t *p_triangles,
                     uint32_t group_index) {
  hit_t hit;
  hit.primitive_index = invalid_index;

  uint32_t stack_top = 0;

  hit.node_intersection_count++;

  node_t root = nodes[0];
  if (!aabb_intersect(ray, root.aabb).did_intersect())
    return hit;

  if (bool(root.is_leaf)) {
    for (uint32_t i = 0; i < root.primitive_count; i++) {
      uint32_t primitive_index =
          primitive_indices[root.first_primitive_index_or_child_index + i];
      triangle_t triangle = p_triangles[primitive_index];
      triangle_intersection_t intersection = triangle_intersect(ray, triangle);
      if (intersection.did_intersect()) {
        ray.tmax = intersection.t;
        hit.primitive_index = primitive_index;
        hit.t = intersection.t;
        hit.u = intersection.u;
        hit.v = intersection.v;
        hit.w = intersection.w;
      }
    }
    return hit;
  }

  uint32_t current = 1;
  while (true) {
    const node_t left = nodes[current];
    const node_t right = nodes[current + 1];

    hit.node_intersection_count++;
    aabb_intersection_t left_intersect = aabb_intersect(ray, left.aabb);
    aabb_intersection_t right_intersect = aabb_intersect(ray, right.aabb);

    uint32_t start = 0;
    uint32_t end = 0;
    if (left_intersect.did_intersect() && bool(left.is_leaf)) {
      if (right_intersect.did_intersect() && bool(right.is_leaf)) {
        start = left.first_primitive_index_or_child_index;
        end =
            right.first_primitive_index_or_child_index + right.primitive_count;
      } else {
        start = left.first_primitive_index_or_child_index;
        end = left.first_primitive_index_or_child_index + left.primitive_count;
      }
    } else if (right_intersect.did_intersect() && bool(right.is_leaf)) {
      start = right.first_primitive_index_or_child_index;
      end = right.first_primitive_index_or_child_index + right.primitive_count;
    }
    for (uint32_t i = start; i < end; i++) {
      hit.primitive_intersection_count++;
      const uint32_t primitive_index = primitive_indices[i];
      triangle_t triangle = p_triangles[primitive_index];
      triangle_intersection_t intersection = triangle_intersect(ray, triangle);
      if (intersection.did_intersect()) {
        ray.tmax = intersection.t;
        hit.primitive_index = primitive_index;
        hit.t = intersection.t;
        hit.u = intersection.u;
        hit.v = intersection.v;
        hit.w = intersection.w;
      }
    }

    if (left_intersect.did_intersect() && !bool(left.is_leaf)) {
      if (right_intersect.did_intersect() && !bool(right.is_leaf)) {
        if (stack_top >= STACK_SIZE)
          return hit; // TODO: maybe raise an error ?
        if (left_intersect.tmin <= right_intersect.tmin) {
          current = left.first_primitive_index_or_child_index;
          // stack[stack_top++] =
          // right.first_primitive_index_or_child_index;
          stack[group_index][stack_top++] =
              right.first_primitive_index_or_child_index;
        } else {
          current = right.first_primitive_index_or_child_index;
          // stack[stack_top++] =
          // left.first_primitive_index_or_child_index;
          stack[group_index][stack_top++] =
              left.first_primitive_index_or_child_index;
        }
      } else {
        current = left.first_primitive_index_or_child_index;
      }
    } else {
      if (right_intersect.did_intersect() && !bool(right.is_leaf)) {
        current = right.first_primitive_index_or_child_index;
      } else {
        if (stack_top == 0)
          return hit;
        // current = stack[--stack_top];
        current = stack[group_index][--stack_top];
      }
    }
  }
  return hit;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void compute_main(const uint3 dispatch_thread_id: SV_DispatchThreadID,
                  const uint group_index: SV_GroupIndex) {
  uint32_t index = dispatch_thread_id.x;

  if (index >= pc.param.num_rays)
    return;
  // TODO: remove this as this should never be true
  if (index >= pc.width * pc.height)
    return;

  ray_data_t ray_data = pc.ray_data[index];
  // hit_t hit = intersect(*pc.bvh, ray_data, pc.triangles, group_index);
  hit_t tlas_hit;
  for (uint32_t i = 0; i < pc.num_blas_instances; i++) {
    hit_t blas_hit =
        intersect_blas(pc.instances[i].nodes, pc.instances[i].primitive_indices,
                       ray_data, pc.instances[i].bvh_triangles, group_index);
    if (blas_hit.t < tlas_hit.t) {
      tlas_hit = blas_hit;
      tlas_hit.blas_index = i;
    }
  }
  pc.hits[index] = tlas_hit;
}
