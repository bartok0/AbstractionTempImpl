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

	for (auto it = edgeBuffers.begin(); it != edgeBuffers.end(); it++) {
		Windows::Foundation::Numerics::float4x4 modelData = it->coord->TryGetTransformTo(base)->Value;

		context->UpdateSubresource(
			it->modelConstantBuffer.Get(),
			0,
			NULL,
			&modelData,
			0,
			0
		);
	}
	
	//TODO UPDATE VERTICES (?)
}

void EdgeRenderer::UpdateEdgeBuffers(std::vector<DirectX::XMFLOAT3>* vertices, Windows::Perception::Spatial::SpatialCoordinateSystem ^ model, Windows::Perception::Spatial::SpatialCoordinateSystem ^ base) {
	//Guard agains data-race
	vertexMutex.lock();
	buffersReady = false;
	vertexMutex.unlock();
	auto device = deviceResources->GetD3DDevice();

	std::vector<DirectX::XMFLOAT3> transformedVertices;
	DirectX::XMMATRIX modelTransform = DirectX::XMLoadFloat4x4(&model->TryGetTransformTo(base)->Value);
	for (std::vector<DirectX::XMFLOAT3>::iterator it = vertices->begin(); it != vertices->end(); it++) {
		DirectX::XMVECTOR vertexVec = DirectX::XMLoadFloat3(&(DirectX::XMFLOAT3)(*it));
		DirectX::XMFLOAT3 transformedVertex;
		DirectX::XMStoreFloat3(&transformedVertex, DirectX::XMVector3Transform(vertexVec, modelTransform));
		transformedVertices.push_back(transformedVertex);
	}

	//edgeVertices->insert(edgeVertices->end(), transformedVertices.begin(), transformedVertices.end());
	
	for (auto it = transformedVertices.begin(); it != transformedVertices.end(); it++) {
		edgeVertices->push_back(*it);
	}
	
	vertexCount = edgeVertices->size();
	auto dataptr = edgeVertices->data();
	
	if (edgeVertexBuffer != nullptr) {
		edgeVertexBuffer = nullptr;
	}

	D3D11_BUFFER_DESC vBufferDesc;
	ZeroMemory(&vBufferDesc, sizeof(vBufferDesc));
	vBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	vBufferDesc.ByteWidth = sizeof(DirectX::XMFLOAT3) * vertexCount;
	vBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vBufferDesc.CPUAccessFlags = 0;
	vBufferDesc.MiscFlags = 0;
	vBufferDesc.StructureByteStride = sizeof(DirectX::XMFLOAT3);

	D3D11_SUBRESOURCE_DATA vBufferData;
	ZeroMemory(&vBufferData, sizeof(vBufferData));
	vBufferData.pSysMem = edgeVertices->data();
	vBufferData.SysMemPitch = 0;
	vBufferData.SysMemSlicePitch = 0;

	device->CreateBuffer(&vBufferDesc, &vBufferData, edgeVertexBuffer.GetAddressOf());

	buffersReady = true;
}

void EdgeRenderer::CreateBuffers(std::vector<DirectX::XMFLOAT3>* vertices,
	Windows::Perception::Spatial::SpatialCoordinateSystem^ meshCoord,
	Windows::Perception::Spatial::SpatialCoordinateSystem^ base)
{
	auto device = deviceResources->GetD3DDevice();

	EdgeVertexCollection newCollection;
	newCollection.vertexBuffer = Microsoft::WRL::ComPtr<ID3D11Buffer>();
	newCollection.modelConstantBuffer = Microsoft::WRL::ComPtr<ID3D11Buffer>();
	newCollection.coord = meshCoord;
	newCollection.numVertices = vertices->size();
	Windows::Foundation::Numerics::float4x4 model = meshCoord->TryGetTransformTo(base)->Value;

	D3D11_BUFFER_DESC vBufferDesc;
	vBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	vBufferDesc.ByteWidth = sizeof(DirectX::XMFLOAT3) * (unsigned int)vertices->size();
	vBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vBufferDesc.CPUAccessFlags = 0;
	vBufferDesc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA vBufferData;
	vBufferData.pSysMem = &vertices;
	vBufferData.SysMemPitch = 0;
	vBufferData.SysMemSlicePitch = 0;
	DX::ThrowIfFailed(
		device->CreateBuffer(&vBufferDesc, &vBufferData, newCollection.vertexBuffer.GetAddressOf())
	);
	/*
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
	*/

	//vertexMutex.lock();
	edgeBuffers.push_back(newCollection);
	//vertexMutex.unlock();

	OutputDebugStringA("\n\n HERE DEBUGS!? \n\n");
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
	vertexStride = sizeof(DirectX::XMFLOAT3);

	auto context = deviceResources->GetD3DDeviceContext();

	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
	context->IASetInputLayout(inputLayout.Get());
	context->VSSetShader(vertexShader.Get(), nullptr, 0);
	context->PSSetShader(pixelShader.Get(), nullptr, 0);
	if (!usingVprt)
		context->GSSetShader(geometryShader.Get(), nullptr, 0);
	context->RSSetState(rasterizerState.Get());

	context->IASetVertexBuffers(
		0,
		1,
		edgeVertexBuffer.GetAddressOf(),
		&vertexStride,
		&vertexOffset
	);

	//context->VSSetConstantBuffers(0, 1, modelTransformBuffer.GetAddressOf());

	context->DrawInstanced(
		vertexCount,
		isStereo ? 2 : 1,
		0,
		0
	);
	//Release lock after drawing the frame
	vertexMutex.unlock();
}

void EdgeRenderer::CreateDeviceDependentResources()
{
	auto device = deviceResources->GetD3DDevice();
	bool usingVprt = deviceResources->GetDeviceSupportsVprt();
	edgeVertices = new std::vector<DirectX::XMFLOAT3>;

	//Inititalize model constant buffer
	D3D11_BUFFER_DESC cBufferDesc;
	cBufferDesc.ByteWidth = sizeof(ModelConstantStruct);
	cBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	cBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cBufferDesc.CPUAccessFlags = 0;
	cBufferDesc.MiscFlags = 0;
	cBufferDesc.StructureByteStride = 0;

	device->CreateBuffer(&cBufferDesc, nullptr, modelTransformBuffer.GetAddressOf());

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

	edgeVertexBuffer.Reset();
	modelTransformBuffer.Reset();
	delete edgeVertices;
	buffersReady = false;
	loadingComplete = false;
	vertexCount = 0;
	//TODO: CLEAR OUT VERTEX DATA
}

