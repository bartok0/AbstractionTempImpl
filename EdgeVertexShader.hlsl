cbuffer ViewProjectionConstantBuffer : register(b1)
{
	float4 cameraPosition;
	float4 lightPosition;
	float4x4 viewProjection[2];
}

cbuffer ModelConstantBuffer : register(b0)
{
	float4x4 model;
}

struct VertexShaderOutput
{
	min16float4 pos     : SV_POSITION;
	// The render target array index will be set by the geometry shader.
	uint        viewId  : TEXCOORD0;
};

struct input {
	float3 pos : POSITION;
	uint instId : SV_InstanceID;
};

VertexShaderOutput main( input input )
{
	VertexShaderOutput output;

	float4 pos4 = float4(input.pos, 1.0f);
	uint idx = input.instId % 2;

	//pos4 = mul(pos4, model);

	output.pos = mul(pos4, viewProjection[idx]);
	output.viewId = idx;

	return output;
}