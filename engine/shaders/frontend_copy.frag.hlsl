Texture2D<float4> source_image : register(t0, space2);
SamplerState source_sampler : register(s0, space2);
cbuffer Copy : register(b0, space3) { float4 source_rect; };
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return source_image.Sample(source_sampler, source_rect.xy + uv * source_rect.zw);
}
