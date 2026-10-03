Texture2D<float4> color_texture : register(t0, space2);
Texture2D<float4> mask_texture : register(t1, space2);
SamplerState color_sampler : register(s0, space2);
SamplerState mask_sampler : register(s1, space2);
struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
float4 main(VSOut input) : SV_Target
{
    // Like TIE's CRT compositor, with the target's own coverage preserved for XvT artwork.
    return color_texture.Sample(color_sampler, input.uv) * mask_texture.Sample(mask_sampler, input.uv).a;
}
