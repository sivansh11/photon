#include "common.slang"

[vk::binding(0, 0)]
uniform Texture2D textures[1000];
[vk::binding(1, 0)]
uniform SamplerState samplers[1000];

[vk::push_constant]
push_constant_raster_t pc;

struct fragment_t {
  float4 o_color : COLOR0;
};

[shader("fragment")]
fragment_t fragment_main(per_fragment_t per_fragment) : SV_Target {
  fragment_t fragment;

  fragment.o_color =
      textures[pc.diffuse_bindless].Sample(samplers[0], per_fragment.uv);
  // fragment.o_color = float4(per_fragment.uv, 0, 1);

  return fragment;
}
