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
void HolographicSpatialMapping::HolographicSpatialMappingMain::PopulateEdgeList(
	Windows::Perception::Spatial::Surfaces::SpatialSurfaceMesh^ mesh
) {
	std::vector<unsigned short> indexData;
	std::vector<DirectX::XMFLOAT3> vertexData;
	std::vector<DirectX::XMFLOAT3> vertexNormalsData;

	DirectX::PackedVector::XMSHORTN4* rawVertexData = (DirectX::PackedVector::XMSHORTN4*)GetDataFromIBuffer(mesh->VertexPositions->Data);
	float3 vertexScale = mesh->VertexPositionScale;
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

		// Which need to be scaled by the vertex scale.
		DirectX::XMFLOAT4 scaledVector = DirectX::XMFLOAT4(xmfloat.x*vertexScale.x, xmfloat.y*vertexScale.y, xmfloat.z*vertexScale.z, xmfloat.w);

		vertexData.push_back(DirectX::XMFLOAT3(scaledVector.x, scaledVector.y, scaledVector.z));
		//float4 nextFloat = float4(scaledVector.x, scaledVector.y, scaledVector.z, scaledVector.w);
	}

	//STORE IN VERTEX GLOBAL(?)

	DirectX::PackedVector::XMSHORTN4* rawNormalData = (DirectX::PackedVector::XMSHORTN4*)GetDataFromIBuffer(mesh->VertexNormals->Data);
	unsigned int InputNormalsCount = mesh->VertexNormals->ElementCount;

	for (unsigned int index = 0; index < InputNormalsCount; index++)
	{
		// read the currentPos as an XMSHORTN4. 
		DirectX::PackedVector::XMSHORTN4 currentPos = DirectX::PackedVector::XMSHORTN4(rawNormalData[index]);
		DirectX::XMFLOAT4 xmfloat;

		// XMVECTOR knows how to convert the XMSHORTN4 to actual floating point coordinates. 
		DirectX::XMVECTOR xmvec = XMLoadShortN4(&currentPos);

		// STore that into an XMFLOAT4 so we can read the values.
		DirectX::XMStoreFloat4(&xmfloat, xmvec);

		// Which need to be scaled by the vertex scale.
		DirectX::XMFLOAT4 scaledVector = DirectX::XMFLOAT4(xmfloat.x, xmfloat.y, xmfloat.z, xmfloat.w);

		vertexNormalsData.push_back(DirectX::XMFLOAT3(scaledVector.x, scaledVector.y, scaledVector.z));
	}

	//STORE IN NORMAL GLOBAL(?)

	DirectX::XMUINT4* rawIndexData = (DirectX::XMUINT4*)GetDataFromIBuffer(mesh->TriangleIndices->Data);
	unsigned int InputIndexCount = mesh->TriangleIndices->ElementCount / 4;

	for (unsigned int index = 0; index < InputIndexCount; index++)
	{
		// read the currentPos as an XMSHORTN4.
		DirectX::XMUINT4 currentPos = DirectX::XMUINT4(rawIndexData[index]);
		DirectX::XMUINT4 xmfloat;

		// XMVECTOR knows how to convert the XMSHORTN4 to actual floating point coordinates. 
		DirectX::XMVECTOR xmvec = DirectX::XMLoadUInt4(&currentPos);

		// STore that into an XMFLOAT4 so we can read the values.
		DirectX::XMStoreUInt4(&xmfloat, xmvec);

		// Which need to be scaled by the vertex scale.
		//DirectX::PackedVector::XMUSHORT4 scaledVector = DirectX::PackedVector::XMUSHORT4(xmfloat.x, xmfloat.y, xmfloat.z, xmfloat.w);
		indexData.push_back(xmfloat.x);
		indexData.push_back(xmfloat.y);
		indexData.push_back(xmfloat.z);
		indexData.push_back(xmfloat.w);
	}

	//STORE IN INDEX GLOBAL(?)

	char msgbuffer[512];
	unsigned int val = (unsigned int)indexData.size();
	sprintf_s(msgbuffer, 512, "\nNumber of indices: %u \nNumber of vertices: %u\nNumber of normals: %u\n", val, InputVertexCount, InputNormalsCount);
	OutputDebugStringA(msgbuffer);

	//DEBUG PRINTS
	/*
	auto it = indexData.begin();
	auto one = *it;
	auto two = *(it + 1);
	auto three = *(it + 2);
	auto four = *(it + 3);
	auto five = *(it + 4);
	auto check = *(it + 5);

	char msgbuf4[255];
	sprintf_s(msgbuf4, 255, "\n0: %u \n 1: %u \n 2: %u \n 3: %u \n 4: %u \n check: %u\n", one, two, three, four, five, check);
	OutputDebugStringA(msgbuf4);


	auto itt = vertexData.begin();
	auto onef = itt->x;
	auto twof = itt->y;
	auto threef = itt->z;
	auto fourf = (itt + 1)->x;
	auto fivef = (itt + 1)->y;
	auto checkf = (itt + 1)->z;

	char msgbuf5[255];
	sprintf_s(msgbuf5, 255, "\n0: %f \n 1: %f \n 2: %f \n 3: %f \n 4: %f \n check: %f\n\n", onef, twof, threef, fourf, fivef, checkf);
	OutputDebugStringA(msgbuf5);
	*/
	//Populate edgelist
	GUID meshID = mesh->SurfaceInfo->Id;
	int edgeCounter = 0;

	//WAY TOO SLOW... parallellize?

	for (auto it1 = indexData.begin(); it1 != indexData.end(); it1 += 3) {

		//There is no guarantee that 'index count' % 3 = 0.
		if ((it1 + 1) == indexData.end())
			break;
		if ((it1 + 2) == indexData.end())
			break;

		unsigned short TriangleAIndices[3] = { *it1,*(it1 + 1),*(it1 + 2) };

		for (auto it2 = it1 + 3; it2 != indexData.end(); it2 += 3) {
			//There is no guarantee that 'index count' % 3 = 0.
			if ((it2 + 1) == indexData.end())
				break;
			if ((it2 + 2) == indexData.end())
				break;

			unsigned short TriangleBIndices[3] = { *it2,*(it2 + 1),*(it2 + 2) };

			std::vector<unsigned short> edgeIndices = {};
			std::vector<unsigned short> neighbourIndices = {};

			for (int i = 0; i < 3; i++) {
				unsigned short A = TriangleAIndices[i];
				for (int j = 0; j < 3; j++) {
					if (A == TriangleBIndices[j])
						edgeIndices.push_back(A);

					if (edgeIndices.size() > 1) {
						//The triangles have a shared edge, Construct the new edge-data and add it to the edgelist
						unsigned short edgeIndexA = edgeIndices[0];
						unsigned short edgeIndexB = edgeIndices[1];
						std::vector<unsigned short> triangleIndices;

						triangleIndices.assign(TriangleAIndices[0], TriangleAIndices[2]);
						for (int q = 0; q < 3; q++)
							triangleIndices.push_back(TriangleBIndices[q]);
						//std::merge(TriangleAIndices, TriangleAIndices + 3, TriangleBIndices, TriangleBIndices + 3, triangleIndices.begin());

						for (auto edgeit = triangleIndices.begin(); edgeit != triangleIndices.end(); edgeit++) {
							if (*edgeit != edgeIndexA || *edgeit != edgeIndexB)
								neighbourIndices.push_back(*edgeit);
						}

						auto vertexPair = std::make_pair(vertexData[edgeIndices[0]], vertexData[edgeIndices[1]]);
						auto normalsPair = std::make_pair(vertexNormalsData[neighbourIndices[0]], vertexNormalsData[neighbourIndices[1]]);

						Edge newEdge = Edge(triangleIndices, vertexPair, normalsPair);
						edgeList->push_back(newEdge);
						edgeCounter++;
						break;
					}
				}
				if (edgeIndices.size() > 1)
					break;
			}
		}
	}
	char buffr[255];
	sprintf_s(buffr, 255, "\nFound %d edges,\nFound %d edges total\n\n", edgeCounter, edgeList->size());
	OutputDebugStringA(buffr);
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

void HolographicSpatialMapping::HolographicSpatialMappingMain::CalculateEdgeWeight(Edge edge, EdgeOperator mode)
{
	auto it1 = edge.triangleVertices.begin();

	switch (mode) {
	case SOD:

		//ASSUMPTION OF VERTEX ORDER: {<triangleAvertices>,<triangleBvertices>}
		DirectX::XMVECTOR triangleANormal = normal(DirectX::XMVECTOR() = { it1->x,it1->y,it1->z }, DirectX::XMVECTOR() = { (it1 + 1)->x,(it1 + 1)->y ,(it1 + 1)->z }, DirectX::XMVECTOR() = { (it1 + 2)->x ,(it1 + 2)->y ,(it1 + 2)->z });
		DirectX::XMVECTOR triangleBNormal = normal(DirectX::XMVECTOR() = { (it1 + 3)->x,(it1 + 3)->y,(it1 + 3)->z }, DirectX::XMVECTOR() = { (it1 + 4)->x,(it1 + 4)->y ,(it1 + 4)->z }, DirectX::XMVECTOR() = { (it1 + 5)->x ,(it1 + 5)->y ,(it1 + 5)->z });

		edge.weight = DirectX::XMScalarACos(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVector3Normalize(triangleANormal), DirectX::XMVector3Normalize(triangleBNormal))));
		break;

	case ESOD:

		DirectX::XMVECTOR vertexAnormal = DirectX::XMLoadFloat3(&edge.outlyingVertexNormals.first);
		DirectX::XMVECTOR vertexBnormal = DirectX::XMLoadFloat3(&edge.outlyingVertexNormals.second);

		edge.weight = DirectX::XMScalarACos(DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVector3Normalize(vertexAnormal), DirectX::XMVector3Normalize(vertexBnormal))));

		break;
	default:
		OutputDebugStringW(L"No opearator-mode selected for edge-weight calculations!, available are: SOD, ESOD\n");
		return;
	}
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
	//Initialize external data buffers if needed
	if (vertexMap == nullptr)
		vertexMap = (std::map<GUID, std::vector<DirectX::XMFLOAT3>>*)new std::map<GUID, std::vector<DirectX::XMFLOAT3>>();
	if (normalsMap == nullptr)
		normalsMap = (std::map<GUID, std::vector<DirectX::XMFLOAT3>>*)new std::map<GUID, std::vector<DirectX::XMFLOAT3>>();
	if (indexMap == nullptr)
		indexMap = (std::map<GUID, std::vector<unsigned short>>*)new std::map<GUID, std::vector<unsigned short>>();

	if (edgeList == nullptr)
		edgeList = new std::vector<Edge>;

	//Pull vertex data
	if (needSpatialMapping && m_surfaceObserver) {
		auto options = ref new SpatialSurfaceMeshOptions();
		options->IncludeVertexNormals = true;
		options->VertexPositionFormat = Windows::Graphics::DirectX::DirectXPixelFormat::R32G32B32A32Float;
		options->VertexNormalFormat = Windows::Graphics::DirectX::DirectXPixelFormat::R32G32B32A32Float;
		options->TriangleIndexFormat = Windows::Graphics::DirectX::DirectXPixelFormat::R32UInt;

		auto surfaceMap = m_surfaceObserver->GetObservedSurfaces();

		for (auto const& pair : surfaceMap)
		{
			// Store the ID and metadata for each surface.
			auto const& id = pair->Key;
			auto const& surfaceInfo = pair->Value;

			auto createMeshTask = create_task(surfaceInfo->TryComputeLatestMeshAsync(meshDensity, options));

			createMeshTask.then([this, id](SpatialSurfaceMesh^ mesh)
			{
				if (mesh != nullptr)
				{
					char msgbuf[100];
					sprintf_s(msgbuf, 100, "Surface ID: %u:\n", mesh->SurfaceInfo->Id);
					OutputDebugStringA(msgbuf);

					std::lock_guard<std::mutex> guard(meshMutex);
					PopulateEdgeList(mesh);
				}
			}, task_continuation_context::use_current());

		}

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
				// Draw the sample hologram.
				m_meshRenderer->Render(pCameraResources->IsRenderingStereoscopic(), m_drawWireframe);
			}
#endif
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
	delete &vertexMap;
	delete &normalsMap;
	delete &indexMap;
	vertexMap, normalsMap, indexMap = nullptr;
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
