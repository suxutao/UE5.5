// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHIResources.h"
#include "RHIResourceCollection.h"
#include "RHICommandList.h"
#include "DynamicRHI.h"
#include "Containers/ResourceArray.h"

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING

namespace UE::RHICore
{
	template<typename TType>
	inline size_t CalculateResourceCollectionMemorySize(TConstArrayView<TType> InValues)
	{
		return (1 + InValues.Num()) * sizeof(uint32);
	}

	inline FRHIDescriptorHandle GetHandleForResourceCollectionValue(const FRHIResourceCollectionMember& Member)
	{
		switch (Member.Type)
		{
		case FRHIResourceCollectionMember::EType::Texture:
			return static_cast<const FRHITexture*>(Member.Resource)->GetDefaultBindlessHandle();
		case FRHIResourceCollectionMember::EType::TextureReference:
			return static_cast<const FRHITextureReference*>(Member.Resource)->GetBindlessHandle();
		case FRHIResourceCollectionMember::EType::ShaderResourceView:
			return static_cast<const FRHIShaderResourceView*>(Member.Resource)->GetBindlessHandle();
		}

		return FRHIDescriptorHandle();
	}

	inline FRHIDescriptorHandle GetHandleForResourceCollectionValue(const FRHIDescriptorHandle& Handle)
	{
		return Handle;
	}

	template<typename TType>
	inline void FillResourceCollectionMemory(TArray<uint32>& Destination, TConstArrayView<TType> InValues)
	{
		Destination.SetNumUninitialized(1 + InValues.Num());

		int32 WriteIndex = 0;

		Destination[WriteIndex] = static_cast<uint32>(InValues.Num());
		++WriteIndex;

		for (const TType& Value : InValues)
		{
			const FRHIDescriptorHandle Handle = GetHandleForResourceCollectionValue(Value);
			check(Handle.IsValid());

			const uint32 BindlessIndex = Handle.IsValid() ? Handle.GetIndex() : 0;

			Destination[WriteIndex] = BindlessIndex;
			++WriteIndex;
		}
	}

	template<typename TType>
	inline TArray<uint32> CreateResourceCollectionArray(TConstArrayView<TType> InValues)
	{
		TArray<uint32> Result;
		FillResourceCollectionMemory(Result, InValues);
		return Result;
	}

	struct FResourceCollectionUpload : public FResourceArrayUploadInterface
	{
		FResourceCollectionUpload(TConstArrayView<FRHIResourceCollectionMember> InMembers)
			: Memory(CreateResourceCollectionArray(InMembers))
		{
		}
		virtual const void* GetResourceData() const final
		{
			return Memory.GetData();
		}
		virtual uint32 GetResourceDataSize() const final
		{
			return Memory.Num() * Memory.GetTypeSize();
		}
		virtual void Discard() final
		{
			Memory.Reset();
		}

		TArray<uint32> Memory;
	};

	inline FRHIBuffer* CreateResourceCollectionBuffer(FRHICommandListBase& RHICmdList, TConstArrayView<FRHIResourceCollectionMember> InMembers)
	{
		FResourceCollectionUpload UploadData(InMembers);

		FRHIResourceCreateInfo CreateInfo(TEXT("ResourceCollection"), &UploadData);
		return RHICmdList.CreateBuffer(UploadData.GetResourceDataSize(), EBufferUsageFlags::Static | EBufferUsageFlags::ByteAddressBuffer, sizeof(uint32), ERHIAccess::SRVMask, CreateInfo);
	}

	class FGenericResourceCollection : public FRHIResourceCollection
	{
	public:
		FGenericResourceCollection(FRHICommandListBase& RHICmdList, TConstArrayView<FRHIResourceCollectionMember> InMembers)
			: FRHIResourceCollection(InMembers)
			, Buffer(CreateResourceCollectionBuffer(RHICmdList, InMembers))
		{
			FRHIViewDesc::FBufferSRV::FInitializer ViewDesc = FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Raw);
			ShaderResourceView = RHICmdList.CreateShaderResourceView(Buffer, ViewDesc);
		}

		~FGenericResourceCollection() = default;

		// FRHIResourceCollection
		virtual FRHIDescriptorHandle GetBindlessHandle() const final
		{
			return ShaderResourceView->GetBindlessHandle();
		}
		//~FRHIResourceCollection

		FRHIShaderResourceView* GetShaderResourceView() const
		{
			return ShaderResourceView;
		}

		TRefCountPtr<FRHIBuffer> Buffer;
		TRefCountPtr<FRHIShaderResourceView> ShaderResourceView;
	};

	inline FRHIResourceCollectionRef CreateGenericResourceCollection(FRHICommandListBase& RHICmdList, TConstArrayView<FRHIResourceCollectionMember> InMembers)
	{
		return new FGenericResourceCollection(RHICmdList, InMembers);
	}
}

#endif // PLATFORM_SUPPORTS_BINDLESS_RENDERING
