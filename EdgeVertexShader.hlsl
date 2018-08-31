cbuffer MVP_Struct : register(b0)
{
	float4x4 MVP_M;
}

float4 main( float3 pos : POSITION ) : SV_POSITION
{
	float4 pos4 = float4(pos, 1.0f);
	float4 output = mul(pos4, MVP_M);
	return output;
}