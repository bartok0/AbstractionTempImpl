#pragma once
#include "Common\DeviceResources.h"
#include "D3d11.h"

class EdgeRenderer
{
public:
	struct EdgeVertexCollection {
		Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> modelConstantBuffer;
		UINT numVertices;
	};

	EdgeRenderer::EdgeRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);

	void EdgeRenderer::Render(bool isStereo);

	void EdgeRenderer::Update(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);

	//void EdgeRenderer::CreateBuffers(CreateBufferInput input);
	void EdgeRenderer::UpdateEdgeVertexBuffer(DirectX::XMFLOAT3* vertices);

	void EdgeRenderer::CreateDeviceDependentResources();
	void EdgeRenderer::ReleaseDeviceDependentResources();

	struct ViewProjectionStruct {
		Windows::Foundation::Numerics::float4x4 VPmatrix[2];
	};

private:

	std::vector<EdgeVertexCollection> edgeBuffers;
	UINT vertexStride;
	UINT vertexOffset;

	Windows::Perception::Spatial::SpatialCoordinateSystem^ baseCoordinateSystem;

	//shaders
	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
	Microsoft::WRL::ComPtr<ID3D11GeometryShader> geometryShader;

	Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;

	std::shared_ptr<DX::DeviceResources> deviceResources;

	ViewProjectionStruct MVP_M;

	std::mutex vertexMutex;
	bool loadingComplete;
	bool isStereo;

	float timeSinceUpdate;
	//How often to pull new edge data
	const float updateInterval = 1.0f;
};
