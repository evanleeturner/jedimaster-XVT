cbuffer Placement : register(b0, space1)
{
    float4 dst_rect;
    float4 src_rect;
};
struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
VSOut main(uint vid : SV_VertexID)
{
    float2 corner = float2(vid & 1u, (vid >> 1u) & 1u);
    VSOut o;
    o.position = float4(dst_rect.xy + corner * dst_rect.zw, 0, 1);
    o.uv = lerp(src_rect.xy, src_rect.zw, float2(corner.x, 1 - corner.y));
    return o;
}
