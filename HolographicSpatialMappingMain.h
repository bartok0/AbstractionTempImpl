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

#pragma once

//
// Comment out this preprocessor definition to disable all of the
// sample content.
//
#define DRAW_SAMPLE_CONTENT

#include "Common\DeviceResources.h"
#include "Common\StepTimer.h"

#ifdef DRAW_SAMPLE_CONTENT
#include "Content\SpatialInputHandler.h"
#include "Content\RealtimeSurfaceMeshRenderer.h"
#endif

//---
#include "EdgeRenderer.h"
//---

// Updates, renders, and presents holographic content using Direct3D.
namespace HolographicSpatialMapping
{
    class HolographicSpatialMappingMain : public DX::IDeviceNotify
    {
    public:
        HolographicSpatialMappingMain(const std::shared_ptr<DX::DeviceResources>& deviceResources);
        ~HolographicSpatialMappingMain();

        // Sets the holographic space. This is our closest analogue to setting a new window
        // for the app.
        void SetHolographicSpace(Windows::Graphics::Holographic::HolographicSpace^ holographicSpace);

        // Starts the holographic frame and updates the content.
        Windows::Graphics::Holographic::HolographicFrame^ Update();

        // Renders holograms, including world-locked content.
        bool Render(Windows::Graphics::Holographic::HolographicFrame^ holographicFrame);

        // Handle saving and loading of app state owned by AppMain.
        void SaveAppState();
        void LoadAppState();

        // IDeviceNotify
        virtual void OnDeviceLost();
        virtual void OnDeviceRestored();

        // Handle surface change events.
        void OnSurfacesChanged(Windows::Perception::Spatial::Surfaces::SpatialSurfaceObserver^ sender, Platform::Object^ args);

		//---
		enum EdgeOperator { SOD, ESOD };

		//Mesh edge structure
		struct Edge
		{
			//Edge weight
			float weight = -1.0;

			//Edge indices
			std::vector<unsigned short> triangleIndices;
			std::pair<unsigned short, unsigned short> edgeIndices;
			std::pair<DirectX::XMFLOAT3, DirectX::XMFLOAT3> edgeVertices;
			std::pair<DirectX::XMFLOAT3, DirectX::XMFLOAT3> outlyingVertices;
			std::pair<unsigned short, unsigned short> outlyingIndices;

			//Vertex position and normal data
			std::pair<DirectX::XMFLOAT3, DirectX::XMFLOAT3> outlyingVertexNormals;
			std::vector<DirectX::XMFLOAT3> triangleVertices;

			Edge(
				std::vector<unsigned short> TriangleIndices,
				std::pair<DirectX::XMFLOAT3, DirectX::XMFLOAT3> EdgeVertices,
				std::pair<DirectX::XMFLOAT3,DirectX::XMFLOAT3> neighbourVertexNormals
			){
				triangleIndices = TriangleIndices;
				outlyingVertexNormals = neighbourVertexNormals;
				edgeVertices = EdgeVertices;
			}
		};

		inline void assignEdgeIndices(Edge* edge, std::pair<unsigned int, unsigned int> indices){
			edge->edgeIndices = indices;
		}

		inline void assignOutlyingIndices(Edge* edge, std::pair<unsigned int, unsigned int> indices) {
			edge->outlyingIndices = indices;
		};

		//Helper function for populating edge-list needed for edge-weight calculations
		void PopulateEdgeList(Windows::Perception::Spatial::Surfaces::SpatialSurfaceMesh^ mesh);

		//Function for calculating edge weight
		void CalculateEdgeWeight(Edge edge, EdgeOperator mode);
		//---

    private:
        // Asynchronously creates resources for new holographic cameras.
        void OnCameraAdded(
            Windows::Graphics::Holographic::HolographicSpace^ sender,
            Windows::Graphics::Holographic::HolographicSpaceCameraAddedEventArgs^ args);

        // Synchronously releases resources for holographic cameras that are no longer
        // attached to the system.
        void OnCameraRemoved(
            Windows::Graphics::Holographic::HolographicSpace^ sender,
            Windows::Graphics::Holographic::HolographicSpaceCameraRemovedEventArgs^ args);
        
        // Used to prevent the device from deactivating positional tracking, which is 
        // necessary to continue to receive spatial mapping data.
        void OnPositionalTrackingDeactivating(
            Windows::Perception::Spatial::SpatialLocator^ sender,
            Windows::Perception::Spatial::SpatialLocatorPositionalTrackingDeactivatingEventArgs^ args);

        // Clears event registration state. Used when changing to a new HolographicSpace
        // and when tearing down AppMain.
        void UnregisterHolographicEventHandlers();

#ifdef DRAW_SAMPLE_CONTENT
        // Listens for the Pressed spatial input event.
        std::shared_ptr<SpatialInputHandler>                                m_spatialInputHandler;

        // A data handler for surface meshes.
        std::unique_ptr<WindowsHolographicCodeSamples::RealtimeSurfaceMeshRenderer> m_meshRenderer;
#endif

        // Cached pointer to device resources.
        std::shared_ptr<DX::DeviceResources>                                m_deviceResources;

        // Render loop timer.
        DX::StepTimer                                                       m_timer;

        // Represents the holographic space around the user.
        Windows::Graphics::Holographic::HolographicSpace^                   m_holographicSpace;

        // SpatialLocator that is attached to the primary camera.
        Windows::Perception::Spatial::SpatialLocator^                       m_locator;

        // A reference frame attached to the holographic camera.
        Windows::Perception::Spatial::SpatialLocatorAttachedFrameOfReference^ m_referenceFrame;

        // Event registration tokens.
        Windows::Foundation::EventRegistrationToken                         m_cameraAddedToken;
        Windows::Foundation::EventRegistrationToken                         m_cameraRemovedToken;
        Windows::Foundation::EventRegistrationToken                         m_positionalTrackingDeactivatingToken;
        Windows::Foundation::EventRegistrationToken                         m_surfacesChangedToken;

        // Indicates whether access to spatial mapping data has been granted.
        bool                                                                m_surfaceAccessAllowed = false;

        // Indicates whether the surface observer initialization process was started.
        bool                                                                m_spatialPerceptionAccessRequested = false;

        // Obtains spatial mapping data from the device in real time.
        Windows::Perception::Spatial::Surfaces::SpatialSurfaceObserver^     m_surfaceObserver;
        Windows::Perception::Spatial::Surfaces::SpatialSurfaceMeshOptions^  m_surfaceMeshOptions;

        // Determines the rendering mode.
        bool                                                                m_drawWireframe = true;

		//---
		//Flag for setting feature extraction mode, available values are:
		//
		//"SOD": Second Order Difference
		//"ESOD": Extended Second Order Difference
		EdgeOperator mode = SOD;

		double meshDensity = 1000.0;

		//Spatial surface mesh data used for feature extraction:
		//std::vector<Windows::Storage::Streams::IBuffer^> vertexNormals;
		//std::vector<Windows::Storage::Streams::IBuffer^> vertexIndices;
		//std::vector<Windows::Storage::Streams::IBuffer^> vertexPositions;

		//Triangle edge list
		std::vector<Edge>* edgeList = nullptr;
		//Edges to be drawn in the 'edge renderer'
		std::vector<DirectX::XMFLOAT3> edgeVertices;

		std::map<GUID,std::vector<DirectX::XMFLOAT3>>* vertexMap = nullptr;
		std::map<GUID,std::vector<DirectX::XMFLOAT3>>* normalsMap = nullptr;
		std::map<GUID, std::vector<unsigned short>>* indexMap = nullptr;

		//std::vector<std::pair<DirectX::XMFLOAT3,DirectX::XMFLOAT3>> lineVertices;
		
		std::mutex meshMutex;

		std::unique_ptr<EdgeRenderer> edgeRenderer;

		bool needsExtraction = true;
		bool needSpatialMapping = true;
		bool featuresExtracted = false;

		//---
    };
}
