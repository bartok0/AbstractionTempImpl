//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "pch.h"
#include "HolographicSpatialMappingMain.h"
#include "Common\DirectXHelper.h"

#include <windows.graphics.directx.direct3d11.interop.h>
#include <Collection.h>

//---
#include "Content\GetDataFromIBuffer.h"
//---


using namespace HolographicSpatialMapping;
using namespace WindowsHolographicCodeSamples;

using namespace concurrency;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Graphics::Holographic;
using namespace Windows::Perception::Spatial;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::UI::Input::Spatial;
using namespace std::placeholders;

// Loads and initializes application assets when the application is loaded.
HolographicSpatialMappingMain::HolographicSpatialMappingMain(
	const std::shared_ptr<DX::DeviceResources>& deviceResources) :
	m_deviceResources(deviceResources)
{
	// Register to be notified if the device is lost or recreated.
	m_deviceResources->RegisterDeviceNotify(this);
}

void HolographicSpatialMappingMain::SetHolographicSpace(
	HolographicSpace^ holographicSpace)
{
	UnregisterHolographicEventHandlers();

	m_holographicSpace = holographicSpace;

#ifdef DRAW_SAMPLE_CONTENT
	// Initialize the sample hologram.
	m_meshRenderer = std::make_unique<RealtimeSurfaceMeshRenderer>(m_deviceResources);

	m_spatialInputHandler = std::make_unique<SpatialInputHandler>();
#endif

	//---
	//Initialize the edge renderer
	edgeRenderer = std::make_unique<EdgeRenderer>(m_deviceResources);
#ifdef MATLAB_DATA
	Platform::String^ localfolder = Windows::Storage::ApplicationData::Current->LocalFolder->Path;
	std::wstring folderNameW(localfolder->Begin());
	folderPath = std::string(folderNameW.begin(), folderNameW.end());
#endif
	//---

	// Use the default SpatialLocator to track the motion of the device.
	m_locator = SpatialLocator::GetDefault();

	// This sample responds to changes in the positional tracking state by cancelling deactivation 
	// of positional tracking.
	m_positionalTrackingDeactivatingToken =
		m_locator->PositionalTrackingDeactivating +=
		ref new Windows::Foundation::TypedEventHandler<SpatialLocator^, SpatialLocatorPositionalTrackingDeactivatingEventArgs^>(
			std::bind(&HolographicSpatialMappingMain::OnPositionalTrackingDeactivating, this, _1, _2)
			);


	// Respond to camera added events by creating any resources that are specific
	// to that camera, such as the back buffer render target view.
	// When we add an event handler for CameraAdded, the API layer will avoid putting
	// the new camera in new HolographicFrames until we complete the deferral we created
	// for that handler, or return from the handler without creating a deferral. This
	// allows the app to take more than one frame to finish creating resources and
	// loading assets for the new holographic camera.
	// This function should be registered before the app creates any HolographicFrames.
	m_cameraAddedToken =
		m_holographicSpace->CameraAdded +=
		ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraAddedEventArgs^>(
			std::bind(&HolographicSpatialMappingMain::OnCameraAdded, this, _1, _2)
			);

	// Respond to camera removed events by releasing resources that were created for that
	// camera.
	// When the app receives a CameraRemoved event, it releases all references to the back
	// buffer right away. This includes render target views, Direct2D target bitmaps, and so on.
	// The app must also ensure that the back buffer is not attached as a render target, as
	// shown in DeviceResources::ReleaseResourcesForBackBuffer.
	m_cameraRemovedToken =
		m_holographicSpace->CameraRemoved +=
		ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraRemovedEventArgs^>(
			std::bind(&HolographicSpatialMappingMain::OnCameraRemoved, this, _1, _2)
			);

	// This code sample uses a DeviceAttachedFrameOfReference to have the Spatial Mapping surface observer
	// follow along with the device's location.
	m_referenceFrame = m_locator->CreateAttachedFrameOfReferenceAtCurrentHeading();

	// Notes on spatial tracking APIs:
	// * Stationary reference frames are designed to provide a best-fit position relative to the
	//   overall space. Individual positions within that reference frame are allowed to drift slightly
	//   as the device learns more about the environment.
	// * When precise placement of individual holograms is required, a SpatialAnchor should be used to
	//   anchor the individual hologram to a position in the real world - for example, a point the user
	//   indicates to be of special interest. Anchor positions do not drift, but can be corrected; the
	//   anchor will use the corrected position starting in the next frame after the correction has
	//   occurred.
}

void HolographicSpatialMappingMain::UnregisterHolographicEventHandlers()
{
	if (m_holographicSpace != nullptr)
	{
		// Clear previous event registrations.

		if (m_cameraAddedToken.Value != 0)
		{
			m_holographicSpace->CameraAdded -= m_cameraAddedToken;
			m_cameraAddedToken.Value = 0;
		}

		if (m_cameraRemovedToken.Value != 0)
		{
			m_holographicSpace->CameraRemoved -= m_cameraRemovedToken;
			m_cameraRemovedToken.Value = 0;
		}
	}

	if (m_locator != nullptr)
	{
		m_locator->PositionalTrackingDeactivating -= m_positionalTrackingDeactivatingToken;
	}

	if (m_surfaceObserver != nullptr)
	{
		m_surfaceObserver->ObservedSurfacesChanged -= m_surfacesChangedToken;
		//---
		m_surfaceObserver->ObservedSurfacesChanged -= surfaceUpdateToken;
		//---
	}
}


HolographicSpatialMappingMain::~HolographicSpatialMappingMain()
{
	// Deregister device notification.
	m_deviceResources->RegisterDeviceNotify(nullptr);

	UnregisterHolographicEventHandlers();
}

void HolographicSpatialMappingMain::OnSurfacesChanged(
	SpatialSurfaceObserver^ sender,
	Object^ args)
{
	IMapView<Guid, SpatialSurfaceInfo^>^ const& surfaceCollection = sender->GetObservedSurfaces();

	// Process surface adds and updates.
	for (const auto& pair : surfaceCollection)
	{
		auto id = pair->Key;
		auto surfaceInfo = pair->Value;

		// Choose whether to add, or update the surface.
		// In this example, new surfaces are treated differently by highlighting them in a different
		// color. This allows you to observe changes in the spatial map that are due to new meshes,
		// as opposed to mesh updates.
		// In your app, you might choose to process added surfaces differently than updated
		// surfaces. For example, you might prioritize processing of added surfaces, and
		// defer processing of updates to existing surfaces.
		if (m_meshRenderer->HasSurface(id))
		{
			if (m_meshRenderer->GetLastUpdateTime(id).UniversalTime < surfaceInfo->UpdateTime.UniversalTime)
			{
				// Update existing surface.
				m_meshRenderer->UpdateSurface(id, surfaceInfo);
			}
		}
		else
		{
			// New surface.
			m_meshRenderer->AddSurface(id, surfaceInfo);
		}
	}

	// Sometimes, a mesh will fall outside the area that is currently visible to
	// the surface observer. In this code sample, we "sleep" any meshes that are
	// not included in the surface collection to avoid rendering them.
	// The system can including them in the collection again later, in which case
	// they will no longer be hidden.
	m_meshRenderer->HideInactiveMeshes(surfaceCollection);
}

//---
void HolographicSpatialMappingMain::newSurfaces(SpatialSurfaceObserver^ sender, Object^ args) {
	IMapView<Guid, SpatialSurfaceInfo^>^ const& surfaceMap = sender->GetObservedSurfaces();

	for (const auto& pair : surfaceMap) {
		auto id = pair->Key;
		auto surfaceInfo = pair->Value;

		auto it = std::find(surfaceIDs.begin(), surfaceIDs.end(), surfaceInfo->Id);
		if (it != surfaceIDs.end()) {
			//Found it in collection, do nothing
		}
		else {
			//New surface, add to collection
			surfaceIDs.push_back(surfaceInfo->Id);
			auto createMeshTask = create_task(surfaceInfo->TryComputeLatestMeshAsync(meshDensity, options));

			createMeshTask.then([this, id](SpatialSurfaceMesh^ mesh)
			{
				if (mesh != nullptr)
				{
					auto operation = create_async([this, mesh]
					{
						PopulateEdgeList(mesh);
					});
					auto populateTask = create_task(operation);
				}
			}, task_continuation_context::use_current());
		}
	}
	return;
}

bool operator== (const DirectX::XMFLOAT3 A, const DirectX::XMFLOAT3 B) {
	if (A.x == B.x &&
		A.y == B.y &&
		A.z == B.z)
	{
		return true;
	}
	return false;
}
bool operator!= (const DirectX::XMFLOAT3 A, const DirectX::XMFLOAT3 B) {
	if (A.x != B.x ||
		A.y != B.y ||
		A.z != B.z)
	{
		return true;
	}
	return false;
}
bool operator!= (const Triangle A, const Triangle B) {
	for (unsigned int i = 0; i < 3; i++) {
		if (A.triangleVertices[i] != B.triangleVertices[i]) {
			return true;
		}
	}
	return false;
}

void HolographicSpatialMapping::HolographicSpatialMappingMain::PopulateEdgeList(
	Windows::Perception::Spatial::Surfaces::SpatialSurfaceMesh^ mesh
) {
	clock_t timer;
	timer = clock();

	std::vector<UINT> indexData;
	std::vector<DirectX::XMFLOAT3> vertexData;
	std::vector<DirectX::XMFLOAT3> vertexNormalsData;

	std::vector<Triangle> meshTriangles;

	DirectX::PackedVector::XMSHORTN4* rawVertexData = (DirectX::PackedVector::XMSHORTN4*)GetDataFromIBuffer(mesh->VertexPositions->Data);
	auto vertexScale = mesh->VertexPositionScale;
	unsigned int InputVertexCount = mesh->VertexPositions->ElementCount;

	for (unsigned int index = 0; index < InputVertexCount; index++)
	{
		// read the currentPos as an XMSHORTN4. 
		DirectX::PackedVector::XMSHORTN4 currentPos = DirectX::PackedVector::XMSHORTN4(rawVertexData[index]);

		DirectX::XMFLOAT4 xmfloat;

		// XMVECTOR knows how to convert the XMSHORTN4 to actual floating point coordinates. 
		DirectX::XMVECTOR xmvec = XMLoadShortN4(&currentPos);

		// STore that into an XMFLOAT4 so we can read the values.
		DirectX::XMStoreFloat4(&xmfloat, xmvec);

		DirectX::XMFLOAT4 scaledVector = DirectX::XMFLOAT4(xmfloat.x*vertexScale.x, xmfloat.y*vertexScale.y, xmfloat.z*vertexScale.z, xmfloat.w);

		vertexData.push_back(DirectX::XMFLOAT3(scaledVector.x, scaledVector.y, scaledVector.z));
	}

	DirectX::PackedVector::XMBYTEN4* rawNormalData = (DirectX::PackedVector::XMBYTEN4*)GetDataFromIBuffer(mesh->VertexNormals->Data);
	unsigned int InputNormalsCount = mesh->VertexNormals->ElementCount;

	for (unsigned int index = 0; index < InputNormalsCount; index++)
	{
		// read the currentPos as an XMSHORTN4. 
		DirectX::PackedVector::XMBYTEN4 currentPos = DirectX::PackedVector::XMBYTEN4(rawNormalData[index]);
		DirectX::XMFLOAT4 xmfloat;

		// XMVECTOR knows how to convert the XMSHORTN4 to actual floating point coordinates. 
		DirectX::XMVECTOR xmvec = DirectX::PackedVector::XMLoadByteN4(&currentPos);

		// STore that into an XMFLOAT4 so we can read the values.
		DirectX::XMStoreFloat4(&xmfloat, xmvec);

		// Which need to be scaled by the vertex scale.
		DirectX::XMFLOAT4 scaledVector = DirectX::XMFLOAT4(xmfloat.x, xmfloat.y, xmfloat.z, xmfloat.w);

		vertexNormalsData.push_back(DirectX::XMFLOAT3(scaledVector.x, scaledVector.y, scaledVector.z));
	}

	DirectX::PackedVector::XMUSHORT4* rawIndexData = (DirectX::PackedVector::XMUSHORT4*)GetDataFromIBuffer(mesh->TriangleIndices->Data);
	unsigned int InputIndexCount = mesh->TriangleIndices->ElementCount / 4;
	for (unsigned int index = 0; index < InputIndexCount; index++)
	{
		// read the currentPos as an XMSHORTN4.
		//DirectX::XMUINT4 currentPos = DirectX::XMUINT4(rawIndexData[index]);
		DirectX::PackedVector::XMUSHORT4 currentPos = (DirectX::PackedVector::XMUSHORT4)rawIndexData[index];
		DirectX::XMUINT4 xmfloat;

		// XMVECTOR knows how to convert the XMSHORTN4 to actual floating point coordinates. 
		DirectX::XMVECTOR xmvec = DirectX::PackedVector::XMLoadUShort4(&currentPos);

		// STore that into an XMFLOAT4 so we can read the values.
		DirectX::XMStoreUInt4(&xmfloat, xmvec);

		// Which need to be scaled by the vertex scale.
		//DirectX::PackedVector::XMUSHORT4 scaledVector = DirectX::PackedVector::XMUSHORT4(xmfloat.x, xmfloat.y, xmfloat.z, xmfloat.w);
		//indexData.push_back(DirectX::XMUINT3(xmfloat.x, xmfloat.y, xmfloat.z));

		indexData.push_back(xmfloat.x);
		indexData.push_back(xmfloat.y);
		indexData.push_back(xmfloat.z);
		indexData.push_back(xmfloat.w);
	}

	//Construct triangles
	for (std::vector<UINT>::iterator it = indexData.begin(); it != indexData.end() && it + 1 != indexData.end() && it + 2 != indexData.end(); it += 3) {
		meshTriangles.push_back(Triangle(vertexData[*it], vertexData[*(it + 1)], vertexData[*(it + 2)],
			vertexNormalsData[*it], vertexNormalsData[*(it + 1)], vertexNormalsData[*(it + 2)]));
	}

	Kdtree tree = Kdtree();
	Kdtree::Node* rootNode = tree.Create(meshTriangles, 0, 100);
	for (Triangle triangle : meshTriangles) {
		tree.Insert(triangle, rootNode);
	}

	std::vector<DirectX::XMFLOAT3> edgeVertices;
	std::vector<DirectX::XMFLOAT3> neighbourNormals;
	std::vector<Triangle> localTriangles;

	//Populate edgelist
	std::vector<DirectX::XMFLOAT3> vertexPositions;
	Windows::Perception::Spatial::SpatialCoordinateSystem^ modelCoord = mesh->CoordinateSystem;
	
	for (Triangle triangleA : meshTriangles) {
		//localTriangles = tree.SearchPos(triangleA.position, rootNode)->triangles;
		localTriangles = tree.SearchTri(triangleA, rootNode);
		for (Triangle triangleB : localTriangles) {
			if (triangleA != triangleB) {
				edgeVertices.clear();
				neighbourNormals.clear();

				for (int i = 0; i < 3; i++) {
					DirectX::XMFLOAT3 A = triangleA.triangleVertices[i];
					for (int j = 0; j < 3; j++) {
						if (A == triangleB.triangleVertices[j]) {
							edgeVertices.push_back(A);
						}
					}
				}

				if (edgeVertices.size() > 1) {
					//The triangles have a shared edge, calculate the edge weight
					
					for (int i = 0; i < 3; i++) {
						if (triangleA.triangleVertices[i] != edgeVertices[0] || triangleA.triangleVertices[i] != edgeVertices[1])
							neighbourNormals.push_back(triangleA.triangleNormals[i]);
						if (triangleB.triangleVertices[i] != edgeVertices[0] || triangleB.triangleVertices[i] != edgeVertices[1])
							neighbourNormals.push_back(triangleB.triangleNormals[i]);
					}

					float edgeWeight = 0.0f;

					switch (mode) {
					case SOD:
						edgeWeight = CalculateSODWeight(triangleA, triangleB);
						break;
					case ESOD:
						edgeWeight = CalculateESODWeight(neighbourNormals[0], neighbourNormals[1]);
						break;
					default:
						OutputDebugStringA("\nNO MODE SELECTED!\n");
					}
					if (edgeWeight > weightThreshold) {
						vertexPositions.push_back(edgeVertices[0]);
						vertexPositions.push_back(edgeVertices[1]);
					}
				}
			}
		}
	}
	
	/*
	for (auto it1 = indexData.begin(); it1 != indexData.end(); it1 += 3) {
		//There is no guarantee that 'index/vertex/normal count' % 3 = 0.
		if (it1 + 1 == indexData.end() || it1 + 2 == indexData.end()) {
			break;
		}
		unsigned int TriangleAIndices[3] = { *it1,*(it1 + 1),*(it1 + 2) };

		for (auto it2 = it1 + 3; it2 != indexData.end(); it2 += 3) {
			//There is no guarantee that 'index/vertex/normal count' % 3 = 0.
			if (it2 + 1 == indexData.end() || it2 + 2 == indexData.end()) {
				break;
			}
			unsigned int TriangleBIndices[3] = { *it2,*(it2 + 1),*(it2 + 2) };

			std::vector<UINT16> edgeIndices = {};
			std::vector<UINT16> neighbourIndices = {};

			for (int i = 0; i < 3; i++) {
				UINT16 A = TriangleAIndices[i];
				for (int j = 0; j < 3; j++) {
					if (A == TriangleBIndices[j])
						edgeIndices.push_back(A);

					if (edgeIndices.size() > 1) {
						//The triangles have a shared edge, Construct the new edge-data and add it to the edgelist
						UINT16 edgeIndexA = edgeIndices[0];
						UINT16 edgeIndexB = edgeIndices[1];

						for (int i = 0; i < 3; i++) {
							if (TriangleAIndices[i] != edgeIndexA || TriangleAIndices[i] != edgeIndexB)
								neighbourIndices.push_back(TriangleAIndices[i]);
							if (TriangleBIndices[i] != edgeIndexA || TriangleBIndices[i] != edgeIndexB)
								neighbourIndices.push_back(TriangleBIndices[i]);
						}

						float edgeWeight = 0.0f;

						DirectX::XMFLOAT3 triA[] = { vertexData[TriangleAIndices[0]] , vertexData[TriangleAIndices[1]] , vertexData[TriangleAIndices[2]] };
						DirectX::XMFLOAT3 triB[] = { vertexData[TriangleBIndices[0]], vertexData[TriangleBIndices[1]], vertexData[TriangleBIndices[2]] };

						switch (mode) {
						case SOD:
							edgeWeight = CalculateSODWeight(triA, triB);
							break;
						case ESOD:
							edgeWeight = CalculateESODWeight(DirectX::XMFLOAT3(vertexNormalsData[neighbourIndices[0]]), DirectX::XMFLOAT3(vertexNormalsData[neighbourIndices[1]]));
							break;
						default:
							OutputDebugStringA("\nNO MODE SELECTED!\n");
						}
						if (edgeWeight > weightThreshold) {
							vertexPositions.push_back(vertexData[edgeIndices[0]]);
							vertexPositions.push_back(vertexData[edgeIndices[1]]);
						}

						break;
					}
				}
				if (edgeIndices.size() > 1)
					break;
			}

		}

	}
	*/
	
	//At least 2 vertices are required to draw a line
	if (vertexPositions.size() > 1) {
		std::vector<DirectX::XMFLOAT3>* vertexPtr = &vertexPositions;
		edgeRenderer->CreateBuffer(vertexPtr, modelCoord);

#ifdef MATLAB_DATA
		matLock.lock();
		char fileName[1024];
		sprintf_s(fileName,1024,"%s\\EdgeVertices%d.txt",folderPath.c_str(),surfcount);
		//sprintf_s(fileName, 1024, "%s\\VertexData%d.txt", folderPath.c_str(),surfcount);
		

		//std::ofstream stream(fileName, std::ofstream::app);
		std::ofstream stream(fileName,std::ofstream::app);
		Windows::Perception::Spatial::SpatialLocator ^ loc = Windows::Perception::Spatial::SpatialLocator::GetDefault();
		auto locReference = loc->CreateStationaryFrameOfReferenceAtCurrentLocation(Windows::Foundation::Numerics::float3(0, 0, 0), Windows::Foundation::Numerics::quaternion::identity());
		DirectX::XMMATRIX mvp = DirectX::XMLoadFloat4x4(&mesh->CoordinateSystem->TryGetTransformTo(locReference->CoordinateSystem)->Value);
		DirectX::XMMATRIX normalTransform = mvp;
		normalTransform.r[3] = XMVectorSet(0.f, 0.f, 0.f, XMVectorGetW(normalTransform.r[3]));
		normalTransform = XMMatrixTranspose(normalTransform);
		
		if (stream) {

			for (DirectX::XMFLOAT3 v : vertexPositions) {
			//for (DirectX::XMFLOAT3 v : vertexData) {
				DirectX::XMFLOAT3 v_t;
				DirectX::XMStoreFloat3(&v_t,DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&v), mvp));

				stream << std::to_string(v_t.x).c_str() << " " << std::to_string(v_t.y).c_str() << " " << std::to_string(v_t.z).c_str() << "\n";
			}
		}

		
		char fileName2[1024];
		sprintf_s(fileName2, 1024, "%s\\VertexNormalData%d.txt", folderPath.c_str(),surfcount);
		std::ofstream stream2(fileName2, std::ofstream::app);
		if (stream2) {
			for (int i = 0; i < vertexData.size(); i++) {
				DirectX::XMFLOAT3 v_t;
				//DirectX::XMFLOAT3 n_t = vertexNormalsData[i];
				DirectX::XMFLOAT3 n_t;
				DirectX::XMStoreFloat3(&v_t, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&vertexData[i]), mvp));
				DirectX::XMStoreFloat3(&n_t, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&vertexNormalsData[i]), normalTransform));
				stream2 << std::to_string(v_t.x).c_str() << " " << std::to_string(v_t.y).c_str() << " " << std::to_string(v_t.z).c_str() << " ";
				stream2 << std::to_string(n_t.x).c_str() << " " << std::to_string(n_t.y).c_str() << " " << std::to_string(n_t.z).c_str() << "\n";
			}
		}
		

		//PLY FILE INPUT
		char fileName3[1024];
		sprintf_s(fileName3, 1024, "%s\\MeshData%d.ply", folderPath.c_str(), surfcount);
		std::ofstream stream3(fileName3, std::ofstream::app);
		if (stream3) {
			//INIT BLOCK
			stream3 << "ply\nformat ascii 1.0\nelement vertex ";
			stream3 << vertexData.size() << "\n"; //NUMBER OF VERTS
			stream3 << "property float32 x\nproperty float32 y\nproperty float32 z\nelement face ";
			stream3 << indexData.size()/3 << "\n"; //NUMBER OF TRIANGLES
			stream3 << "property list uint8 int32 vertex_index\nend_header\n";
			//END OF INIT
			//ADD VERTICES
			for (DirectX::XMFLOAT3 v : vertexData) {
				DirectX::XMFLOAT3 v_t;
				DirectX::XMStoreFloat3(&v_t, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&v), mvp));
				stream3 << std::to_string(v_t.x).c_str() << " " << std::to_string(v_t.y).c_str() << " " << std::to_string(v_t.z).c_str() << "\n";
			}
			//ADD INDICES/FACES
			for (std::vector<UINT>::iterator it = indexData.begin(); it != indexData.end() && it + 1 != indexData.end() && it + 2 != indexData.end(); it += 3) {
				stream3 << "3 " << *it << " " << *(it + 1) << " " << *(it + 2) << "\n";
			}
			stream3.seekp(-2, std::ios_base::cur);
		}

		surfcount++;
		stream.close();
		stream2.close();
		stream3.close();
		matLock.unlock();
#endif

		//Time measurement
		char buffer[255];
		timer = clock() - timer;
		sprintf_s(buffer, 255, "Sent to render, took %f seconds.\n", (float)timer / CLOCKS_PER_SEC);
		OutputDebugStringA(buffer);
	}
	return;
}

DirectX::XMVECTOR normal(DirectX::XMVECTOR triangleP1, DirectX::XMVECTOR triangleP2, DirectX::XMVECTOR triangleP3) {

	DirectX::XMFLOAT3 normal;
	DirectX::XMVECTOR U = DirectX::XMVectorSubtract(triangleP2, triangleP1);
	DirectX::XMVECTOR V = DirectX::XMVectorSubtract(triangleP3, triangleP1);

	normal.x = (DirectX::XMVectorGetY(U)*DirectX::XMVectorGetZ(V)) - (DirectX::XMVectorGetZ(U)*DirectX::XMVectorGetY(V));
	normal.y = (DirectX::XMVectorGetZ(U)*DirectX::XMVectorGetX(V)) - (DirectX::XMVectorGetX(U)*DirectX::XMVectorGetZ(V));
	normal.z = (DirectX::XMVectorGetX(U)*DirectX::XMVectorGetY(V)) - (DirectX::XMVectorGetY(U)*DirectX::XMVectorGetX(V));

	DirectX::XMVECTOR out = { normal.x, normal.y, normal.z };
	return out;
}

float HolographicSpatialMapping::HolographicSpatialMappingMain::CalculateSODWeight(DirectX::XMFLOAT3 triangleA[3], DirectX::XMFLOAT3 triangleB[3]) {
	DirectX::XMVECTOR A1 = { triangleA[0].x,triangleA[0].y,triangleA[0].z };
	DirectX::XMVECTOR A2 = { triangleA[1].x,triangleA[1].y,triangleA[1].z };
	DirectX::XMVECTOR A3 = { triangleA[2].x,triangleA[2].y,triangleA[2].z };

	DirectX::XMVECTOR B1 = { triangleB[0].x,triangleB[0].y,triangleB[0].z };
	DirectX::XMVECTOR B2 = { triangleB[1].x,triangleB[1].y,triangleB[1].z };
	DirectX::XMVECTOR B3 = { triangleB[2].x,triangleB[2].y,triangleB[2].z };

	DirectX::XMVECTOR triangleANormal = normal(A1, A2, A3);
	DirectX::XMVECTOR triangleBNormal = normal(B1, B2, B3);

	return DirectX::XMScalarACos(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVector3Normalize(triangleANormal), DirectX::XMVector3Normalize(triangleBNormal))));
}

float HolographicSpatialMapping::HolographicSpatialMappingMain::CalculateESODWeight(DirectX::XMFLOAT3 vertexANormal, DirectX::XMFLOAT3 vertexBNormal) {
	DirectX::XMVECTOR vertexAnormal = DirectX::XMLoadFloat3(&vertexANormal);
	DirectX::XMVECTOR vertexBnormal = DirectX::XMLoadFloat3(&vertexBNormal);

	return DirectX::XMScalarACos(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVector3Normalize(vertexAnormal), DirectX::XMVector3Normalize(vertexBnormal))));
}

float HolographicSpatialMapping::HolographicSpatialMappingMain::CalculateSODWeight(Triangle triangleA, Triangle triangleB) {
	DirectX::XMVECTOR A1 = { triangleA.triangleVertices[0].x,triangleA.triangleVertices[0].y,triangleA.triangleVertices[0].z };
	DirectX::XMVECTOR A2 = { triangleA.triangleVertices[1].x,triangleA.triangleVertices[1].y,triangleA.triangleVertices[1].z };
	DirectX::XMVECTOR A3 = { triangleA.triangleVertices[2].x,triangleA.triangleVertices[2].y,triangleA.triangleVertices[2].z };

	DirectX::XMVECTOR B1 = { triangleB.triangleVertices[0].x,triangleB.triangleVertices[0].y,triangleB.triangleVertices[0].z };
	DirectX::XMVECTOR B2 = { triangleB.triangleVertices[1].x,triangleB.triangleVertices[1].y,triangleB.triangleVertices[1].z };
	DirectX::XMVECTOR B3 = { triangleB.triangleVertices[2].x,triangleB.triangleVertices[2].y,triangleB.triangleVertices[2].z };

	DirectX::XMVECTOR triangleANormal = normal(A1, A2, A3);
	DirectX::XMVECTOR triangleBNormal = normal(B1, B2, B3);

	return DirectX::XMScalarACos(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVector3Normalize(triangleANormal), DirectX::XMVector3Normalize(triangleBNormal))));
}
//---

// Updates the application state once per frame.
HolographicFrame^ HolographicSpatialMappingMain::Update()
{
	// Before doing the timer update, there is some work to do per-frame
	// to maintain holographic rendering. First, we will get information
	// about the current frame.

	// The HolographicFrame has information that the app needs in order
	// to update and render the current frame. The app begins each new
	// frame by calling CreateNextFrame.
	HolographicFrame^ holographicFrame = m_holographicSpace->CreateNextFrame();

	// Get a prediction of where holographic cameras will be when this frame
	// is presented.
	HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;

	// Back buffers can change from frame to frame. Validate each buffer, and recreate
	// resource views and depth buffers as needed.
	m_deviceResources->EnsureCameraResources(holographicFrame, prediction);

	// Next, we get a coordinate system from the attached frame of reference that is
	// associated with the current frame. Later, this coordinate system is used for
	// for creating the stereo view matrices when rendering the sample content.
	SpatialCoordinateSystem^ currentCoordinateSystem = m_referenceFrame->GetStationaryCoordinateSystemAtTimestamp(prediction->Timestamp);

	// Only create a surface observer when you need to - do not create a new one each frame.
	if (m_surfaceObserver == nullptr)
	{
		// Initialize the Surface Observer using a valid coordinate system.
		if (!m_spatialPerceptionAccessRequested)
		{
			// The spatial mapping API reads information about the user's environment. The user must
			// grant permission to the app to use this capability of the Windows Holographic device.
			auto initSurfaceObserverTask = create_task(SpatialSurfaceObserver::RequestAccessAsync());
			initSurfaceObserverTask.then([this, currentCoordinateSystem](Windows::Perception::Spatial::SpatialPerceptionAccessStatus status)
			{
				switch (status)
				{
				case SpatialPerceptionAccessStatus::Allowed:
					m_surfaceAccessAllowed = true;
					break;
				default:
					// Access was denied. This usually happens because your AppX manifest file does not declare the
					// spatialPerception capability.
					// For info on what else can cause this, see: http://msdn.microsoft.com/library/windows/apps/mt621422.aspx
					m_surfaceAccessAllowed = false;
					break;
				}
			});

			m_spatialPerceptionAccessRequested = true;
		}
	}

	if (m_surfaceAccessAllowed)
	{
		SpatialBoundingBox axisAlignedBoundingBox =
		{
			{  0.f,  0.f, 0.f },
			{ 20.f, 20.f, 5.f },
		};
		SpatialBoundingVolume^ bounds = SpatialBoundingVolume::FromBox(currentCoordinateSystem, axisAlignedBoundingBox);

		// If status is Allowed, we can create the surface observer.
		if (m_surfaceObserver == nullptr)
		{
			// First, we'll set up the surface observer to use our preferred data formats.
			// In this example, a "preferred" format is chosen that is compatible with our precompiled shader pipeline.
			m_surfaceMeshOptions = ref new SpatialSurfaceMeshOptions();
			IVectorView<DirectXPixelFormat>^ supportedVertexPositionFormats = m_surfaceMeshOptions->SupportedVertexPositionFormats;
			unsigned int formatIndex = 0;
			if (supportedVertexPositionFormats->IndexOf(DirectXPixelFormat::R16G16B16A16IntNormalized, &formatIndex))
			{
				m_surfaceMeshOptions->VertexPositionFormat = DirectXPixelFormat::R16G16B16A16IntNormalized;
			}
			IVectorView<DirectXPixelFormat>^ supportedVertexNormalFormats = m_surfaceMeshOptions->SupportedVertexNormalFormats;
			if (supportedVertexNormalFormats->IndexOf(DirectXPixelFormat::R8G8B8A8IntNormalized, &formatIndex))
			{
				m_surfaceMeshOptions->VertexNormalFormat = DirectXPixelFormat::R8G8B8A8IntNormalized;
			}

			// If you are using a very high detail setting with spatial mapping, it can be beneficial
			// to use a 32-bit unsigned integer format for indices instead of the default 16-bit. 
			// Uncomment the following code to enable 32-bit indices.
			//IVectorView<DirectXPixelFormat>^ supportedTriangleIndexFormats = m_surfaceMeshOptions->SupportedTriangleIndexFormats;
			//if (supportedTriangleIndexFormats->IndexOf(DirectXPixelFormat::R32UInt, &formatIndex))
			//{
			//    m_surfaceMeshOptions->TriangleIndexFormat = DirectXPixelFormat::R32UInt;
			//}

			// Create the observer.
			m_surfaceObserver = ref new SpatialSurfaceObserver();
			if (m_surfaceObserver)
			{
				m_surfaceObserver->SetBoundingVolume(bounds);

				// If the surface observer was successfully created, we can initialize our
				// collection by pulling the current data set.
				auto mapContainingSurfaceCollection = m_surfaceObserver->GetObservedSurfaces();
				for (auto const& pair : mapContainingSurfaceCollection)
				{
					// Store the ID and metadata for each surface.
					auto const& id = pair->Key;
					auto const& surfaceInfo = pair->Value;
					m_meshRenderer->AddSurface(id, surfaceInfo);

				}

				// We then subcribe to an event to receive up-to-date data.
				m_surfacesChangedToken = m_surfaceObserver->ObservedSurfacesChanged +=
					ref new TypedEventHandler<SpatialSurfaceObserver^, Platform::Object^>(
						bind(&HolographicSpatialMappingMain::OnSurfacesChanged, this, _1, _2)
						);
			}
		}

		// Keep the surface observer positioned at the device's location.
		m_surfaceObserver->SetBoundingVolume(bounds);

		// Note that it is possible to set multiple bounding volumes. Pseudocode:
		//     m_surfaceObserver->SetBoundingVolumes(/* iterable collection of bounding volumes*/);
		//
		// It is also possible to use other bounding shapes - such as a view frustum. Pseudocode:
		//     SpatialBoundingVolume^ bounds = SpatialBoundingVolume::FromFrustum(coordinateSystem, viewFrustum);
		//     m_surfaceObserver->SetBoundingVolume(bounds);
	}


	//---

	//Pull vertex data
	if (needSpatialMapping && m_surfaceObserver) {
		options = ref new SpatialSurfaceMeshOptions();
		options->IncludeVertexNormals = true;

		auto surfaceMap = m_surfaceObserver->GetObservedSurfaces();

		for (auto const& pair : surfaceMap)
		{
			// Store the ID and metadata for each surface.
			auto const& id = pair->Key;
			auto const& surfaceInfo = pair->Value;

			surfaceIDs.push_back(surfaceInfo->Id);

			auto createMeshTask = create_task(surfaceInfo->TryComputeLatestMeshAsync(meshDensity, options));

			createMeshTask.then([this, id, currentCoordinateSystem](SpatialSurfaceMesh^ mesh)
			{
				if (mesh != nullptr)
				{
					auto operation = create_async([this, mesh, currentCoordinateSystem]
					{
						PopulateEdgeList(mesh);
					});
					auto populateTask = create_task(operation);
				}
			}, task_continuation_context::use_current());
		}

		//Register for updates
		surfaceUpdateToken = m_surfaceObserver->ObservedSurfacesChanged +=
			ref new TypedEventHandler<SpatialSurfaceObserver^, Platform::Object^>(
				bind(&HolographicSpatialMappingMain::newSurfaces, this, _1, _2)
				);

		needSpatialMapping = false;
	}
	//---

#ifdef DRAW_SAMPLE_CONTENT
	// Check for new input state since the last frame.
	SpatialInteractionSourceState^ pointerState = m_spatialInputHandler->CheckForInput();
	if (pointerState != nullptr)
	{
		// When a Pressed gesture is detected, the rendering mode will be changed to wireframe.
		m_drawWireframe = !m_drawWireframe;
	}
#endif

	m_timer.Tick([&]()
	{
#ifdef DRAW_SAMPLE_CONTENT
		m_meshRenderer->Update(m_timer, currentCoordinateSystem);
#endif
	});


	//---
	//Update MVP transform
	edgeRenderer->Update(currentCoordinateSystem);
	//---

	// This sample uses default image stabilization settings, and does not set the focus point.

	// The holographic frame will be used to get up-to-date view and projection matrices and
	// to present the swap chain.
	return holographicFrame;
}

// Renders the current frame to each holographic camera, according to the
// current application and spatial positioning state. Returns true if the
// frame was rendered to at least one camera.
bool HolographicSpatialMappingMain::Render(
	HolographicFrame^ holographicFrame)
{
	// Don't try to render anything before the first Update.
	if (m_timer.GetFrameCount() == 0)
	{
		return false;
	}

	// Lock the set of holographic camera resources, then draw to each camera
	// in this frame.
	return m_deviceResources->UseHolographicCameraResources<bool>(
		[this, holographicFrame](std::map<UINT32, std::unique_ptr<DX::CameraResources>>& cameraResourceMap)
	{
		// Up-to-date frame predictions enhance the effectiveness of image stablization and
		// allow more accurate positioning of holograms.
		holographicFrame->UpdateCurrentPrediction();
		HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;
		SpatialCoordinateSystem^ currentCoordinateSystem = m_referenceFrame->GetStationaryCoordinateSystemAtTimestamp(prediction->Timestamp);

		bool atLeastOneCameraRendered = false;
		for (auto cameraPose : prediction->CameraPoses)
		{
			// This represents the device-based resources for a HolographicCamera.
			DX::CameraResources* pCameraResources = cameraResourceMap[cameraPose->HolographicCamera->Id].get();

			// Get the device context.
			const auto context = m_deviceResources->GetD3DDeviceContext();
			const auto depthStencilView = pCameraResources->GetDepthStencilView();

			// Set render targets to the current holographic camera.
			ID3D11RenderTargetView *const targets[1] = { pCameraResources->GetBackBufferRenderTargetView() };
			context->OMSetRenderTargets(1, targets, depthStencilView);

			// Clear the back buffer and depth stencil view.
			context->ClearRenderTargetView(targets[0], DirectX::Colors::Transparent);
			context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

			// The view and projection matrices for each holographic camera will change
			// every frame. This function refreshes the data in the constant buffer for
			// the holographic camera indicated by cameraPose.
			pCameraResources->UpdateViewProjectionBuffer(m_deviceResources, cameraPose, currentCoordinateSystem);

			// Attach the view/projection constant buffer for this camera to the graphics pipeline.
			bool cameraActive = pCameraResources->AttachViewProjectionBuffer(m_deviceResources);

#ifdef DRAW_SAMPLE_CONTENT
			// Only render world-locked content when positional tracking is active.
			if (cameraActive)
			{
				//Draw the sample hologram.
				//m_meshRenderer->Render(pCameraResources->IsRenderingStereoscopic(), m_drawWireframe);
			}
#endif

			//---
			if (cameraActive)
			{
				edgeRenderer->Render(pCameraResources->IsRenderingStereoscopic());
			}
			//---

			atLeastOneCameraRendered = true;
		}

		return atLeastOneCameraRendered;
	});
}

void HolographicSpatialMappingMain::SaveAppState()
{
	// This sample does not persist any state between sessions.

}

void HolographicSpatialMappingMain::LoadAppState()
{
	// This sample does not persist any state between sessions.
}

// Notifies classes that use Direct3D device resources that the device resources
// need to be released before this method returns.
void HolographicSpatialMappingMain::OnDeviceLost()
{
	//---
	edgeRenderer->ReleaseDeviceDependentResources();
	//---

#ifdef DRAW_SAMPLE_CONTENT
	m_meshRenderer->ReleaseDeviceDependentResources();
#endif
}

// Notifies classes that use Direct3D device resources that the device resources
// may now be recreated.
void HolographicSpatialMappingMain::OnDeviceRestored()
{
#ifdef DRAW_SAMPLE_CONTENT
	m_meshRenderer->CreateDeviceDependentResources();
#endif
	//---
	edgeRenderer->CreateDeviceDependentResources();
	//---
}

void HolographicSpatialMappingMain::OnPositionalTrackingDeactivating(
	SpatialLocator^ sender,
	SpatialLocatorPositionalTrackingDeactivatingEventArgs^ args)
{
	// Without positional tracking, spatial meshes will not be locatable.
	args->Canceled = true;
}

void HolographicSpatialMappingMain::OnCameraAdded(
	HolographicSpace^ sender,
	HolographicSpaceCameraAddedEventArgs^ args)
{
	Deferral^ deferral = args->GetDeferral();
	HolographicCamera^ holographicCamera = args->Camera;
	create_task([this, deferral, holographicCamera]()
	{
		// Create device-based resources for the holographic camera and add it to the list of
		// cameras used for updates and rendering. Notes:
		//   * Since this function may be called at any time, the AddHolographicCamera function
		//     waits until it can get a lock on the set of holographic camera resources before
		//     adding the new camera. At 60 frames per second this wait should not take long.
		//   * A subsequent Update will take the back buffer from the RenderingParameters of this
		//     camera's CameraPose and use it to create the ID3D11RenderTargetView for this camera.
		//     Content can then be rendered for the HolographicCamera.
		m_deviceResources->AddHolographicCamera(holographicCamera);

		// Holographic frame predictions will not include any information about this camera until
		// the deferral is completed.
		deferral->Complete();
	});
}

void HolographicSpatialMappingMain::OnCameraRemoved(
	HolographicSpace^ sender,
	HolographicSpaceCameraRemovedEventArgs^ args)
{
	// Before letting this callback return, ensure that all references to the back buffer
	// are released.
	// Since this function may be called at any time, the RemoveHolographicCamera function
	// waits until it can get a lock on the set of holographic camera resources before
	// deallocating resources for this camera. At 60 frames per second this wait should
	// not take long.
	m_deviceResources->RemoveHolographicCamera(args->Camera);
}
