public static const float epsilon = 0.0001;
public static const float infinity = 100000000000000.f;

public static const uint32_t invalid_index = 4294967295u;

public static const uint32_t STACK_SIZE = 16;

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
