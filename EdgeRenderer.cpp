#include "pch.h"
#include "EdgeRenderer.h"

#include "Common/DirectXHelper.h"


EdgeRenderer::EdgeRenderer()
{
}

EdgeRenderer::EdgeRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources) :
	deviceResources(deviceResources)
{
	//m_meshCollection.clear(); - TODO CLEAR VERTICES ARRAY
	CreateDeviceDependentResources();
};


EdgeRenderer::~EdgeRenderer()
{
}

void EdgeRenderer::Update(Windows::Perception::Spatial::SpatialCoordinateSystem ^ baseCoordinateSystem)
{
	if (!loadingComplete) {
		return;
	}

	auto context = deviceResources->GetD3DDeviceContext();

	DirectX::XMMATRIX transform;
	auto MVP_M = baseCoordinateSystem->TryGetTransformTo(baseCoordinateSystem);
	if (MVP_M != nullptr) {
		transform = DirectX::XMLoadFloat4x4(&MVP_M->Value);
	}

	DirectX::XMFLOAT4X4 updatedTransform;
	DirectX::XMStoreFloat4x4(&updatedTransform, transform);

	context->UpdateSubresource(
		mvp_constantBuffer,
		0,
		NULL,
		&updatedTransform,
		0,
		0
	);
}

void EdgeRenderer::Render(bool isStereo)
{
	if (!loadingComplete) {
		return;
	}
	
	auto context = deviceResources->GetD3DDeviceContext();

	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->IASetInputLayout(inputLayout.Get());

	context->VSSetShader(
		vertexShader.Get(),
		nullptr,
		0
	);

	context->PSSetShader(
		pixelShader.Get(),
		nullptr,
		0
	);

	DirectX::XMFLOAT3 vertexlist[] = { DirectX::XMFLOAT3( -1.5f,-1.5f,-1.0f ),DirectX::XMFLOAT3(0,0,-1.0f), DirectX::XMFLOAT3(1.5f,1.5f,-1.0f) };

	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(DirectX::XMFLOAT3) * 3;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;
	bufferDesc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA InitData;
	InitData.pSysMem = &vertexlist;
	InitData.SysMemPitch = 0;
	InitData.SysMemSlicePitch = 0;

	ID3D11Buffer** vBuffer = {};
	ID3D11Device* device;
	context->GetDevice(&device);
	device->CreateBuffer(&bufferDesc, &InitData, vBuffer);

	UINT stride = sizeof(DirectX::XMFLOAT3);

	context->IASetVertexBuffers(
		0,
		1,
		vBuffer,
		&stride,
		0
	);

	deviceResources->GetD3DDeviceContext()->RSSetState(rasterizerState.Get());

	D3D11_BUFFER_DESC cbDesc;
	cbDesc.ByteWidth = sizeof(DirectX::XMFLOAT4X4);
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDesc.MiscFlags = 0;
	cbDesc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA InitData2;
	InitData.pSysMem = &mvp_constantBuffer;
	InitData.SysMemPitch = 0;
	InitData.SysMemSlicePitch = 0;

	device->CreateBuffer(&cbDesc, &InitData2, &mvp_constantBuffer);

	context->VSSetConstantBuffers(
		0,
		1,
		&mvp_constantBuffer
	);

	context->DrawInstanced(
		3,
		isStereo ? 2 : 1,  
		0,                  
		0
	);
}

//TODO IMPLEMENT "XMFLOAT3 -> I3D11BUffer" for rendering
void EdgeRenderer::UpdateEdgeVertexBuffer(DirectX::XMFLOAT3* vertices){
	return;
}

void EdgeRenderer::CreateDeviceDependentResources()
{
	Concurrency::task<std::vector<byte>> loadVSTask = DX::ReadDataAsync(L"ms-appx:///EdgeVertexShader.cso");
	Concurrency::task<std::vector<byte>> loadPSTask = DX::ReadDataAsync(L"ms-appx:///EdgePixelShader.cso");

	//mvp_constantBuffer = new ID3D11Buffer;

	//Create vertex shader
	auto createVSTask = loadVSTask.then([this](const std::vector<byte>& fileData) {
		DX::ThrowIfFailed(
			deviceResources->GetD3DDevice()->CreateVertexShader(
				&fileData[0],
				fileData.size(),
				nullptr,
				&vertexShader
			)
		);

		static const D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};

		DX::ThrowIfFailed(
			deviceResources->GetD3DDevice()->CreateInputLayout(
				vertexDesc,
				ARRAYSIZE(vertexDesc),
				&fileData[0],
				fileData.size(),
				&inputLayout
			)
		);
	});

	//Create pixel shader
	auto createPSTask = loadPSTask.then([this](const std::vector<byte>& fileData) {
		DX::ThrowIfFailed(
			deviceResources->GetD3DDevice()->CreatePixelShader(
				&fileData[0],
				fileData.size(),
				nullptr,
				&pixelShader
			)
		);
	});

	Concurrency::task<void> shaderTaskGroup = (createPSTask && createVSTask);

	auto finishLoadingTask = shaderTaskGroup.then([this]() {

		// Create rasterizer state descriptor.
		D3D11_RASTERIZER_DESC rasterizerDesc = CD3D11_RASTERIZER_DESC(D3D11_DEFAULT);

		//rasterizer settings
		rasterizerDesc.AntialiasedLineEnable = true;
		rasterizerDesc.CullMode = D3D11_CULL_NONE;
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;

		// Create rasterizer state.
		deviceResources->GetD3DDevice()->CreateRasterizerState(&rasterizerDesc, rasterizerState.GetAddressOf());

		loadingComplete = true;
	});
}

void EdgeRenderer::ReleaseDeviceDependentResources()
{
	edgeVertexPositions.Reset();
	delete mvp_constantBuffer;
	vertexShader.Reset(); 
	pixelShader.Reset();
	rasterizerState.Reset();
	
	//TODO: CLEAR OUT VERTEX DATA
}

