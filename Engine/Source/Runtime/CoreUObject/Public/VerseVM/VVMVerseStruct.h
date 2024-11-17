// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "VerseVM/VVMVerseEffectSet.h"
#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMEmergentType.h"
#endif

#include "VVMVerseStruct.generated.h"

class UStructCookedMetaData;
class UVerseClass;

UCLASS()
class COREUOBJECT_API UVerseStruct : public UScriptStruct
{
	GENERATED_BODY()

	/** Creates the field/property links and gets structure ready for use at runtime */
	virtual void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	virtual uint32 GetStructTypeHash(const void* Src) const override;

public:
	/** EVerseClassFlags */
	UPROPERTY()
	uint32 VerseClassFlags;

	virtual FGuid GetCustomGuid() const override
	{
		return Guid;
	}

	/** Function used for initialization */
	UPROPERTY()
	TObjectPtr<UFunction> InitFunction;

	/** Parent module class */
	UPROPERTY()
	TObjectPtr<UVerseClass> ModuleClass;

	/** GUID to be able to match old version of this struct to new one */
	UPROPERTY()
	FGuid Guid;

	UPROPERTY()
	TObjectPtr<UFunction> FactoryFunction;

	UPROPERTY()
	TObjectPtr<UFunction> OverrideFactoryFunction;

	UPROPERTY()
	EVerseEffectSet ConstructorEffects;

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	Verse::TWriteBarrier<Verse::VEmergentType> EmergentType;
#endif

	virtual FString GetAuthoredNameForField(const FField* Field) const override;

	void InvokeDefaultFactoryFunction(uint8* InStructData) const;

private:
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UStructCookedMetaData> CachedCookedMetaDataPtr;
#endif // WITH_EDITORONLY_DATA
};
