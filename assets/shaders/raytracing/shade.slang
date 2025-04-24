#include "common.slang"

[vk::push_constant]
push_constant_raytracing_t pc;
[vk::binding(2, 0)]
RWTexture2D<float4> storage_images[1000];

float4 color(uint32_t index) {
  float4 col = float4(((index * 9665 + 790) % 256) / 255.f,
                      ((index * 8765976 + 876) % 256) / 255.f,
                      ((index * 2345678 + 876) % 256) / 255.f, 1);
  return col;
}

float4 heatmap(float t) {
  const float3 C0 = float3(-0.020390, 0.009557, 0.018508);
  const float3 C1 = float3(3.108226, -0.106297, -1.105891);
  const float3 C2 = float3(-14.539061, -2.943057, 14.548595);
  const float3 C3 = float3(71.394557, 22.644423, -71.418400);
  const float3 C4 = float3(-152.022488, -31.024563, 152.048692);
  const float3 C5 = float3(139.593599, 12.411251, -139.604042);
  const float3 C6 = float3(-46.532952, -0.000874, 46.532928);
  return float4(
      C0 + (C1 + (C2 + (C3 + (C4 + (C5 + C6 * t) * t) * t) * t) * t) * t, 1);
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

  hit_t hit = pc.hits[index];
  ray_data_t ray_in = pc.ray_data[index];

  uint32_t pixel_i = ray_in.pixel_index % pc.width;
  uint32_t pixel_j = ray_in.pixel_index / pc.width;

  if (hit.did_intersect()) {
    // storage_images[0][uint2(pixel_i, pixel_j)] = color(hit.primitive_index);
    storage_images[0][uint2(pixel_i, pixel_j)] =
        heatmap(hit.node_intersection_count / 100.f);
  }
}
