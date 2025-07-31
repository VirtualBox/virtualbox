struct VS_Output {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

VS_Output vs_main(uint id : SV_VertexID)
{
    VS_Output output;
    float2 uv = float2((id << 1) & 2, id & 2);
	output.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    output.uv = uv;
    return output;
}

float4 ps_main_linear(VS_Output input) : SV_TARGET
{
    float2 coord = floor(input.uv * float2(10.0f, 10.0f));
    float linear_value = coord.y * 10 + coord.x;
    return float4(linear_value, linear_value, linear_value, 1.0f);
}

float linear_to_pq(float L)
{
	float m1 = 2610.0 / 4096.0 / 4;
	float m2 = 2523.0 / 4096.0 * 128;
	float c1 = 3424.0 / 4096.0;
	float c2 = 2413.0 / 4096.0 * 32;
	float c3 = 2392.0 / 4096.0 * 32;
	float Lp = pow(L, m1);
	return pow((c1 + c2 * Lp) / (1 + c3 * Lp), m2);
}

float4 ps_main_pq(VS_Output input) : SV_TARGET
{
    float2 coord = floor(input.uv * float2(10.0f, 10.0f));
    float pq_value = linear_to_pq((coord.y * 10 + coord.x) / 100.0f);
    return float4(pq_value, pq_value, pq_value, 1.0f);
}