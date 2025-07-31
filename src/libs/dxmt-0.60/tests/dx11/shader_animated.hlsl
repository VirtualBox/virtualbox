cbuffer constants : register(b0)
{
    float2 offset;
    float4 uniformColor;
};

struct VS_Output {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

VS_Output vs_main(float2 pos : POS)
{
    VS_Output output;
    output.pos = float4(pos + offset, 0.0f, 1.0f);
    output.color = uniformColor;
    return output;
}

float4 ps_main(VS_Output input) : SV_Target
{
     return input.color;   
}