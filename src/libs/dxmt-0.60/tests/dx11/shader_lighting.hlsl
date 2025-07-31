cbuffer constants : register(b0)
{
    float4x4 modelViewProj;
    float4 color;
};

struct VertexShaderInput {
    float3 pos : POS;
};

struct VertexShaderOutput {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

VertexShaderOutput vs_main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.pos = mul(float4(input.pos, 1.0f), modelViewProj);
    output.color = color;
    return output;
}

float4 ps_main(VertexShaderOutput input) : SV_Target
{
    return input.color;
}