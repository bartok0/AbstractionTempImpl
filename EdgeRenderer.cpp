#include "pch.h"
#include "Common/DirectXHelper.h"
#include "EdgeRenderer.h"


EdgeRenderer::EdgeRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources) :
	deviceResources(deviceResources)
{
	CreateDeviceDependentResources();
};

void EdgeRenderer::Update(Windows::Perception::Spatial::SpatialCoordinateSystem ^ base)
{
	if (!loadingComplete) {
		return;
	}
	
	auto context = deviceResources->GetD3DDeviceContext();

	for (auto it = edgeBuffers->begin(); it != edgeBuffers->end(); it++) {
		Windows::Foundation::Numerics::float4x4 model;
		DirectX::XMStoreFloat4x4(&model,
			DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&it->coord->TryGetTransformTo(base)->Value)));
		

		context->UpdateSubresource(
			it->modelConstantBuffer.Get(),
			0,
			NULL,
			&model,
			0,
			0
		);
	}
}

void EdgeRenderer::CreateBuffer(std::vector<DirectX::XMFLOAT3>* vertices, Windows::Perception::Spatial::SpatialCoordinateSystem^ modelCoord) {
	vertexMutex.lock();
	buffersReady = false;
	vertexMutex.unlock();
	auto device = deviceResources->GetD3DDevice();

	EdgeVertexCollection newCollection;
	newCollection.coord = modelCoord;
	newCollection.numVertices = vertices->size();

	D3D11_BUFFER_DESC vBufferDesc;
	ZeroMemory(&vBufferDesc, sizeof(vBufferDesc));
	vBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	vBufferDesc.ByteWidth = sizeof(DirectX::XMFLOAT3) * vertices->size();
	vBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vBufferDesc.CPUAccessFlags = 0;
	vBufferDesc.MiscFlags = 0;
	vBufferDesc.StructureByteStride = sizeof(DirectX::XMFLOAT3);

	D3D11_SUBRESOURCE_DATA vBufferData;
	ZeroMemory(&vBufferData, sizeof(vBufferData));
	vBufferData.pSysMem = vertices->data();
	vBufferData.SysMemPitch = 0;
	vBufferData.SysMemSlicePitch = 0;

	device->CreateBuffer(&vBufferDesc, &vBufferData, newCollection.vertexBuffer.GetAddressOf());

	D3D11_BUFFER_DESC cBufferDesc;
	ZeroMemory(&cBufferDesc, sizeof(cBufferDesc));
	cBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	cBufferDesc.ByteWidth = sizeof(Windows::Foundation::Numerics::float4x4);
	cBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cBufferDesc.CPUAccessFlags = 0;
	cBufferDesc.MiscFlags = 0;
	cBufferDesc.StructureByteStride = sizeof(Windows::Foundation::Numerics::float4x4);

	D3D11_SUBRESOURCE_DATA cBufferData;
	ZeroMemory(&cBufferData, sizeof(cBufferData));
	cBufferData.pSysMem = Windows::Foundation::Numerics::float4x4::identity;
	cBufferData.SysMemPitch = 0;
	cBufferData.SysMemSlicePitch = 0;

	device->CreateBuffer(&cBufferDesc, &cBufferData, newCollection.modelConstantBuffer.GetAddressOf());

	edgeBuffers->push_back(newCollection);

	buffersReady = true;
}

void EdgeRenderer::Render(bool isStereo)
{
	if (!loadingComplete) {
		return;
	}
	//Lock to avoid data race with 'UpdateEdgeBuffers'
	vertexMutex.lock();
	if (!buffersReady) {
		//If the buffers are not ready yet, release the lock and return
		vertexMutex.unlock();
		return;
	}

	bool usingVprt = deviceResources->GetDeviceSupportsVprt();
	

	auto context = deviceResources->GetD3DDeviceContext();

	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
	context->IASetInputLayout(inputLayout.Get());
	context->VSSetShader(vertexShader.Get(), nullptr, 0);
	context->PSSetShader(pixelShader.Get(), nullptr, 0);
	if (!usingVprt)
		context->GSSetShader(geometryShader.Get(), nullptr, 0);
	context->RSSetState(rasterizerState.Get());
	
	for (auto it = edgeBuffers->begin(); it != edgeBuffers->end(); it++) {
		context->IASetVertexBuffers(
			0,
			1,
			it->vertexBuffer.GetAddressOf(),
			&vertexStride,
			&vertexOffset
		);

		context->VSSetConstantBuffers(
			0, 
			1, 
			it->modelConstantBuffer.GetAddressOf()
		);

		context->DrawInstanced(
			it->numVertices,
			isStereo ? 2 : 1,
			0,
			0
		);
	}
	
	//Release lock after drawing the frame
	vertexMutex.unlock();
}

void EdgeRenderer::CreateDeviceDependentResources()
{
	auto device = deviceResources->GetD3DDevice();
	bool usingVprt = deviceResources->GetDeviceSupportsVprt();
	
	edgeBuffers = new std::vector<EdgeVertexCollection>;
	vertexStride = sizeof(DirectX::XMFLOAT3);

	Concurrency::task<std::vector<byte>> loadVSTask = DX::ReadDataAsync(L"ms-appx:///EdgeVertexShader.cso");
	Concurrency::task<std::vector<byte>> loadPSTask = DX::ReadDataAsync(L"ms-appx:///EdgePixelShader.cso");
	Concurrency::task<std::vector<byte>> loadGSTask;
	if (!usingVprt)
	{
		// Load the pass-through geometry shader.
		loadGSTask = DX::ReadDataAsync(L"ms-appx:///PassThroughGeometryShader.cso");
	}
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
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
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

	//Create geometry shader if needed
	Concurrency::task<void> createGSTask;
	if (!usingVprt)
	{
		// After the pass-through geometry shader file is loaded, create the shader.
		createGSTask = loadGSTask.then([this](const std::vector<byte>& fileData)
		{
			DX::ThrowIfFailed(
				deviceResources->GetD3DDevice()->CreateGeometryShader(
					&fileData[0],
					fileData.size(),
					nullptr,
					geometryShader.GetAddressOf()
				)
			);
		});
	}

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

	Concurrency::task<void> shaderTaskGroup = usingVprt ? (createPSTask && createVSTask) : (createPSTask && createGSTask && createPSTask);

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
	vertexShader.Reset();
	pixelShader.Reset();
	geometryShader.Reset();
	rasterizerState.Reset();
	buffersReady = false;
	loadingComplete = false;
	for (auto it = edgeBuffers->begin(); it != edgeBuffers->end(); it++) {
		it->modelConstantBuffer.Reset();
		it->vertexBuffer.Reset();
	}
	//TODO: CLEAR OUT VERTEX DATA
}

