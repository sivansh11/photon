#include "common.slang"

[vk::push_constant]
push_constant_raytracing_t pc;
[vk::binding(2, 0)]
RWTexture2D<float4> storage_images[1000];

ray_data_t raygen(float2 uv, uint32_t pixel_index) {
  float2 px_nds = uv * 2.f - 1.f;
  float3 point_nds = float3(px_nds, -1);
  float4 point_ndsh = float4(point_nds, 1);
  float4 dir_eye = point_ndsh * pc.camera.inv_projection;
  dir_eye.w = 0;
  float3 dir_world = float3(dir_eye * pc.camera.inv_view);
  float3 eye = { pc.camera.inv_view[3][0], pc.camera.inv_view[3][1],
                 pc.camera.inv_view[3][2] };
  return ray_data_t::create(eye, dir_world, pixel_index);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(const uint3 dispatch_thread_id: SV_DispatchThreadID,
                  const uint group_index: SV_GroupIndex) {
  const uint32_t pixel_i = dispatch_thread_id.x;
  const uint32_t pixel_j = dispatch_thread_id.y;
  const uint32_t pixel_index = pixel_j * pc.width + pixel_i;

  if (pixel_i >= pc.width)
    return;
  if (pixel_j >= pc.height)
    return;

  const float u = float(pixel_i) / float(pc.width - 1);
  const float v = float(pixel_j) / float(pc.height - 1);

  storage_images[0][uint2(pixel_i, pixel_j)] = float4(0, 0, 0, 0);

  pc.ray_data[pixel_index] = raygen( { u, v }, pixel_index);
  if (pixel_i == 0 && pixel_j == 0) {
    pc.param.num_rays = pc.width * pc.height;
  }
}
