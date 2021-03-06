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
#include "Kdtree.h"
#define MATLAB_DATA
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

		//Helper function for populating edge-list needed for edge-weight calculations
		void HolographicSpatialMapping::HolographicSpatialMappingMain::PopulateEdgeList(
			Windows::Perception::Spatial::Surfaces::SpatialSurfaceMesh^ mesh
		);

		void HolographicSpatialMapping::HolographicSpatialMappingMain::newSurfaces(Windows::Perception::Spatial::Surfaces::SpatialSurfaceObserver^ sender, Platform::Object^ args);
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
		EdgeOperator mode = ESOD;

		double meshDensity = 1000.0;
		float weightThreshold = 0.55f;

		//Weight calculation methods
		float HolographicSpatialMapping::HolographicSpatialMappingMain::CalculateSODWeight(DirectX::XMFLOAT3 triangleA[3], DirectX::XMFLOAT3 triangleB[3]);
		float HolographicSpatialMapping::HolographicSpatialMappingMain::CalculateESODWeight(DirectX::XMFLOAT3 vertexANormal, DirectX::XMFLOAT3 vertexBNormal);

		float HolographicSpatialMapping::HolographicSpatialMappingMain::CalculateSODWeight(Triangle triangleA, Triangle triangleB);

		//std::map<GUID,std::vector<DirectX::XMFLOAT3>>* vertexMap = nullptr;
		//std::map<GUID,std::vector<DirectX::XMFLOAT3>>* normalsMap = nullptr;
		//std::map<GUID,std::vector<unsigned short>>* indexMap = nullptr;
		
		std::mutex meshMutex;

		std::unique_ptr<EdgeRenderer> edgeRenderer;

		Windows::Foundation::EventRegistrationToken surfaceUpdateToken;
		std::vector<Platform::Guid> surfaceIDs;
		Windows::Perception::Spatial::Surfaces::SpatialSurfaceMeshOptions^ options;

		bool needsExtraction = true;
		bool needSpatialMapping = true;
		bool featuresExtracted = false;

		//Text output 
#ifdef MATLAB_DATA
		std::mutex matLock;
		bool firstSurf = true;
		std::ofstream stream;
		std::string folderPath;
		int surfcount = 0;
#endif
		//---
    };
}
