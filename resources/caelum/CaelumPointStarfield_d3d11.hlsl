// Modern D3D11 port of Caelum's magnitude-scaled point starfield.

#if defined(CAELUM_POINT_STAR_VERTEX)

float4x4 worldviewproj_matrix;
float mag_scale;
float mag0_size;
float min_size;
float max_size;
float render_target_flipping;
float aspect_ratio;

struct CaelumPointStarVertexInput
{
    float4 position : POSITION;
    float3 texcoord : TEXCOORD0;
};

struct CaelumPointStarVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 colour : COLOR0;
};

CaelumPointStarVertexOutput CaelumPointStarVS(
    CaelumPointStarVertexInput input)
{
    CaelumPointStarVertexOutput output;
    output.clipPosition = mul(worldviewproj_matrix, input.position);
    output.texcoord = input.texcoord.xy;

    float size = exp(mag_scale * input.texcoord.z) * mag0_size;
    float fade = saturate(size / min_size);
    output.colour = float4(1.0, 1.0, 1.0, fade * fade);
    size = clamp(size, min_size, max_size);
    output.clipPosition.xy += output.clipPosition.w * input.texcoord.xy
        * float2(size, size * aspect_ratio * render_target_flipping);
    return output;
}

#elif defined(CAELUM_POINT_STAR_FRAGMENT)

struct CaelumPointStarVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 colour : COLOR0;
};

float4 CaelumPointStarPS(
    CaelumPointStarVertexOutput input) : SV_Target
{
    float4 colour = input.colour;
    float squaredLength = dot(input.texcoord, input.texcoord);
    colour.a *= 1.5 * exp(-(squaredLength * 8.0));
    return colour;
}

#else
#error A Caelum point-star shader stage selector is required
#endif
