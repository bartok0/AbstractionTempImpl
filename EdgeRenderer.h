#pragma once
#include "Common\DeviceResources.h"
#include "D3d11.h"

class EdgeRenderer
{
public:
	struct EdgeVertexCollection {
		Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> modelConstantBuffer;
		Windows::Perception::Spatial::SpatialCoordinateSystem^ coord;
		UINT numVertices;
		Windows::Foundation::Numerics::float3 vertexScale;
	};

	EdgeRenderer::EdgeRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);

	void EdgeRenderer::Render(bool isStereo);

	void EdgeRenderer::Update(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);

	void EdgeRenderer::CreateBuffers(std::vector<DirectX::XMFLOAT3>* vertices, Windows::Perception::Spatial::SpatialCoordinateSystem^ meshCoord, Windows::Perception::Spatial::SpatialCoordinateSystem^ base);
	void EdgeRenderer::UpdateEdgeBuffers(std::vector<DirectX::XMFLOAT3>* vertices, Windows::Perception::Spatial::SpatialCoordinateSystem ^ model, Windows::Perception::Spatial::SpatialCoordinateSystem ^ base);

	void EdgeRenderer::CreateDeviceDependentResources();
	void EdgeRenderer::ReleaseDeviceDependentResources();

	struct ModelConstantStruct {
		Windows::Foundation::Numerics::float4x4 model;
	};

private:
	//**
	Microsoft::WRL::ComPtr<ID3D11Buffer> edgeVertexBuffer;
	Microsoft::WRL::ComPtr<ID3D11Buffer> modelTransformBuffer;
	std::vector<DirectX::XMFLOAT3>* edgeVertices;

	UINT vertexCount = 0;
	UINT vertexStride;
	UINT vertexOffset = 0;
	

	bool buffersReady = false;
	//**


	std::vector<EdgeVertexCollection> edgeBuffers;
	

	Windows::Perception::Spatial::SpatialCoordinateSystem^ baseCoordinateSystem;

	//shaders
	Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
	Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
	Microsoft::WRL::ComPtr<ID3D11GeometryShader> geometryShader;

	Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;

	std::shared_ptr<DX::DeviceResources> deviceResources;

	std::mutex vertexMutex;
	bool loadingComplete;
};
