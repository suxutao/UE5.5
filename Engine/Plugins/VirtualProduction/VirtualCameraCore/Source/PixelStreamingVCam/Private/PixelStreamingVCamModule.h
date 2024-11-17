// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Networking/VirtualCameraBeaconReceiver.h"

#include "Modules/ModuleInterface.h"
#include "UObject/WeakObjectPtr.h"

class UVCamPixelStreamingSession;

namespace UE::PixelStreamingVCam
{
	class FPixelStreamingVCamModule : public IModuleInterface
	{
	public:
		
		static FPixelStreamingVCamModule& Get();

		//~ Begin IModuleInterface Interface
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
		//~ End IModuleInterface Interface

		/** Indicate that a VCAM pixel streaming session has become active. */
		void AddActiveSession(const TWeakObjectPtr<UVCamPixelStreamingSession>& Session);

		/** Indicate that a VCAM pixel streaming session has become inactive. */
		void RemoveActiveSession(const TWeakObjectPtr<UVCamPixelStreamingSession>& Session);

	private:
		
		/** Receiver that responds to beacon messages from the VCAM app. */
		FVirtualCameraBeaconReceiver BeaconReceiver;

		/** VCAM Pixel Streaming sessions that are currently active. */
		TSet<TWeakObjectPtr<UVCamPixelStreamingSession>, TWeakObjectPtrSetKeyFuncs<TWeakObjectPtr<UVCamPixelStreamingSession>>> ActiveSessions;
		
		/** Configure CVars and session logic for Pixel Streaming. */
		void ConfigurePixelStreaming();

		/** Update the beacon receiver's streaming readiness state based on the number of active sessions. */
		void UpdateBeaconReceiverStreamReadiness();
	};
}

