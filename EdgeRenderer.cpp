#include "pch.h"
#include "Common/DirectXHelper.h"
#include "EdgeRenderer.h"


EdgeRenderer::EdgeRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources) :
	deviceResources(deviceResources)
{
	//m_meshCollection.clear(); - TODO CLEAR VERTICES ARRAY
	CreateDeviceDependentResources();
};

void EdgeRenderer::Update(Windows::Perception::Spatial::SpatialCoordinateSystem ^ base)
{
	if (baseCoordinateSystem == nullptr) {
		baseCoordinateSystem = base;
	}

	if (!loadingComplete) {
		return;
	}

	//auto context = deviceResources->GetD3DDeviceContext();
	/*
	const DirectX::XMMATRIX modelTranslation = DirectX::XMMatrixTranslationFromVector(DirectX::XMLoadFloat3(DirectX::XMFLOAT3(0,0,0));
	Windows::Foundation::Numerics::float4x4 modelData;
	DirectX::XMStoreFloat4x4(&modelData, modelTranslation);

	context->UpdateSubresource(
		modelConstantBuffer.Get(),
		0,
		NULL,
		&modelData,
		0,
		0
	);
	*/

	//TODO UPDATE VERTICES (?)
}
/*
void EdgeRenderer::CreateBuffers( CreateBufferInput input) {
	auto device = deviceResources->GetD3DDevice();

	EdgeVertexCollection newCollection;
	newCollection.numVertices = input.vertices.size();
	Windows::Foundation::Numerics::float4x4 model;
	model = input.meshCoord->TryGetTransformTo(input.base)->Value;

	D3D11_BUFFER_DESC vBufferDesc;
	vBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	vBufferDesc.ByteWidth = sizeof(DirectX::XMFLOAT3) * input.vertices.size();
	vBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vBufferDesc.CPUAccessFlags = 0;
	vBufferDesc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA vBufferData;
	vBufferData.pSysMem = &input.vertices;
	vBufferData.SysMemPitch = 0;
	vBufferData.SysMemSlicePitch = 0;

	device->CreateBuffer(&vBufferDesc, &vBufferData, newCollection.vertexBuffer.GetAddressOf());

	D3D11_BUFFER_DESC iBufferDesc;
	iBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	iBufferDesc.ByteWidth = sizeof(indices);
	iBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	iBufferDesc.CPUAccessFlags = 0;
	iBufferDesc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA iBufferData;
	iBufferData.pSysMem = indices;
	iBufferData.SysMemPitch = 0;
	iBufferData.SysMemSlicePitch = 0;

	device->CreateBuffer(&iBufferDesc, &iBufferData, indexBuffer.GetAddressOf());
	
	D3D11_BUFFER_DESC cBufferDesc;
	cBufferDesc.ByteWidth = sizeof(Windows::Foundation::Numerics::float4x4);
	cBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	cBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cBufferDesc.CPUAccessFlags = 0;
	cBufferDesc.MiscFlags = 0;
	cBufferDesc.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA cBufferData;
	vBufferData.pSysMem = &model;
	vBufferData.SysMemPitch = 0;
	vBufferData.SysMemSlicePitch = 0;

	device->CreateBuffer(&cBufferDesc, &cBufferData, newCollection.modelConstantBuffer.GetAddressOf());

	vertexMutex.lock();
	edgeBuffers.push_back(newCollection);
	vertexMutex.unlock();

	OutputDebugStringA("\n\n HERE DEBUGS!? \n\n");
}
*/
void EdgeRenderer::Render(bool isStereo)
{
	if (!loadingComplete) {
		return;
	}

	bool usingVprt = deviceResources->GetDeviceSupportsVprt();
	vertexStride = sizeof(DirectX::XMFLOAT3);
	vertexOffset = 0;

	auto context = deviceResources->GetD3DDeviceContext();

	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
	context->IASetInputLayout(inputLayout.Get());
	context->VSSetShader(vertexShader.Get(), nullptr, 0);
	context->PSSetShader(pixelShader.Get(), nullptr, 0);
	if (!usingVprt)
		context->GSSetShader(geometryShader.Get(), nullptr, 0);
	context->RSSetState(rasterizerState.Get());

	//Draw each edge-collection
	vertexMutex.lock();
	
	for (auto it = edgeBuffers.begin(); it != edgeBuffers.end(); it++) {
		auto buffers = *it;
		context->IASetVertexBuffers(
			0,
			1,
			buffers.vertexBuffer.GetAddressOf(),
			&vertexStride,
			&vertexOffset
		);

		context->VSSetConstantBuffers(0, 1, buffers.modelConstantBuffer.GetAddressOf());
		
		context->DrawInstanced(
			buffers.numVertices,
			isStereo ? 2 : 1,
			0,
			0
		);
	}
	vertexMutex.unlock();
}

//TODO IMPLEMENT "XMFLOAT3 -> I3D11BUffer" for rendering
/*
 EdgeRenderer::UpdateEdgeVertexBuffer(DirectX::XMFLOAT3* vertices) {
	return;
}
*/

void EdgeRenderer::CreateDeviceDependentResources()
{

	bool usingVprt = deviceResources->GetDeviceSupportsVprt();
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

	//TODO: CLEAR OUT VERTEX DATA
}

