// D3D11 Shader Model 4 replacement for the historical StdQuad Cg family.

float4x4 worldViewProj;

struct StdQuadVertexInput
{
    float4 position : POSITION;
};

struct StdQuadVertexOutput
{
    float4 position : SV_Position;
    float2 uv0 : TEXCOORD0;
};

struct StdQuadTex2VertexOutput
{
    float4 position : SV_Position;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
};

struct StdQuadTex3VertexOutput
{
    float4 position : SV_Position;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float2 uv2 : TEXCOORD2;
};

struct StdQuadTex4VertexOutput
{
    float4 position : SV_Position;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float2 uv2 : TEXCOORD2;
    float2 uv3 : TEXCOORD3;
};

float2 StdQuadSignedPosition(float4 position)
{
    return sign(position.xy);
}

float2 StdQuadImageUv(float2 signedPosition)
{
    return (float2(signedPosition.x, -signedPosition.y) + 1.0f) * 0.5f;
}

StdQuadVertexOutput StdQuad_vp(StdQuadVertexInput input)
{
    StdQuadVertexOutput output;
    float2 signedPosition = StdQuadSignedPosition(input.position);
    output.position = mul(worldViewProj, input.position);
    output.uv0 = StdQuadImageUv(signedPosition);
    return output;
}

StdQuadTex2VertexOutput StdQuad_Tex2_vp(StdQuadVertexInput input)
{
    StdQuadTex2VertexOutput output;
    float2 signedPosition = StdQuadSignedPosition(input.position);
    float2 imageUv = StdQuadImageUv(signedPosition);
    output.position = mul(worldViewProj, input.position);
    output.uv0 = imageUv;
    output.uv1 = imageUv;
    return output;
}

StdQuadTex2VertexOutput StdQuad_Tex2a_vp(StdQuadVertexInput input)
{
    StdQuadTex2VertexOutput output;
    float2 signedPosition = StdQuadSignedPosition(input.position);
    output.position = mul(worldViewProj, input.position);
    output.uv0 = StdQuadImageUv(signedPosition);
    output.uv1 = signedPosition;
    return output;
}

StdQuadTex3VertexOutput StdQuad_Tex3_vp(StdQuadVertexInput input)
{
    StdQuadTex3VertexOutput output;
    float2 signedPosition = StdQuadSignedPosition(input.position);
    float2 imageUv = StdQuadImageUv(signedPosition);
    output.position = mul(worldViewProj, input.position);
    output.uv0 = imageUv;
    output.uv1 = imageUv;
    output.uv2 = imageUv;
    return output;
}

StdQuadTex4VertexOutput StdQuad_Tex4_vp(StdQuadVertexInput input)
{
    StdQuadTex4VertexOutput output;
    float2 signedPosition = StdQuadSignedPosition(input.position);
    float2 imageUv = StdQuadImageUv(signedPosition);
    output.position = mul(worldViewProj, input.position);
    output.uv0 = imageUv;
    output.uv1 = imageUv;
    output.uv2 = imageUv;
    output.uv3 = imageUv;
    return output;
}
