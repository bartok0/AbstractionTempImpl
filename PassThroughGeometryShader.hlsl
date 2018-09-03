struct GeometryShaderInput
{
	min16float4 pos     : SV_POSITION;
	uint        instId  : TEXCOORD0;
};

struct GeometryShaderOutput
{
	min16float4 pos     : SV_POSITION;
	uint        rtvId   : SV_RenderTargetArrayIndex;
};

[maxvertexcount(64)]
void main( line GeometryShaderInput input[2], inout LineStream < GeometryShaderOutput > output)
{
	[unroll(2)]
	for (uint i = 0; i < 2; i++)
	{
		GeometryShaderOutput element;
		element.pos = input[i].pos;
		element.rtvId = input[i].instId;
		output.Append(element);
	}
}