// Copyright Epic Games, Inc. All Rights Reserved.

#include "NDIMediaOutput.h"

#include "NDIMediaCapture.h"
#include "UnrealEngine.h"

UNDIMediaOutput::UNDIMediaOutput(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UNDIMediaOutput::Validate(FString& OutFailureReason) const
{
	if (!Super::Validate(OutFailureReason))
	{
		return false;
	}

	if (GetRequestedPixelFormat() != PF_B8G8R8A8 )
	{
		OutFailureReason = FString::Printf(TEXT("Can't validate MediaOutput '%s'. Only Supported format is RTF RGBA8 (PF_B8G8R8A8)"), *GetName());
		return false;
	}

	return true;
}

FIntPoint UNDIMediaOutput::GetRequestedSize() const
{
	return (bOverrideDesiredSize) ? DesiredSize : UMediaOutput::RequestCaptureSourceSize;
}

EPixelFormat UNDIMediaOutput::GetRequestedPixelFormat() const
{
	return PF_B8G8R8A8;
}

EMediaCaptureConversionOperation UNDIMediaOutput::GetConversionOperation(EMediaCaptureSourceType /*InSourceType*/) const
{
	if (OutputType == EMediaIOOutputType::Fill)
	{
		return EMediaCaptureConversionOperation::RGBA8_TO_YUV_8BIT;
	}
	else if (OutputType == EMediaIOOutputType::FillAndKey && bInvertKeyOutput)
	{
		// Another options is to convert to NDIlib_FourCC_type_UYVA, but this would need
		// a custom conversion (with and without alpha inversion).
		// For now, we keep the format as RGBA, but only invert the alpha if needed.
		return EMediaCaptureConversionOperation::INVERT_ALPHA;
	}
	else
	{
		return EMediaCaptureConversionOperation::NONE;
	}
}

UMediaCapture* UNDIMediaOutput::CreateMediaCaptureImpl()
{
	UMediaCapture* Result = NewObject<UNDIMediaCapture>();
	if (Result)
	{
		UE_LOG(LogNDIMedia, Log, TEXT("Created NDI Media Capture"));
		Result->SetMediaOutput(this);
	}
	return Result;
}
