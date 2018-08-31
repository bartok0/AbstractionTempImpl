#pragma once
#include "Common\DeviceResources.h"
#include "D3d11.h"

class EdgeRenderer
{
public:
	EdgeRenderer::EdgeRenderer();
	EdgeRenderer::EdgeRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);
	EdgeRenderer::~EdgeRenderer();

	void EdgeRenderer::Render(bool isStereo);

	void EdgeRenderer::Update(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem /*DATA POINTER FOR UPDATE??*/);

	//create/update vertex buffer method
	void EdgeRenderer::UpdateEdgeVertexBuffer(DirectX::XMFLOAT3* vertices);

	void CreateDeviceDependentResources();
	void ReleaseDeviceDependentResources();

	struct MVP_Struct {
		Windows::Foundation::Numerics::float4x4 MVP;
		MVP_Struct(Windows::Foundation::Numerics::float4x4 in) : MVP(in) {}
	};

private:
	//vertex buffer
	Microsoft::WRL::ComPtr<ID3D11Buffer> edgeVertexPositions;

	//transform / constant buffer(s)
	ID3D11Buffer* mvp_constantBuffer;

	//format


	//stride(s)


	//shaders
	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;

	Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;

	std::shared_ptr<DX::DeviceResources> deviceResources;

	Windows::Foundation::Numerics::float4x4 MVP_M;

	std::mutex vertexMutex;
	bool loadingComplete;
	bool isStereo;

	float timeSinceUpdate;
	//How often to pull new edge data
	const float updateInterval = 1.0f;
};

