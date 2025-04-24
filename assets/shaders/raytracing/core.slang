public static const float epsilon = 0.0001;
public static const float infinity = 100000000000000.f;

public static const uint32_t invalid_index = 4294967295u;

extension float3 { __init(float4 v) { this.x = v.x;
this.y = v.y;
this.z = v.z;
}
};

float4 operator *(float4x4 m, float4 v) { return mul(m, v); }
float4 operator *(float4 v, float4x4 m) { return mul(v, m); }
float safe_inverse(float x) {
  static const float epsilon = epsilon;
  if (abs(x) <= epsilon)
    return x >= 0 ? 1.f / epsilon : -1.f / epsilon;
  return 1.f / x;
}

struct vertex_t {
  float3 position;
  float3 normal;
  float2 uv;
  float3 tangent;
  float3 bi_tangent;
};

struct camera_t {
  float4x4 view;
  float4x4 inv_view;
  float4x4 projection;
  float4x4 inv_projection;
};

struct triangle_t {
  float3 v0, v1, v2;
};

struct aabb_t {
  float3 min;
  float3 max;
};

struct node_t {
  aabb_t aabb;
  uint32_t is_leaf : 1;
  uint32_t primitive_count : 31;
  uint32_t first_primitive_index_or_child_index : 28;
  uint32_t children_count : 4; // do not use
};

struct bvh_t {
  node_t *nodes;
  uint32_t *primitive_indices;
};

struct ray_data_t {
  static ray_data_t create(const float3 origin, const float3 direction,
                           uint32_t pixel_index) {
    ray_data_t ray_data;
    ray_data.origin = origin;
    ray_data.direction = direction;
    ray_data.inv_direction = float3(safe_inverse(ray_data.direction.x),
                                    safe_inverse(ray_data.direction.y),
                                    safe_inverse(ray_data.direction.z), );
    ray_data.tmin = epsilon;
    ray_data.tmax = infinity;
    ray_data.pixel_index = pixel_index;
    return ray_data;
  }
  float3 origin, direction;
  float3 inv_direction;
  float tmin, tmax;
  uint32_t pixel_index;
};

struct triangle_intersection_t {
  bool did_intersect() { return _did_intersect; }
  bool _did_intersect;
  float t, u, v, w;
};

struct aabb_intersection_t {
  bool did_intersect() { return tmin <= tmax; }
  float tmin, tmax;
};

struct hit_t {
  bool did_intersect() {
    return blas_index != invalid_index && primitive_index != invalid_index;
  }
  uint32_t blas_index = invalid_index;
  uint32_t primitive_index = invalid_index;
  float t = infinity;
  float u = 0, v = 0, w = 0;
  uint32_t node_intersection_count = 0;
  uint32_t primitive_intersection_count = 0;
};

triangle_intersection_t triangle_intersect(const ray_data_t ray_data,
                                           const triangle_t triangle) {
  triangle_intersection_t intersection;

  float3 e1 = triangle.v0 - triangle.v1;
  float3 e2 = triangle.v2 - triangle.v0;
  float3 n = cross(e1, e2);

  float3 c = triangle.v0 - ray_data.origin;
  float3 r = cross(ray_data.direction, c);
  float inverse_det = 1.0f / dot(n, ray_data.direction);

  float u = dot(r, e2) * inverse_det;
  float v = dot(r, e1) * inverse_det;
  float w = 1.0f - u - v;

  if (u >= 0 && v >= 0 && w >= 0) {
    float t = dot(n, c) * inverse_det;
    if (t >= ray_data.tmin && t <= ray_data.tmax) {
      intersection._did_intersect = true;
      intersection.t = t;
      intersection.u = u;
      intersection.v = v;
      intersection.w = w;
      return intersection;
    }
  }
  intersection._did_intersect = false;
  return intersection;
}

aabb_intersection_t aabb_intersect(const ray_data_t ray_data,
                                   const aabb_t aabb) {
  float3 tmin = (aabb.min - ray_data.origin) * ray_data.inv_direction;
  float3 tmax = (aabb.max - ray_data.origin) * ray_data.inv_direction;

  const float3 old_tmin = tmin;
  const float3 old_tmax = tmax;

  tmin = min(old_tmin, old_tmax);
  tmax = max(old_tmin, old_tmax);

  float _tmin = max(tmin[0], max(tmin[1], max(tmin[2], ray_data.tmin)));
  float _tmax = min(tmax[0], min(tmax[1], min(tmax[2], ray_data.tmax)));

  aabb_intersection_t aabb_intersection = { _tmin, _tmax };
  return aabb_intersection;
}

struct bvh_instance_t {
  vertex_t *vertices;
  uint32_t *indices;

  // bvh
  node_t *nodes;
  /* bvh indices buffer
   * To get the bvh triangle, directly use index
   * To get the vertices, the indices are as follows
   * indices in normal index buffer are as follows
   *    primitive_index * 3 + 0
   *    primitive_index * 3 + 1
   *    primitive_index * 3 + 2
   * */
  uint32_t *primitive_indices;
  triangle_t *bvh_triangles;

  float4x4 *model;
  float4x4 *inv_model;

  uint32_t diffuse_bindless;
};

// traversal will be in trace.slang shader
