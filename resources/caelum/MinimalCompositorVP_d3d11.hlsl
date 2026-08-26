// Modern D3D11 full-screen vertex path for Caelum compositors.

#if defined(CAELUM_MINIMAL_COMPOSITOR_VERTEX)

float4x4 worldviewproj_matrix;

struct CaelumCompositorVertexInput
{
    float4 position : POSITION;
};

struct CaelumCompositorVertexOutput
{
    float4 clipPosition : SV_Position;
    float2 screenPosition : TEXCOORD0;
};

CaelumCompositorVertexOutput CaelumMinimalCompositorVS(
    CaelumCompositorVertexInput input)
{
    CaelumCompositorVertexOutput output;
    output.clipPosition = mul(worldviewproj_matrix, input.position);
    float2 signedPosition = sign(input.position.xy);
    output.screenPosition =
        (float2(signedPosition.x, -signedPosition.y) + 1.0) * 0.5;
    return output;
}

#else
#error The Caelum minimal-compositor vertex selector is required
#endif
