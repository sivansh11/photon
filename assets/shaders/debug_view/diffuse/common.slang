float4 operator *(float4x4 m, float4 v) { return mul(m, v); }
float4 operator *(float4 v, float4x4 m) { return mul(v, m); }

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

struct material_t {
  uint32_t diffuse; // bindless index
};

struct mesh_t {
  vertex_t *vertices;
  uint32_t *indices;
  float4x4 *model;
  float4x4 *inv_model;
  material_t material;
};

struct push_constant_t {
  mesh_t mesh;
  camera_t *camera;
};

struct per_fragment_t {
  float2 uv;
};

struct vertex_shader_output_t {
  per_fragment_t per_fragment;
  float4 sv_position : SV_Position;
};
