// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneIndices.h"

template <typename T>
class FGLTFMeshAttributesArray : public TArray<T>
{
public:

	using TArray<T>::TArray;
	using TArray<T>::operator=;

	friend uint32 GetTypeHash(const FGLTFMeshAttributesArray& AttributesArray)
	{
		uint32 Hash = GetTypeHash(AttributesArray.Num());
		for (const T& Attribute : AttributesArray)
		{
			Hash = HashCombine(Hash, GetTypeHash(Attribute));
		}
		return Hash;
	}
};

typedef FGLTFMeshAttributesArray<FColor> FGLTFColorArray;
typedef FGLTFMeshAttributesArray<int32> FGLTFIndexArray;
typedef FGLTFMeshAttributesArray<UE::Math::TIntVector4<FBoneIndexType>> FGLTFJointInfluenceArray;
typedef FGLTFMeshAttributesArray<UE::Math::TIntVector4<uint16>> FGLTFJointWeightArray;
typedef FGLTFMeshAttributesArray<FVector3f> FGLTFNormalArray;
typedef FGLTFMeshAttributesArray<FVector3f> FGLTFPositionArray;
typedef FGLTFMeshAttributesArray<FVector4f> FGLTFTangentArray;
typedef FGLTFMeshAttributesArray<FVector2f> FGLTFUVArray;